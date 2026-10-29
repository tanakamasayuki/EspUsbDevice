// Independent device/EP0 engine with native class and controller boundaries. See
// third_party/tinyusb/PROVENANCE.md for the adapter's MIT-licensed origins.
#include "EspUsbDeviceCore.h"
#include "EspUsbDeviceAppDriver.h"
#include "EspUsbDeviceEndpoint.h"
#include "EspUsbDwc2Runtime.h"
#include "EspUsbMemory.h"
#include "EspUsbController.h"
#include "EspUsbDeviceBus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <new>
#include <string.h>

namespace {
constexpr uint8_t Invalid = 255, Busy = 1, Claimed = 2, Halted = 4;
// Deferred jobs are worker queue items, never controller events.
constexpr uint8_t DeferredCall = 255, WakeEnabled = 1, WakePending = 2;
constexpr size_t PacketBytes = 64;
portMUX_TYPE hardwareLock = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE endpointLock = portMUX_INITIALIZER_UNLOCKED;
struct Endpoint {
  uint8_t driver = Invalid, type = 0, interfaceNumber = Invalid;
  std::atomic<bool> opened{false};
  std::atomic<uint8_t> flags{0};
};
struct Event {
  EspUsbControllerEvent data;
  uint32_t bus = 0, setup = 0, configurationEpoch = 0;
};
struct Control {
  enum class Phase : uint8_t { Idle, Setup, Data, Status, Stalled } phase = Phase::Idle;
  EspUsbControlRequest request = {};
  uint8_t *buffer = nullptr;
  uint16_t capacity = 0, transferred = 0, armed = 0;
  uint32_t serial = 0, bus = 0;
  decltype(espusb::internal::UsbClassDriver::control_xfer_cb) callback = nullptr;
};
struct Core {
  uint8_t port = Invalid, interfaceCount = 0, endpointCount = 0, driverCount = 0;
  uint16_t queueDepth = 0;
  const espusb::internal::UsbClassDriver *drivers = nullptr;
  Endpoint *endpoints = nullptr;
  uint8_t *interfaces = nullptr, *packet = nullptr;
  StaticQueue_t queueState{};
  portMUX_TYPE eventLock = portMUX_INITIALIZER_UNLOCKED;
  QueueHandle_t queue = nullptr;
  Control control;
  std::atomic<uint32_t> bus{0}, setup{0}, workerBus{0}, configurationEpoch{0};
  std::atomic<bool> overflow{false}, connected{false}, suspended{false};
  std::atomic<uint8_t> configuration{0};
  std::atomic<EspUsbSpeed> speed{ESPUSB_SPEED_INVALID};
  bool addressed = false, selfPowered = false;
  std::atomic<uint8_t> wakeupFlags{0};
  std::atomic<bool> recovering{false}, stopping{false}, resetting{false};
  Endpoint *endpoint(uint8_t address) {
    if ((address & 0x70) || (address & 15) >= endpointCount) return nullptr;
    return endpoints + (address & 15)*2 + (address >> 7);
  }
  const espusb::internal::UsbClassDriver *driver(uint8_t index) const {
    return index < driverCount ? drivers + index : nullptr;
  }
  void invalidateBus() {
    resetting.store(true);configuration.store(0);connected.store(false);suspended.store(false);bus.fetch_add(1);
  }
  bool currentControl() const {
    return control.serial == setup.load() && control.bus == bus.load() && !stopping && !recovering && !resetting;
  }
  bool post(const Event &event, bool inIsr) {
    if (!queue || stopping) return false;
    BaseType_t woken = pdFALSE;
    portENTER_CRITICAL(&eventLock);
    // Keep one physical slot for hardware events. A queued fence must not
    // prevent the pending control status (or next SETUP) from being recorded.
    const bool deferred = event.data.event_id == DeferredCall;
    const UBaseType_t pending = inIsr ? uxQueueMessagesWaitingFromISR(queue) : uxQueueMessagesWaiting(queue);
    bool ok = false;
    if (!deferred || pending < queueDepth) {
      ok = inIsr ? xQueueSendToBackFromISR(queue,&event,&woken) == pdTRUE
                 : xQueueSendToBack(queue,&event,0) == pdTRUE;
    }
    portEXIT_CRITICAL(&eventLock);
    if (inIsr && woken) portYIELD_FROM_ISR();
    return ok;
  }
  void resetDrivers() {
    configurationEpoch.fetch_add(1);
    configuration.store(0); wakeupFlags.store(0); selfPowered = false;
    control = {};
    for (unsigned i=0;i<unsigned(endpointCount)*2;++i) {
      endpoints[i].driver=Invalid; endpoints[i].type=0; endpoints[i].interfaceNumber=Invalid; endpoints[i].opened.store(false); endpoints[i].flags.store(0);
    }
    memset(interfaces,Invalid,interfaceCount);
    for (unsigned i=0;i<driverCount;++i) if (drivers[i].reset) drivers[i].reset(port);
  }
  void stallControl() {
    control.phase = Control::Phase::Stalled;
    espusb_controller_edpt_stall(port,0); espusb_controller_edpt_stall(port,128);
    endpoints[0].flags = endpoints[1].flags = Halted;
  }
  bool status() {
    if (!currentControl() || (control.phase!=Control::Phase::Setup && control.phase!=Control::Phase::Data)) return false;
    control.phase = Control::Phase::Status;
    const uint8_t ep = control.request.wLength && (control.request.bmRequestType & 128) ? 0 : 128;
    return espusb_device_edpt_xfer(port,ep,nullptr,0,false);
  }
  bool data() {
    if (!currentControl() || control.transferred > control.capacity) return false;
    control.armed = uint16_t(std::min(size_t(control.capacity-control.transferred),PacketBytes));
    const bool input = control.request.bmRequestType & 128;
    if (input && control.armed) memcpy(packet,control.buffer+control.transferred,control.armed);
    control.phase = Control::Phase::Data;
    return espusb_device_edpt_xfer(port,input ? 128 : 0,control.armed ? packet : nullptr,control.armed,false);
  }
  void completedControl(const Event &event) {
    auto &c=control; const auto &x=event.data.xfer_complete;
    if (event.setup != c.serial || event.bus != c.bus) return;
    // A completed status packet commits the old request even if a new SETUP
    // has arrived since. Old data must never arm more traffic after new SETUP.
    if (c.phase != Control::Phase::Status && !currentControl()) return;
    if (x.result != ESPUSB_TRANSFER_SUCCESS) { stallControl(); return; }
    const uint8_t statusEp = c.request.wLength && (c.request.bmRequestType&128) ? 0 : 128;
    if (c.phase == Control::Phase::Status) {
      if (x.ep_addr != statusEp || x.len) { stallControl(); return; }
      c.phase = Control::Phase::Idle;
      if (c.request.bmRequestType==0 && c.request.bRequest==5) addressed = c.request.wValue != 0;
      espusb_device_control_acknowledged(port,&c.request);
      if (c.callback) c.callback(port,ESPUSB_CONTROL_STAGE_ACK,&c.request);
      return;
    }
    if (c.phase != Control::Phase::Data || x.ep_addr == statusEp || x.len > c.armed) { stallControl(); return; }
    if (!(c.request.bmRequestType&128) && x.len) memcpy(c.buffer+c.transferred,packet,x.len);
    c.transferred += uint16_t(x.len);
    if (c.transferred == c.request.wLength || x.len < PacketBytes) {
      if (c.callback && !c.callback(port,ESPUSB_CONTROL_STAGE_DATA,&c.request)) { stallControl(); return; }
      if (!status() && currentControl()) stallControl();
    } else if (!data() && currentControl()) stallControl();
  }
  bool reply(const void *buffer,uint16_t bytes) {
    auto &c=control;
    if (!currentControl() || c.phase != Control::Phase::Setup || (bytes && !buffer)) return false;
    if (!c.request.wLength) return status();
    if (!(c.request.bmRequestType&128) && bytes < c.request.wLength) return false;
    c.buffer = static_cast<uint8_t *>(const_cast<void *>(buffer));
    c.capacity = std::min(bytes,c.request.wLength); c.transferred = 0;
    return data();
  }
  bool invoke(const espusb::internal::UsbClassDriver *d) {
    control.callback = d ? d->control_xfer_cb : nullptr;
    return control.callback && control.callback(port,ESPUSB_CONTROL_STAGE_SETUP,&control.request);
  }
  bool descriptor() {
    const auto &r=control.request;
    if (r.bmRequestType!=128) return false;
    const uint8_t type=r.wValue>>8, index=uint8_t(r.wValue);
    const uint8_t *p=nullptr; uint16_t bytes=0;
    switch (type) {
    case 1:
      if (index || r.wIndex) return false;
      p=espusb_device_descriptor_device(); if (p) bytes=p[0]; break;
    case 2: case 7:
      if (r.wIndex) return false;
      p=type==2 ? espusb_device_descriptor_configuration(index) : espusb_device_descriptor_other_speed_configuration(index);
      if (p) bytes=uint16_t(p[2]) | uint16_t(p[3])<<8; break;
    case 3:
      p=reinterpret_cast<const uint8_t *>(espusb_device_descriptor_string(index,r.wIndex)); if (p) bytes=p[0]; break;
    case 6:
      if (index || r.wIndex) return false;
      p=espusb_device_descriptor_device_qualifier(); if (p) bytes=p[0]; break;
    case 15:
      if (index || r.wIndex) return false;
      p=espusb_device_descriptor_bos(); if (p) bytes=uint16_t(p[2]) | uint16_t(p[3])<<8; break;
    default: return false;
    }
    return p && bytes >= 2 && reply(p,bytes);
  }
  bool configure(uint8_t value) {
    const auto *p=espusb_device_descriptor_configuration(0);
    if (!p || p[0]!=9 || p[1]!=2 || (value && value!=p[5])) return false;
    const uint16_t length=uint16_t(p[2]) | uint16_t(p[3])<<8;
    // Even reselecting the same configuration resets endpoints and alternates.
    // Preserve this SETUP while resetting class transport state.
    const Control pending=control;
    espusb_controller_sof_enable(port,false); espusb_controller_edpt_close_all(port);
    resetDrivers(); control=pending;
    if (!value) { espusb_device_on_unmount(); return true; }
    configuration.store(value); selfPowered = p[7]&64;
    size_t offset=9;
    while (offset+2 <= length) {
      if (p[offset]<2 || p[offset]>length-offset) return false;
      if (p[offset+1]==11) { offset+=p[offset]; continue; }
      if (p[offset+1]!=4 || p[offset]!=9) return false;
      bool found=false;
      for (unsigned i=0;i<driverCount && !found;++i) {
        if (!drivers[i].open) continue;
        const uint16_t used=drivers[i].open(port,reinterpret_cast<const EspUsbInterfaceDescriptor *>(p+offset),uint16_t(length-offset));
        if (!used) continue;
        if (used<9 || used>length-offset) return false;
        size_t n=0; uint8_t interfaceNumber=Invalid;
        while (n<used) {
          const auto *d=p+offset+n;
          if (n+2>used || d[0]<2 || d[0]>used-n) return false;
          if (d[1]==4) {
            if (d[0]!=9 || d[2]>=interfaceCount || (interfaces[d[2]]!=Invalid && interfaces[d[2]]!=i)) return false;
            interfaces[d[2]]=uint8_t(i); interfaceNumber=d[2];
          } else if (d[1]==5) {
            auto *ep=d[0]>=7 ? endpoint(d[2]) : nullptr;
            if (!ep || !(d[2]&15) || (ep->driver!=Invalid && ep->driver!=i)) return false;
            if (interfaceNumber==Invalid || (ep->interfaceNumber!=Invalid && ep->interfaceNumber!=interfaceNumber)) return false;
            ep->driver=uint8_t(i); ep->interfaceNumber=interfaceNumber;
          }
          n+=d[0];
        }
        offset+=used; found=true;
      }
      if (!found) return false;
    }
    if (offset!=length) return false;
    for (unsigned i=0;i<interfaceCount;++i) if (interfaces[i]==Invalid) return false;
    espusb_device_on_mount(); return true;
  }
  bool standardDevice() {
    const auto &r=control.request;
    switch (r.bRequest) {
    case 0: {
      if (r.bmRequestType!=128 || r.wValue || r.wIndex || r.wLength!=2) return false;
      uint8_t state[2]={uint8_t(unsigned(selfPowered) | unsigned(bool(wakeupFlags.load() & WakeEnabled))<<1),0}; return reply(state,2);
    }
    case 1: case 3: {
      const auto *p=espusb_device_descriptor_configuration(0);
      if (r.bmRequestType || r.wValue!=1 || r.wIndex || r.wLength || !configuration.load() || !p || !(p[7]&32)) return false;
      if (r.bRequest==3) wakeupFlags.fetch_or(WakeEnabled); else wakeupFlags.fetch_and(uint8_t(~WakeEnabled));
      return status();
    }
    case 5:
      if (r.bmRequestType || r.wValue>127 || r.wIndex || r.wLength || configuration.load()) return false;
      // The remaining DWC2 adapter owns SET_ADDRESS timing and its status arm.
      control.phase=Control::Phase::Status; espusb_controller_set_address(port,uint8_t(r.wValue)); return true;
    case 6: return descriptor();
    case 8: {
      if (r.bmRequestType!=128 || r.wValue || r.wIndex || r.wLength!=1 || !addressed) return false;
      const uint8_t value=configuration.load(); return reply(&value,1);
    }
    case 9:
      if (r.bmRequestType || r.wValue>255 || r.wIndex || r.wLength || !addressed) return false;
      { const auto *cfg=espusb_device_descriptor_configuration(0);
        if (!cfg || (r.wValue && r.wValue!=cfg[5])) return false; }
      if (!configure(uint8_t(r.wValue))) {
        const Control pending=control; espusb_controller_edpt_close_all(port); resetDrivers(); control=pending; return false;
      }
      return status();
    default: return false;
    }
  }
  bool dispatch() {
    const auto &r=control.request;
    const unsigned type=(r.bmRequestType>>5)&3, recipient=r.bmRequestType&31;
    if (type==3) return false;
    if (type==2) { control.callback=espusb_device_vendor_control; return control.callback(port,ESPUSB_CONTROL_STAGE_SETUP,&r); }
    if (recipient==0) {
      if (!type) return standardDevice();
      const unsigned itf=uint8_t(r.wIndex);
      return configuration.load() && itf<interfaceCount && invoke(driver(interfaces[itf]));
    }
    if (!configuration.load() && !(recipient==2 && !(r.wIndex&15))) return false;
    if (recipient==1) {
      const unsigned itf=uint8_t(r.wIndex);
      if (itf>=interfaceCount || (!type && (r.wIndex>>8))) return false;
      if (!type) {
        if ((r.bRequest==0 && (r.bmRequestType!=129 || r.wValue || r.wLength!=2)) ||
            (r.bRequest==10 && (r.bmRequestType!=129 || r.wValue || r.wLength!=1)) ||
            (r.bRequest==11 && (r.bmRequestType!=1 || r.wLength || r.wValue>255))) return false;
      }
      if (invoke(driver(interfaces[itf]))) return true;
      control.callback=nullptr;
      if (type) return false;
      uint8_t zero[2]={};
      if (r.bRequest==0) return reply(zero,2);
      if (r.bRequest==10) return reply(zero,1);
      return r.bRequest==11 && !r.wValue && status();
    }
    if (recipient==2) {
      if (r.wIndex>255) return false;
      auto *ep=endpoint(uint8_t(r.wIndex));
      if (!ep || ((r.wIndex&15) && !ep->opened)) return false;
      const auto *d=driver(ep->driver);
      if (type) return invoke(d);
      if (r.bRequest==0) {
        if (r.bmRequestType!=130 || r.wValue || r.wLength!=2) return false;
        uint8_t state[2]={uint8_t(bool(ep->flags&Halted)),0}; return reply(state,2);
      }
      if ((r.bRequest!=1 && r.bRequest!=3) || r.bmRequestType!=2 || r.wValue || r.wLength || !(r.wIndex&15) || !d || ep->type==1) return false;
      if (r.bRequest==1) espusb_device_edpt_clear_stall(port,uint8_t(r.wIndex)); else espusb_device_edpt_stall(port,uint8_t(r.wIndex));
      invoke(d); control.callback=nullptr;
      return control.phase!=Control::Phase::Setup || status();
    }
    return false;
  }
  void recoverOverflow() {
    // Losing a completion must not strand a borrowed buffer. Quiesce DMA,
    // return class ownership once and reconnect for an explicit bus recovery.
    recovering=true;
    espusb_controller_int_disable(port); espusb_controller_disconnect(port); espusb_controller_edpt_close_all(port);
    connected.store(false); suspended.store(false); addressed=false;
    resetDrivers(); espusb_device_on_unmount();
    Event old;
    // Let old fences retire their own queued bookkeeping after reset. Posting
    // new fences is refused until recovery ends; other old events are obsolete.
    while (xQueueReceive(queue,&old,0)==pdTRUE) if (old.data.event_id==DeferredCall && old.data.func_call.func) old.data.func_call.func(old.data.func_call.param);
    workerBus.store(bus.load()); overflow.store(false); resetting=false; recovering=false;
    vTaskDelay(std::max(TickType_t(1),pdMS_TO_TICKS(20)));
    espusb_controller_connect(port); if(!espusb_controller_int_enable(port))overflow.store(true);
  }
};
Core *core=nullptr;
bool wakeReady(const Core *s) {
  return s && !s->stopping && !s->recovering && !s->resetting && !s->overflow &&
      s->workerBus.load()==s->bus.load() && s->configuration.load() && s->connected &&
      s->suspended && (s->wakeupFlags.load() & WakeEnabled);
}
void runRemoteWakeup(void *context) {
  const uint32_t epoch=uint32_t(reinterpret_cast<uintptr_t>(context));
  portENTER_CRITICAL(&hardwareLock);
  auto *s=core;
  const bool current=s && s->configurationEpoch.load()==epoch;
  const bool allowed=current && (s->wakeupFlags.load() & WakePending) && wakeReady(s);
  if(current && !allowed)s->wakeupFlags.fetch_and(uint8_t(~WakePending));
  portEXIT_CRITICAL(&hardwareLock);
  if(!allowed)return;
  // A full five milliseconds after observing suspend is conservative relative
  // to the USB five-millisecond idle minimum. No persistent timer is needed.
  vTaskDelay(pdMS_TO_TICKS(5)+1);
  portENTER_CRITICAL(&hardwareLock);
  s=core;
  if(s && s->configurationEpoch.load()==epoch) {
    if((s->wakeupFlags.load() & WakePending) && wakeReady(s))espusb_controller_remote_wakeup(s->port);
    s->wakeupFlags.fetch_and(uint8_t(~WakePending));
  }
  portEXIT_CRITICAL(&hardwareLock);
}
size_t aligned(size_t n,size_t alignment) { return (n+alignment-1)&~(alignment-1); }
}

