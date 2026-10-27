// Independent device/EP0 engine. The temporary C ABI below connects existing
// first-party classes and the remaining TinyUSB DWC2 adapter. See
// third_party/tinyusb/PROVENANCE.md for the adapter's MIT-licensed origins.
#include "EspUsbDeviceCore.h"
#include "EspUsbDwc2Runtime.h"
#include "EspUsbMemory.h"
#include "device/dcd.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "freertos/queue.h"
#include <algorithm>
#include <atomic>
#include <new>
#include <string.h>

namespace {
constexpr uint8_t Invalid = 255, Busy = 1, Claimed = 2, Halted = 4;
constexpr size_t PacketBytes = 64;
portMUX_TYPE hardwareLock = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE endpointLock = portMUX_INITIALIZER_UNLOCKED;
struct Endpoint {
  uint8_t driver = Invalid, type = 0, interfaceNumber = Invalid;
  std::atomic<bool> opened{false};
  std::atomic<uint8_t> flags{0};
};
struct Event {
  dcd_event_t data;
  uint32_t bus = 0, setup = 0, configurationEpoch = 0;
};
struct Control {
  enum class Phase : uint8_t { Idle, Setup, Data, Status, Stalled } phase = Phase::Idle;
  tusb_control_request_t request = {};
  uint8_t *buffer = nullptr;
  uint16_t capacity = 0, transferred = 0, armed = 0;
  uint32_t serial = 0, bus = 0;
  usbd_control_xfer_cb_t callback = nullptr;
};
struct Core {
  uint8_t port = Invalid, interfaceCount = 0, endpointCount = 0, driverCount = 0;
  uint16_t queueDepth = 0;
  const usbd_class_driver_t *drivers = nullptr;
  Endpoint *endpoints = nullptr;
  uint8_t *interfaces = nullptr, *packet = nullptr;
  StaticQueue_t queueState{};
  portMUX_TYPE eventLock = portMUX_INITIALIZER_UNLOCKED;
  QueueHandle_t queue = nullptr;
  Control control;
  std::atomic<uint32_t> bus{0}, setup{0}, workerBus{0}, configurationEpoch{0};
  std::atomic<bool> overflow{false}, connected{false}, suspended{false};
  std::atomic<uint8_t> configuration{0};
  std::atomic<tusb_speed_t> speed{TUSB_SPEED_INVALID};
  bool addressed = false, selfPowered = false;
  std::atomic<bool> remoteWakeup{false}, recovering{false}, stopping{false};
  std::atomic<uint8_t> sofConsumers{0};
  Endpoint *endpoint(uint8_t address) {
    if ((address & 0x70) || (address & 15) >= endpointCount) return nullptr;
    return endpoints + (address & 15)*2 + (address >> 7);
  }
  const usbd_class_driver_t *driver(uint8_t index) const {
    return index < driverCount ? drivers + index : nullptr;
  }
  bool currentControl() const {
    return control.serial == setup.load() && control.bus == bus.load() && !stopping && !recovering;
  }
  bool post(const Event &event, bool inIsr) {
    if (!queue || stopping) return false;
    BaseType_t woken = pdFALSE;
    portENTER_CRITICAL(&eventLock);
    // Keep one physical slot for hardware events. A queued fence must not
    // prevent the pending control status (or next SETUP) from being recorded.
    const bool deferred = event.data.event_id == USBD_EVENT_FUNC_CALL;
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
    configuration.store(0); remoteWakeup = selfPowered = false; sofConsumers = 0;
    control = {};
    for (unsigned i=0;i<unsigned(endpointCount)*2;++i) {
      endpoints[i].driver=Invalid; endpoints[i].type=0; endpoints[i].interfaceNumber=Invalid; endpoints[i].opened.store(false); endpoints[i].flags.store(0);
    }
    memset(interfaces,Invalid,interfaceCount);
    for (unsigned i=0;i<driverCount;++i) if (drivers[i].reset) drivers[i].reset(port);
  }
  void stallControl() {
    control.phase = Control::Phase::Stalled;
    dcd_edpt_stall(port,0); dcd_edpt_stall(port,128);
    endpoints[0].flags = endpoints[1].flags = Halted;
  }
  bool status() {
    if (!currentControl() || (control.phase!=Control::Phase::Setup && control.phase!=Control::Phase::Data)) return false;
    control.phase = Control::Phase::Status;
    const uint8_t ep = control.request.wLength && (control.request.bmRequestType & 128) ? 0 : 128;
    return usbd_edpt_xfer(port,ep,nullptr,0,false);
  }
  bool data() {
    if (!currentControl() || control.transferred > control.capacity) return false;
    control.armed = uint16_t(std::min(size_t(control.capacity-control.transferred),PacketBytes));
    const bool input = control.request.bmRequestType & 128;
    if (input && control.armed) memcpy(packet,control.buffer+control.transferred,control.armed);
    control.phase = Control::Phase::Data;
    return usbd_edpt_xfer(port,input ? 128 : 0,control.armed ? packet : nullptr,control.armed,false);
  }
  void completedControl(const Event &event) {
    auto &c=control; const auto &x=event.data.xfer_complete;
    if (event.setup != c.serial || event.bus != c.bus) return;
    // A completed status packet commits the old request even if a new SETUP
    // has arrived since. Old data must never arm more traffic after new SETUP.
    if (c.phase != Control::Phase::Status && !currentControl()) return;
    if (x.result != XFER_RESULT_SUCCESS) { stallControl(); return; }
    const uint8_t statusEp = c.request.wLength && (c.request.bmRequestType&128) ? 0 : 128;
    if (c.phase == Control::Phase::Status) {
      if (x.ep_addr != statusEp || x.len) { stallControl(); return; }
      c.phase = Control::Phase::Idle;
      if (c.request.bmRequestType==0 && c.request.bRequest==5) addressed = c.request.wValue != 0;
      dcd_edpt0_status_complete(port,&c.request);
      if (c.callback) c.callback(port,CONTROL_STAGE_ACK,&c.request);
      return;
    }
    if (c.phase != Control::Phase::Data || x.ep_addr == statusEp || x.len > c.armed) { stallControl(); return; }
    if (!(c.request.bmRequestType&128) && x.len) memcpy(c.buffer+c.transferred,packet,x.len);
    c.transferred += uint16_t(x.len);
    if (c.transferred == c.request.wLength || x.len < PacketBytes) {
      if (c.callback && !c.callback(port,CONTROL_STAGE_DATA,&c.request)) { stallControl(); return; }
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
  bool invoke(const usbd_class_driver_t *d) {
    control.callback = d ? d->control_xfer_cb : nullptr;
    return control.callback && control.callback(port,CONTROL_STAGE_SETUP,&control.request);
  }
  bool descriptor() {
    const auto &r=control.request;
    if (r.bmRequestType!=128) return false;
    const uint8_t type=r.wValue>>8, index=uint8_t(r.wValue);
    const uint8_t *p=nullptr; uint16_t bytes=0;
    switch (type) {
    case 1:
      if (index || r.wIndex) return false;
      p=tud_descriptor_device_cb(); if (p) bytes=p[0]; break;
    case 2: case 7:
      if (r.wIndex) return false;
      p=type==2 ? tud_descriptor_configuration_cb(index) : tud_descriptor_other_speed_configuration_cb(index);
      if (p) bytes=uint16_t(p[2]) | uint16_t(p[3])<<8; break;
    case 3:
      p=reinterpret_cast<const uint8_t *>(tud_descriptor_string_cb(index,r.wIndex)); if (p) bytes=p[0]; break;
    case 6:
      if (index || r.wIndex) return false;
      p=tud_descriptor_device_qualifier_cb(); if (p) bytes=p[0]; break;
    case 15:
      if (index || r.wIndex) return false;
      p=tud_descriptor_bos_cb(); if (p) bytes=uint16_t(p[2]) | uint16_t(p[3])<<8; break;
    default: return false;
    }
    return p && bytes >= 2 && reply(p,bytes);
  }
  bool configure(uint8_t value) {
    const auto *p=tud_descriptor_configuration_cb(0);
    if (!p || p[0]!=9 || p[1]!=2 || (value && value!=p[5])) return false;
    const uint16_t length=uint16_t(p[2]) | uint16_t(p[3])<<8;
    // Even reselecting the same configuration resets endpoints and alternates.
    // Preserve this SETUP while resetting class transport state.
    const Control pending=control;
    dcd_sof_enable(port,false); dcd_edpt_close_all(port);
    resetDrivers(); control=pending;
    if (!value) { tud_umount_cb(); return true; }
    configuration.store(value); selfPowered = p[7]&64;
    size_t offset=9;
    while (offset+2 <= length) {
      if (p[offset]<2 || p[offset]>length-offset) return false;
      if (p[offset+1]==11) { offset+=p[offset]; continue; }
      if (p[offset+1]!=4 || p[offset]!=9) return false;
      bool found=false;
      for (unsigned i=0;i<driverCount && !found;++i) {
        if (!drivers[i].open) continue;
        const uint16_t used=drivers[i].open(port,reinterpret_cast<const tusb_desc_interface_t *>(p+offset),uint16_t(length-offset));
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
    tud_mount_cb(); return true;
  }
  bool standardDevice() {
    const auto &r=control.request;
    switch (r.bRequest) {
    case 0: {
      if (r.bmRequestType!=128 || r.wValue || r.wIndex || r.wLength!=2) return false;
      uint8_t state[2]={uint8_t(unsigned(selfPowered) | unsigned(remoteWakeup)<<1),0}; return reply(state,2);
    }
    case 1: case 3: {
      const auto *p=tud_descriptor_configuration_cb(0);
      if (r.bmRequestType || r.wValue!=1 || r.wIndex || r.wLength || !configuration.load() || !p || !(p[7]&32)) return false;
      remoteWakeup=r.bRequest==3; return status();
    }
    case 5:
      if (r.bmRequestType || r.wValue>127 || r.wIndex || r.wLength || configuration.load()) return false;
      // The remaining DWC2 adapter owns SET_ADDRESS timing and its status arm.
      control.phase=Control::Phase::Status; dcd_set_address(port,uint8_t(r.wValue)); return true;
    case 6: return descriptor();
    case 8: {
      if (r.bmRequestType!=128 || r.wValue || r.wIndex || r.wLength!=1 || !addressed) return false;
      const uint8_t value=configuration.load(); return reply(&value,1);
    }
    case 9:
      if (r.bmRequestType || r.wValue>255 || r.wIndex || r.wLength || !addressed) return false;
      { const auto *cfg=tud_descriptor_configuration_cb(0);
        if (!cfg || (r.wValue && r.wValue!=cfg[5])) return false; }
      if (!configure(uint8_t(r.wValue))) {
        const Control pending=control; dcd_edpt_close_all(port); resetDrivers(); control=pending; return false;
      }
      return status();
    default: return false;
    }
  }
  bool dispatch() {
    const auto &r=control.request;
    const unsigned type=(r.bmRequestType>>5)&3, recipient=r.bmRequestType&31;
    if (type==3) return false;
    if (type==2) { control.callback=tud_vendor_control_xfer_cb; return control.callback(port,CONTROL_STAGE_SETUP,&r); }
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
      if (r.bRequest==1) usbd_edpt_clear_stall(port,uint8_t(r.wIndex)); else usbd_edpt_stall(port,uint8_t(r.wIndex));
      invoke(d); control.callback=nullptr;
      return control.phase!=Control::Phase::Setup || status();
    }
    return false;
  }
  void recoverOverflow() {
    // Losing a completion must not strand a borrowed buffer. Quiesce DMA,
    // return class ownership once and reconnect for an explicit bus recovery.
    recovering=true;
    dcd_int_disable(port); dcd_disconnect(port); dcd_edpt_close_all(port);
    connected.store(false); suspended.store(false); addressed=false;
    resetDrivers(); tud_umount_cb();
    Event old;
    // Let old fences retire their own queued bookkeeping after reset. Posting
    // new fences is refused until recovery ends; other old events are obsolete.
    while (xQueueReceive(queue,&old,0)==pdTRUE) if (old.data.event_id==USBD_EVENT_FUNC_CALL && old.data.func_call.func) old.data.func_call.func(old.data.func_call.param);
    workerBus.store(bus.load()); overflow.store(false); recovering=false;
    vTaskDelay(std::max(TickType_t(1),pdMS_TO_TICKS(20)));
    dcd_connect(port); dcd_int_enable(port);
  }
};
Core *core=nullptr;
size_t aligned(size_t n,size_t alignment) { return (n+alignment-1)&~(alignment-1); }
}

namespace espusb { namespace internal {
esp_err_t startUsbDeviceCore(uint8_t port,bool highSpeed,uint16_t queueDepth) {
  if (core) return ESP_ERR_INVALID_STATE;
  if (!queueDepth) return ESP_ERR_INVALID_SIZE;
  const auto *cfg=tud_descriptor_configuration_cb(0);
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
  s->drivers=usbd_app_driver_get_cb(&s->driverCount);
  s->queue=xQueueCreateStatic(UBaseType_t(queueDepth)+1,sizeof(Event),memory+queueOffset,&s->queueState);
  if (!s->queue) { s->~Core(); freeUsbMemory(s); return ESP_ERR_NO_MEM; }
  if (!espusb_dwc2_runtime_prepare(port,memory+dcdOffset,dcdBytes,outMask,inMask)) {
    vQueueDelete(s->queue); s->~Core(); freeUsbMemory(s); return ESP_FAIL;
  }
  core=s;
  for (unsigned i=0;i<s->driverCount;++i) if(s->drivers[i].init) s->drivers[i].init();
  const tusb_rhport_init_t init={TUSB_ROLE_DEVICE, highSpeed ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL};
  if (!dcd_init(port,&init)) { stopUsbDeviceCore(); return ESP_FAIL; }
  dcd_int_enable(port); return ESP_OK;
}
void stopUsbDeviceCore() {
  auto *s=core; if(!s)return;
  s->stopping=true; dcd_int_disable(s->port); dcd_disconnect(s->port); dcd_deinit(s->port);
  const bool mounted=s->configuration.exchange(0)!=0;
  for(unsigned i=0;i<s->driverCount;++i) if(s->drivers[i].deinit)s->drivers[i].deinit();
  if(mounted)tud_umount_cb();
  espusb_dwc2_runtime_release();
  vQueueDelete(s->queue); core=nullptr; s->~Core(); freeUsbMemory(s);
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
  return core && !core->overflow.load() && core->workerBus.load()==core->bus.load();
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
    if(e.event_id==USBD_EVENT_FUNC_CALL) {if(e.func_call.func)e.func_call.func(e.func_call.param);continue;}
    if(event.bus!=s->bus.load())continue;
    switch(e.event_id) {
    case DCD_EVENT_BUS_RESET:
      s->workerBus.store(event.bus);
      s->addressed=false;s->connected.store(false);s->suspended.store(false);s->speed=e.bus_reset.speed;
      s->resetDrivers();break;
    case DCD_EVENT_UNPLUGGED:
      s->workerBus.store(event.bus);
      s->addressed=false;s->connected.store(false);s->suspended.store(false);s->resetDrivers();tud_umount_cb();break;
    case DCD_EVENT_SETUP_RECEIVED:
      if(event.setup!=s->setup.load())break;
      s->connected.store(true);s->endpoints[0].flags=s->endpoints[1].flags=0;
      s->control={};s->control.request=e.setup_received;s->control.serial=event.setup;s->control.bus=event.bus;s->control.phase=Control::Phase::Setup;
      if(!s->dispatch() && s->currentControl())s->stallControl();break;
    case DCD_EVENT_XFER_COMPLETE: {
      // Reconfiguration may already have reopened the same endpoint and
      // armed a different buffer. Old queued data must not clear its flags
      // or call the newly initialized class. EP0 uses its own SETUP serial.
      if((e.xfer_complete.ep_addr&15) && event.configurationEpoch!=s->configurationEpoch.load())break;
      auto *ep=s->endpoint(e.xfer_complete.ep_addr);if(!ep)break;
      {portENTER_CRITICAL(&endpointLock);ep->flags &= uint8_t(~(Busy|Claimed));portEXIT_CRITICAL(&endpointLock);}
      if(!(e.xfer_complete.ep_addr&15))s->completedControl(event);
      else {const auto *d=s->driver(ep->driver);if(d && d->xfer_cb)d->xfer_cb(s->port,e.xfer_complete.ep_addr,xfer_result_t(e.xfer_complete.result),e.xfer_complete.len);}
      break;
    }
    case DCD_EVENT_SUSPEND: if(s->connected.load())tud_suspend_cb(s->remoteWakeup);break;
    case DCD_EVENT_RESUME: if(s->connected.load())tud_resume_cb();break;
    case DCD_EVENT_SOF: if(s->sofConsumers&1)tud_sof_cb(e.sof.frame_count);break;
    default:break;
    }
  }
}
} }

// Transitional class/DWC2 ABI. There is one active core, no legacy fallback.
extern "C" {
void usbd_spin_lock(bool) {portENTER_CRITICAL(&hardwareLock);}
void usbd_spin_unlock(bool) {portEXIT_CRITICAL(&hardwareLock);}
void usbd_int_set(bool enabled) {if(core){if(enabled)dcd_int_enable(core->port);else dcd_int_disable(core->port);}}
bool tud_inited() {return core!=nullptr;}
bool tud_rhport_init(uint8_t port,const tusb_rhport_init_t *init) {return init && espusb::internal::startUsbDeviceCore(port,init->speed==TUSB_SPEED_HIGH,16)==ESP_OK;}
bool tud_deinit(uint8_t) {espusb::internal::stopUsbDeviceCore();return true;}
bool tud_configure(uint8_t port,uint32_t id,const void *parameter) {return !core && dcd_configure(port,id,parameter);}
tusb_speed_t tud_speed_get() {return core ? core->speed.load() : TUSB_SPEED_INVALID;}
bool tud_connected() {return core && core->connected.load();}
bool tud_mounted() {return core && core->configuration.load();}
bool tud_suspended() {return core && core->suspended.load();}
bool tud_remote_wakeup() {if(!core || !core->suspended.load() || !core->remoteWakeup)return false;dcd_remote_wakeup(core->port);return true;}
bool tud_connect() {if(!core)return false;dcd_connect(core->port);return true;}
bool tud_disconnect() {if(!core)return false;dcd_disconnect(core->port);return true;}
void tud_task_ext(uint32_t milliseconds,bool) {espusb::internal::runUsbDeviceCore(milliseconds);}
bool tud_task_event_ready() {return core && (core->overflow.load() || uxQueueMessagesWaiting(core->queue));}
uint8_t *usbd_get_ctrl_buf() {return core ? core->packet : nullptr;}
uint16_t usbd_control_received_bytes() {return espusb::internal::usbControlTransferredBytes();}
bool tud_control_xfer(uint8_t port,const tusb_control_request_t *request,void *buffer,uint16_t bytes) {
  return core && port==core->port && request && !memcmp(request,&core->control.request,sizeof(*request)) && core->reply(buffer,bytes);
}
bool tud_control_status(uint8_t port,const tusb_control_request_t *request) {
  return core && port==core->port && request && !request->wLength && core->control.phase==Control::Phase::Setup && !memcmp(request,&core->control.request,sizeof(*request)) && core->status();
}
bool usbd_edpt_open(uint8_t port,const tusb_desc_endpoint_t *descriptor) {
  if(!core || port!=core->port || !descriptor || descriptor->bLength<7 || descriptor->bDescriptorType!=5)return false;
  auto *ep=core->endpoint(descriptor->bEndpointAddress);if(!ep || !(descriptor->bEndpointAddress&15))return false;
  const unsigned type=descriptor->bmAttributes.xfer, packet=tu_edpt_packet_size(descriptor);
  if(!packet || type==0 || (type==2 && (core->speed==TUSB_SPEED_HIGH ? packet!=512 : (packet!=8 && packet!=16 && packet!=32 && packet!=64))) ||
      (type==1 && packet>(core->speed==TUSB_SPEED_HIGH ? 1024u : 1023u)) ||
      (type==3 && packet>(core->speed==TUSB_SPEED_HIGH ? 1024u : 64u)))return false;
  if(!dcd_edpt_open(port,descriptor))return false;
  ep->opened=true;ep->type=uint8_t(type);ep->flags=0;return true;
}
bool usbd_edpt_claim(uint8_t port,uint8_t address) {
  if(!core || port!=core->port || core->stopping || core->recovering)return false;
  auto *ep=core->endpoint(address);if(!ep || ((address&15) && !ep->opened))return false;
  portENTER_CRITICAL(&endpointLock);bool ok=!(ep->flags&(Busy|Claimed|Halted));if(ok)ep->flags|=Claimed;portEXIT_CRITICAL(&endpointLock);return ok;
}
bool usbd_edpt_release(uint8_t port,uint8_t address) {
  if(!core || port!=core->port)return false;auto *ep=core->endpoint(address);if(!ep)return false;
  portENTER_CRITICAL(&endpointLock);bool ok=(ep->flags&Claimed) && !(ep->flags&Busy);if(ok)ep->flags &= uint8_t(~Claimed);portEXIT_CRITICAL(&endpointLock);return ok;
}
bool usbd_edpt_xfer(uint8_t port,uint8_t address,uint8_t *buffer,uint16_t bytes,bool inIsr) {
  if(!core || port!=core->port || core->stopping || core->recovering || (bytes && !buffer))return false;
  if((address&15) && (core->workerBus.load()!=core->bus.load() || !core->configuration.load()))return false;
  auto *ep=core->endpoint(address);if(!ep || ((address&15) && !ep->opened))return false;
  portENTER_CRITICAL(&endpointLock);bool ok=!(ep->flags&(Busy|Halted));if(ok)ep->flags|=Busy;portEXIT_CRITICAL(&endpointLock);
  if(!ok)return false;
  if(dcd_edpt_xfer(port,address,buffer,bytes,inIsr))return true;
  portENTER_CRITICAL(&endpointLock);ep->flags &= uint8_t(~(Busy|Claimed));portEXIT_CRITICAL(&endpointLock);return false;
}
bool usbd_edpt_busy(uint8_t port,uint8_t address) {auto *ep=core && port==core->port ? core->endpoint(address):nullptr;return ep && (ep->flags&Busy);}
bool usbd_edpt_stalled(uint8_t port,uint8_t address) {auto *ep=core && port==core->port ? core->endpoint(address):nullptr;return ep && (ep->flags&Halted);}
void usbd_edpt_stall(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;if(!ep)return;
  dcd_edpt_stall(port,address);ep->flags |= Busy|Halted;
}
void usbd_edpt_clear_stall(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;if(!ep)return;
  dcd_edpt_clear_stall(port,address);ep->flags &= (ep->flags&Halted) ? 0 : Claimed;
}
bool usbd_edpt_abort_supported() {return dcd_edpt_abort_supported();}
bool usbd_edpt_abort(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;
  if(!ep || !(address&15) || !dcd_edpt_abort(port,address))return false;
  ep->flags=(ep->flags&Halted) ? Busy|Halted : 0;return true;
}
void usbd_edpt_close(uint8_t port,uint8_t address) {
  auto *ep=core && port==core->port ? core->endpoint(address):nullptr;if(!ep || !(address&15))return;
#ifdef TUP_DCD_EDPT_CLOSE_API
  dcd_edpt_close(port,address);
#else
  if(!dcd_edpt_abort(port,address))return;
#endif
  ep->opened=false;ep->flags=0;
}
bool usbd_edpt_iso_alloc(uint8_t,uint8_t,uint16_t) {return false;}
bool usbd_edpt_iso_activate(uint8_t,const tusb_desc_endpoint_t *) {return false;}
bool usbd_edpt_xfer_fifo(uint8_t,uint8_t,tu_fifo_t *,uint16_t,bool) {return false;}
bool usbd_open_edpt_pair(uint8_t port,const uint8_t *descriptor,uint8_t count,uint8_t type,uint8_t *out,uint8_t *in) {
  for(unsigned i=0;i<count;++i,descriptor+=descriptor[0]) {
    if(descriptor[0]!=7 || descriptor[1]!=5 || (descriptor[3]&3)!=type || !usbd_edpt_open(port,reinterpret_cast<const tusb_desc_endpoint_t *>(descriptor)))return false;
    if(descriptor[2]&128)*in=descriptor[2];else *out=descriptor[2];
  }
  return true;
}
bool usbd_try_defer_func(osal_task_func_t function,void *context) {
  if(!core || !function || core->recovering || core->stopping)return false;
  Event event={};event.data.rhport=core->port;event.data.event_id=USBD_EVENT_FUNC_CALL;event.data.func_call.func=function;event.data.func_call.param=context;
  return core->post(event,false);
}
void usbd_defer_func(osal_task_func_t function,void *context,bool inIsr) {
  if(!core || !function)return;
  Event event={};event.data.rhport=core->port;event.data.event_id=USBD_EVENT_FUNC_CALL;event.data.func_call.func=function;event.data.func_call.param=context;
  if(!core->post(event,inIsr))core->overflow.store(true);
}
uint16_t usbd_event_queue_capacity() {return core ? core->queueDepth : 0;}
void usbd_sof_enable(uint8_t port,sof_consumer_t consumer,bool enabled) {
  if(!core || port!=core->port)return;uint8_t old=core->sofConsumers;
  if(enabled)core->sofConsumers|=1u<<consumer;else core->sofConsumers &= uint8_t(~(1u<<consumer));
  if(bool(old)!=bool(core->sofConsumers))dcd_sof_enable(port,core->sofConsumers!=0);
}
void tud_sof_cb_enable(bool enabled) {if(core)usbd_sof_enable(core->port,SOF_CONSUMER_USER,enabled);}
void dcd_event_handler(const dcd_event_t *data,bool inIsr) {
  auto *s=core;if(!s || !data || data->rhport!=s->port || s->stopping)return;
  if(data->event_id==DCD_EVENT_BUS_RESET || data->event_id==DCD_EVENT_UNPLUGGED) {
    s->configuration.store(0);s->connected.store(false);s->suspended.store(false);s->bus.fetch_add(1);
  }
  if(data->event_id==DCD_EVENT_SETUP_RECEIVED)s->setup.fetch_add(1);
  if(data->event_id==DCD_EVENT_SUSPEND || data->event_id==DCD_EVENT_RESUME) {
    if(!s->connected.load())return;s->suspended.store(data->event_id==DCD_EVENT_SUSPEND);
  }
  if(data->event_id==DCD_EVENT_UNPLUGGED){s->connected.store(false);s->configuration.store(0);s->suspended.store(false);}
  if(data->event_id==DCD_EVENT_SOF) {
    for(unsigned i=0;i<s->driverCount;++i)if(s->drivers[i].sof)s->drivers[i].sof(s->port,data->sof.frame_count);
    if(s->suspended.exchange(false)) {dcd_event_t resume={};resume.rhport=s->port;resume.event_id=DCD_EVENT_RESUME;dcd_event_handler(&resume,inIsr);}
    if(!(s->sofConsumers&1))return;
  }
  Event event={};event.data=*data;event.bus=s->bus.load();event.setup=s->setup.load();event.configurationEpoch=s->configurationEpoch.load();
  if(!s->post(event,inIsr))s->overflow.store(true);
  tud_event_hook_cb(data->rhport,data->event_id,inIsr);
}
// Optional integration hooks retained until the remaining DWC2 ABI is replaced.
__attribute__((weak)) void tud_event_hook_cb(uint8_t,uint32_t,bool) {}
__attribute__((weak)) void tud_sof_cb(uint32_t) {}
__attribute__((weak)) void tud_suspend_cb(bool) {}
__attribute__((weak)) void tud_resume_cb() {}
__attribute__((weak)) bool dcd_dcache_clean(const void *,uint32_t) {return true;}
__attribute__((weak)) bool dcd_dcache_invalidate(const void *,uint32_t) {return true;}
__attribute__((weak)) bool dcd_dcache_clean_invalidate(const void *,uint32_t) {return true;}
}