namespace espusb { namespace internal {
esp_err_t startUsbDeviceCore(uint8_t port,bool highSpeed,uint16_t queueDepth) {
  if (core) return ESP_ERR_INVALID_STATE;
  if (!queueDepth) return ESP_ERR_INVALID_SIZE;
  const auto *cfg=espusb_device_descriptor_configuration(0);
  if (!cfg || cfg[0]!=9 || cfg[1]!=2) return ESP_ERR_INVALID_STATE;
  const unsigned count=cfg[4]; const uint16_t length=uint16_t(cfg[2]) | uint16_t(cfg[3])<<8;
  unsigned epCount=1; uint16_t outMask=1, inMask=1;
  for (size_t n=9;n<length;) {
    if (n+2>length || cfg[n]<2 || cfg[n]>length-n) return ESP_ERR_INVALID_ARG;
    if (cfg[n+1]==5) {
      if (cfg[n]<7 || (cfg[n+2]&0x70) || !(cfg[n+2]&15)) return ESP_ERR_INVALID_ARG;
      epCount=std::max(epCount,unsigned(cfg[n+2]&15)+1);
      (cfg[n+2]&128 ? inMask:outMask) |= uint16_t(1u << (cfg[n+2]&15));
    }
    n+=cfg[n];
  }
  size_t epOffset=aligned(sizeof(Core),alignof(Endpoint));
  size_t itfOffset=epOffset+epCount*2*sizeof(Endpoint);
  size_t queueOffset=aligned(itfOffset+count,alignof(Event));
  size_t packetOffset=aligned(queueOffset+(size_t(queueDepth)+1)*sizeof(Event),64);
  const size_t dcdOffset=packetOffset+64;
  const size_t dcdBytes=espusb_dwc2_runtime_size(port,outMask,inMask);
  if (!dcdBytes) return ESP_ERR_INVALID_ARG;
  auto *memory=static_cast<uint8_t *>(allocateUsbMemory(dcdOffset+dcdBytes,64,true));
  if (!memory) return ESP_ERR_NO_MEM;
  auto *s=new (memory) Core;
  s->port=port; s->queueDepth=queueDepth; s->interfaceCount=uint8_t(count); s->endpointCount=uint8_t(epCount);
  s->endpoints=reinterpret_cast<Endpoint *>(memory+epOffset); s->interfaces=memory+itfOffset; s->packet=memory+packetOffset;
  for (unsigned i=0;i<epCount*2;++i) new(s->endpoints+i) Endpoint;
  memset(s->interfaces,Invalid,count);
  s->drivers=espusb::internal::selectedUsbClassDrivers(s->driverCount);
  s->queue=xQueueCreateStatic(UBaseType_t(queueDepth)+1,sizeof(Event),memory+queueOffset,&s->queueState);
  if (!s->queue) { s->~Core(); freeUsbMemory(s); return ESP_ERR_NO_MEM; }
  if (!espusb_dwc2_runtime_prepare(port,memory+dcdOffset,dcdBytes,outMask,inMask)) {
    vQueueDelete(s->queue); s->~Core(); freeUsbMemory(s); return ESP_FAIL;
  }
  portENTER_CRITICAL(&hardwareLock); core=s; portEXIT_CRITICAL(&hardwareLock);
  for (unsigned i=0;i<s->driverCount;++i) if(s->drivers[i].init) s->drivers[i].init();
  if (!espusb_controller_init(port,highSpeed)) { stopUsbDeviceCore(); return ESP_FAIL; }
  if (!espusb_controller_int_enable(port)) { stopUsbDeviceCore(); return ESP_FAIL; }
  return ESP_OK;
}
void stopUsbDeviceCore() {
  auto *s=core; if(!s)return;
  portENTER_CRITICAL(&hardwareLock); s->stopping=true; portEXIT_CRITICAL(&hardwareLock);
  espusb_controller_int_disable(s->port); espusb_controller_disconnect(s->port); espusb_controller_deinit(s->port);
  const bool mounted=s->configuration.exchange(0)!=0;
  for(unsigned i=0;i<s->driverCount;++i) if(s->drivers[i].deinit)s->drivers[i].deinit();
  if(mounted)espusb_device_on_unmount();
  espusb_dwc2_runtime_release();
  portENTER_CRITICAL(&hardwareLock); core=nullptr; portEXIT_CRITICAL(&hardwareLock);
  vQueueDelete(s->queue); s->~Core(); freeUsbMemory(s);
}
bool usbRemoteWakeupEnabled() {
  if(xPortInIsrContext())return false;
  portENTER_CRITICAL(&hardwareLock);
  auto *s=core;
  const bool enabled=s && !s->stopping && !s->resetting && s->configuration.load() && (s->wakeupFlags.load() & WakeEnabled);
  portEXIT_CRITICAL(&hardwareLock); return enabled;
}
esp_err_t requestUsbRemoteWakeup() {
  if(xPortInIsrContext())return ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&hardwareLock);
  auto *s=core;
  esp_err_t result=ESP_ERR_INVALID_STATE;
  if(wakeReady(s) && !(s->wakeupFlags.load() & WakePending)) {
    s->wakeupFlags.fetch_or(WakePending);
    Event event={}; event.data.rhport=s->port; event.data.event_id=DeferredCall;
    event.data.func_call.func=runRemoteWakeup;
    // An opaque epoch value, never dereferenced. No per-request allocation.
    event.data.func_call.param=reinterpret_cast<void *>(uintptr_t(s->configurationEpoch.load()));
    if(s->post(event,false))result=ESP_OK;
    else {s->wakeupFlags.fetch_and(uint8_t(~WakePending));result=ESP_ERR_TIMEOUT;}
  }
  portEXIT_CRITICAL(&hardwareLock); return result;
}
uint8_t usbEndpointInterface(uint8_t port,uint8_t address) {
  auto *ep=core && core->port==port ? core->endpoint(address) : nullptr;
  return ep ? ep->interfaceNumber : Invalid;
}
uint16_t usbControlTransferredBytes() {return core ? core->control.transferred : 0;}
bool rejectUsbControl() {
  if(core && core->currentControl())core->stallControl();
  return false;
}
bool usbDeviceCoreReadyForJobs() {
  return core && !core->overflow.load() && espusb_controller_io_ready(core->port);
}
void runUsbDeviceCore(uint32_t milliseconds) {
  auto *s=core;if(!s)return;
  if(s->overflow.load())s->recoverOverflow();
  Event event;
  TickType_t wait=milliseconds ? std::max(TickType_t(1),pdMS_TO_TICKS(milliseconds)) : 0;
  // A bounded drain leaves time for producer jobs even on a busy event queue.
  for(unsigned n=0;(n<s->queueDepth || s->workerBus.load()!=s->bus.load()) && xQueueReceive(s->queue,&event,wait)==pdTRUE;++n,wait=0) {
    if(s->overflow.load()){s->recoverOverflow();break;}
    const auto &e=event.data;
    if(e.event_id==DeferredCall) {if(e.func_call.func)e.func_call.func(e.func_call.param);continue;}
    if(event.bus!=s->bus.load())continue;
    switch(e.event_id) {
    case ESPUSB_CONTROLLER_EVENT_BUS_RESET:
      s->workerBus.store(event.bus);
      s->addressed=false;s->connected.store(false);s->suspended.store(false);s->speed=e.bus_reset.speed;
      s->resetDrivers();s->resetting=false;break;
    case ESPUSB_CONTROLLER_EVENT_UNPLUGGED:
      s->workerBus.store(event.bus);
      s->addressed=false;s->connected.store(false);s->suspended.store(false);s->resetDrivers();espusb_device_on_unmount();s->resetting=false;break;
    case ESPUSB_CONTROLLER_EVENT_SETUP_RECEIVED:
      if(event.setup!=s->setup.load())break;
      s->connected.store(true);s->endpoints[0].flags=s->endpoints[1].flags=0;
      s->control={};s->control.request=e.setup_received;s->control.serial=event.setup;s->control.bus=event.bus;s->control.phase=Control::Phase::Setup;
      if(!s->dispatch() && s->currentControl())s->stallControl();break;
    case ESPUSB_CONTROLLER_EVENT_XFER_COMPLETE: {
      // Reconfiguration may already have reopened the same endpoint and
      // armed a different buffer. Old queued data must not clear its flags
      // or call the newly initialized class. EP0 uses its own SETUP serial.
      if((e.xfer_complete.ep_addr&15) && event.configurationEpoch!=s->configurationEpoch.load())break;
      auto *ep=s->endpoint(e.xfer_complete.ep_addr);if(!ep)break;
      {portENTER_CRITICAL(&endpointLock);ep->flags &= uint8_t(~(Busy|Claimed));portEXIT_CRITICAL(&endpointLock);}
      if(!(e.xfer_complete.ep_addr&15))s->completedControl(event);
      else {const auto *d=s->driver(ep->driver);if(d && d->xfer_cb)d->xfer_cb(s->port,e.xfer_complete.ep_addr,EspUsbControllerResult(e.xfer_complete.result),e.xfer_complete.len);}
      break;
    }
    default:break;
    }
  }
}
} }

// Native device/controller entry points. There is one active core.
extern "C" {
void espusb_controller_lock(bool) {portENTER_CRITICAL(&hardwareLock);}
void espusb_controller_unlock(bool) {portEXIT_CRITICAL(&hardwareLock);}
void espusb_controller_reset_begin(uint8_t port) {
  if(core && port==core->port && !core->stopping)core->invalidateBus();
}
bool espusb_controller_io_ready(uint8_t port) {
  return core && port==core->port && !core->stopping && !core->recovering && !core->resetting && core->workerBus.load()==core->bus.load();
}
EspUsbSpeed espusb_device_speed() {return core ? core->speed.load() : ESPUSB_SPEED_INVALID;}
bool espusb_device_mounted() {return core && espusb_controller_io_ready(core->port) && core->configuration.load();}
bool espusb_device_suspended() {return core && core->suspended.load();}
bool espusb_device_disconnect() {if(!core)return false;espusb_controller_disconnect(core->port);return true;}
bool espusb_device_events_pending() {return core && (core->overflow.load() || uxQueueMessagesWaiting(core->queue));}
uint16_t espusb_device_control_received_bytes() {return espusb::internal::usbControlTransferredBytes();}
bool espusb_device_control_transfer(uint8_t port,const EspUsbControlRequest *request,void *buffer,uint16_t bytes) {
  return core && port==core->port && request && !memcmp(request,&core->control.request,sizeof(*request)) && core->reply(buffer,bytes);
}
bool espusb_device_control_status(uint8_t port,const EspUsbControlRequest *request) {
  return core && port==core->port && request && !request->wLength && core->control.phase==Control::Phase::Setup && !memcmp(request,&core->control.request,sizeof(*request)) && core->status();
}
bool espusb_device_edpt_open(uint8_t port,const EspUsbEndpointDescriptor *descriptor) {
  if(!espusb_controller_io_ready(port) || !descriptor || descriptor->bLength<7 || descriptor->bDescriptorType!=5)return false;
  auto *ep=core->endpoint(descriptor->bEndpointAddress);if(!ep || !(descriptor->bEndpointAddress&15))return false;
  const unsigned type=espusb_endpoint_type(descriptor), packet=espusb_endpoint_packet_size(descriptor);
  if(!packet || type==0 || (type==2 && (core->speed==ESPUSB_SPEED_HIGH ? packet!=512 : (packet!=8 && packet!=16 && packet!=32 && packet!=64))) ||
      (type==1 && packet>(core->speed==ESPUSB_SPEED_HIGH ? 1024u : 1023u)) ||
      (type==3 && packet>(core->speed==ESPUSB_SPEED_HIGH ? 1024u : 64u)))return false;
  if(!espusb_controller_edpt_open(port,descriptor))return false;
  ep->opened=true;ep->type=uint8_t(type);ep->flags=0;return true;
}
bool espusb_device_edpt_iso_activate(uint8_t port,const EspUsbEndpointDescriptor *descriptor) {
  if (!core || port!=core->port || !descriptor || descriptor->bLength<7 || descriptor->bDescriptorType!=5 || espusb_endpoint_type(descriptor)!=ESPUSB_XFER_ISOCHRONOUS) return false;
  auto *ep=core->endpoint(descriptor->bEndpointAddress);
  const unsigned packet=espusb_endpoint_packet_size(descriptor);
  if (!ep || !ep->opened || ep->type!=ESPUSB_XFER_ISOCHRONOUS || (ep->flags.load()&(Busy|Claimed|Halted)) || !packet || packet>(core->speed==ESPUSB_SPEED_HIGH ? 1024u : 1023u)) return false;
  return espusb_controller_edpt_iso_activate(port,descriptor);
}
bool espusb_device_edpt_claim(uint8_t port,uint8_t address) {
  if(!espusb_controller_io_ready(port))return false;
  auto *ep=core->endpoint(address);if(!ep || ((address&15) && !ep->opened))return false;
  portENTER_CRITICAL(&endpointLock);bool ok=!(ep->flags&(Busy|Claimed|Halted));if(ok)ep->flags|=Claimed;portEXIT_CRITICAL(&endpointLock);return ok;
}
bool espusb_device_edpt_release(uint8_t port,uint8_t address) {
  if(!core || port!=core->port)return false;auto *ep=core->endpoint(address);if(!ep)return false;
  portENTER_CRITICAL(&endpointLock);bool ok=(ep->flags&Claimed) && !(ep->flags&Busy);if(ok)ep->flags &= uint8_t(~Claimed);portEXIT_CRITICAL(&endpointLock);return ok;
}
bool espusb_device_edpt_xfer(uint8_t port,uint8_t address,uint8_t *buffer,uint32_t bytes,bool inIsr) {
  if(!espusb_controller_io_ready(port) || (bytes && !buffer))return false;
  if((address&15) && (core->workerBus.load()!=core->bus.load() || !core->configuration.load()))return false;
  auto *ep=core->endpoint(address);if(!ep || ((address&15) && !ep->opened))return false;
  portENTER_CRITICAL(&endpointLock);bool ok=!(ep->flags&(Busy|Halted));if(ok)ep->flags|=Busy;portEXIT_CRITICAL(&endpointLock);
  if(!ok)return false;
  if(espusb_controller_edpt_xfer(port,address,buffer,bytes,inIsr))return true;
  portENTER_CRITICAL(&endpointLock);ep->flags &= uint8_t(~(Busy|Claimed));portEXIT_CRITICAL(&endpointLock);return false;
}
bool espusb_device_edpt_busy(uint8_t port,uint8_t address) {auto *ep=core && port==core->port ? core->endpoint(address):nullptr;return ep && (ep->flags&Busy);}
bool espusb_device_edpt_stalled(uint8_t port,uint8_t address) {auto *ep=core && port==core->port ? core->endpoint(address):nullptr;return ep && (ep->flags&Halted);}
void espusb_device_edpt_stall(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;if(!ep)return;
  espusb_controller_edpt_stall(port,address);ep->flags |= Busy|Halted;
}
void espusb_device_edpt_clear_stall(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;if(!ep)return;
  espusb_controller_edpt_clear_stall(port,address);ep->flags &= (ep->flags&Halted) ? 0 : Claimed;
}
bool espusb_device_edpt_abort_supported() {return espusb_controller_edpt_abort_supported();}
bool espusb_device_edpt_abort(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;
  if(!ep || !(address&15) || !espusb_controller_edpt_abort(port,address))return false;
  ep->flags=(ep->flags&Halted) ? Busy|Halted : 0;return true;
}
bool espusb_device_try_defer(void (*function)(void *),void *context) {
  if(!core || !function || core->recovering || core->stopping)return false;
  Event event={};event.data.rhport=core->port;event.data.event_id=DeferredCall;event.data.func_call.func=function;event.data.func_call.param=context;
  return core->post(event,false);
}
uint16_t espusb_device_queue_capacity() {return core ? core->queueDepth : 0;}
void espusb_controller_event(const EspUsbControllerEvent *data,bool inIsr) {
  auto *s=core;if(!s || !data || data->rhport!=s->port || s->stopping ||
      data->event_id<=ESPUSB_CONTROLLER_EVENT_INVALID || data->event_id>=ESPUSB_CONTROLLER_EVENT_COUNT)return;
  if(data->event_id==ESPUSB_CONTROLLER_EVENT_BUS_RESET || data->event_id==ESPUSB_CONTROLLER_EVENT_UNPLUGGED) {
    s->invalidateBus();
  }
  if(data->event_id==ESPUSB_CONTROLLER_EVENT_SETUP_RECEIVED)s->setup.fetch_add(1);
  if(data->event_id==ESPUSB_CONTROLLER_EVENT_SUSPEND || data->event_id==ESPUSB_CONTROLLER_EVENT_RESUME) {
    if(!s->connected.load())return;s->suspended.store(data->event_id==ESPUSB_CONTROLLER_EVENT_SUSPEND);
  }
  if(data->event_id==ESPUSB_CONTROLLER_EVENT_UNPLUGGED){s->connected.store(false);s->configuration.store(0);s->suspended.store(false);}
  if(data->event_id==ESPUSB_CONTROLLER_EVENT_SOF) {
    for(unsigned i=0;i<s->driverCount;++i)if(s->drivers[i].sof)s->drivers[i].sof(s->port,data->sof.frame_count);
    if(s->suspended.exchange(false)) {EspUsbControllerEvent resume={};resume.rhport=s->port;resume.event_id=ESPUSB_CONTROLLER_EVENT_RESUME;espusb_controller_event(&resume,inIsr);}
    return; // Class SOF callbacks above have already run in interrupt context.
  }
  Event event={};event.data=*data;event.bus=s->bus.load();event.setup=s->setup.load();event.configurationEpoch=s->configurationEpoch.load();
  if(!s->post(event,inIsr))s->overflow.store(true);
  espusb_device_event_hook(data->rhport,data->event_id,inIsr);
}
// Private observer for adversarial event probes; no public callback contract.
__attribute__((weak)) void espusb_device_event_hook(uint8_t,uint32_t,bool) {}
}
