#include "EspUsbVideoTransport.h"
#include "../EspUsbDevice.h"
#include "EspUsbDeviceAppDriver.h"
#include "EspUsbMemory.h"
#include "EspUsbTinyUsbRuntime.h"
#include "device/usbd.h"
#include "esp_memory_utils.h"
#include <algorithm>
#include <string.h>
namespace {
portMUX_TYPE videoLock = portMUX_INITIALIZER_UNLOCKED;
using espusb::internal::VideoGuard;
uint64_t nextFrameId = 1;
const usbd_class_driver_t &videoDriver();
uint16_t get16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t get32(const uint8_t *p) { return get16(p) | uint32_t(get16(p+2)) << 16; }
void put16(uint8_t *p,uint16_t n) { p[0]=uint8_t(n); p[1]=uint8_t(n>>8); }
void put32(uint8_t *p,uint32_t n) { put16(p,uint16_t(n)); put16(p+2,uint16_t(n>>16)); }
}
namespace espusb { namespace internal {
VideoGuard::VideoGuard() { portENTER_CRITICAL(&videoLock); }
VideoGuard::~VideoGuard() { portEXIT_CRITICAL(&videoLock); }
} }
struct EspUsbVideoState {
  static EspUsbVideoState *first;
  EspUsbVideoState *next = nullptr;
  EspUsbDeviceVideo &owner;
  uint8_t *packet = nullptr;
  bool owned = false, opened = false, committed = false, busy = false, zlp = false;
  bool draining = false, fenceQueued = false, resetPid = false, cancelRequested = false, pendingValid = false;
  bool retireKnown = true;
  EspUsbTransferStatus retireStatus = EspUsbTransferStatus::Cancelled;
  uint8_t port = 0, interface = 255, endpoint = 0, alternate = 0, fid = 0, error = 0, streamError = 0;
  uint16_t packetSize = 0, armed = 0, armedData = 0;
  uint32_t epoch = 0, fenceEpoch = 0;
  uint8_t probe[48] = {}, commit[48] = {}, scratch[48] = {}, pending[48] = {};
  const uint8_t *frame = nullptr;
  size_t length = 0, done = 0;
  uint64_t id = 0;
  espusb::internal::TinyUsbRuntimeJob job{pump,this};
  explicit EspUsbVideoState(EspUsbDeviceVideo &o) : owner(o) { defaults(probe); memcpy(commit,probe,48); }
  ~EspUsbVideoState() {
    espusb::internal::removeTinyUsbRuntimeJob(job);
    if (owned) espusb::internal::freeUsbMemory(packet);
  }
  bool canStream() const { return opened && committed && !draining && !owner.stopping_.load() && (owner.bulkStreaming() || alternate == 1); }
  uint32_t payloadLimit() const { return owner.bulkStreaming() ? uint32_t(owner.videoConfig_.payloadBytes) : std::min(uint32_t(owner.videoConfig_.payloadBytes),uint32_t(packetSize ? packetSize : owner.isochronousPacketSize(false))); }
  void defaults(uint8_t *p) const {
    memset(p,0,48); p[2]=p[3]=1; put32(p+4,10000000u/owner.frameRate_);
    put32(p+18,owner.maxFrameSize()); put32(p+22,payloadLimit()); put32(p+26,27000000);
    p[30]=3; p[31]=p[32]=p[33]=1;
  }
  bool reject(uint8_t code) { error=code; return false; }
  bool normalize(uint8_t *out,const uint8_t *in,bool final) {
    const uint32_t interval=get32(in+4), maximum=get32(in+18), payload=get32(in+22);
    if ((in[2] && in[2]!=1) || (in[3] && in[3]!=1)) return reject(4);
    const uint32_t nominal=10000000u/owner.frameRate_;
    if (final && ((interval && interval!=nominal) || (maximum && maximum!=owner.maxFrameSize()) || payload>payloadLimit())) return reject(4);
    if (payload && payload<3) return reject(4);
    defaults(out);
    if (payload) put32(out+22,std::min(payload,payloadLimit()));
    return true;
  }
  void finish(EspUsbTransferStatus status,bool known) {
    EspUsbTransferResult result;
    {
      VideoGuard guard;
      if (!frame) return;
      result.buffer=frame; result.requestedBytes=length; result.actualBytes=done;
      result.actualBytesKnown=known; result.status=status; result.id=id;
      frame=nullptr; length=done=0; id=0; cancelRequested=false;
      owner.inFlight_.store(false);
    }
    owner.handleFrameResult(result);
  }
  void reset(EspUsbTransferStatus reason) {
    bool known;
    { VideoGuard guard;
      ++epoch; opened=committed=draining=false; alternate=0; pendingValid=false; resetPid=false;
      known=!busy; busy=zlp=false; owner.commit_={}; error=streamError=0;
    }
    finish(reason,known);
    { VideoGuard guard; defaults(probe); memcpy(commit,probe,48); }
  }
  bool requestFence(EspUsbTransferStatus status,bool pid) {
    // Called on the USB worker with the lock held, before acknowledging a
    // reconfiguration. The fence excludes every queued old completion.
    retireKnown = draining ? retireKnown : !busy;
    retireStatus=status; resetPid|=pid; draining=true; ++epoch;
    if (opened && !usbd_edpt_abort(port,endpoint)) return false;
    return espusb::internal::postTinyUsbRuntimeJob(job);
  }
  static void fence(void *context) {
    auto *s=static_cast<EspUsbVideoState *>(context);
    EspUsbTransferStatus status; bool known;
    {
      VideoGuard guard; s->fenceQueued=false;
      if (s->owner.stopping_.load()) return;
      if (s->fenceEpoch!=s->epoch) { espusb::internal::postTinyUsbRuntimeJob(s->job); return; }
      if (s->resetPid && s->opened) usbd_edpt_clear_stall(s->port,s->endpoint);
      s->resetPid=false; s->busy=s->zlp=false; s->draining=false;
      status=s->retireStatus; known=s->retireKnown;
    }
    s->finish(status,known);
    espusb::internal::postTinyUsbRuntimeJob(s->job);
  }
  void arm() {
    if (!canStream() || !frame || busy) return;
    const size_t payload=get32(commit+22);
    armedData=uint16_t(std::min(length-done,payload-2)); armed=armedData+2;
    packet[0]=2; packet[1]=uint8_t(0x80|fid|(done+armedData==length ? 2 : 0));
    memcpy(packet+2,frame+done,armedData); busy=true; zlp=false;
    if (!usbd_edpt_xfer(port,endpoint,packet,armed,false)) {
      busy=false; streamError=3;
      requestFence(EspUsbTransferStatus::TransferError,false);
    }
  }
  static void pump(void *context) {
    auto *s=static_cast<EspUsbVideoState *>(context); VideoGuard guard;
    if (s->owner.stopping_.load()) return;
    if (s->cancelRequested && !s->draining) s->requestFence(EspUsbTransferStatus::Cancelled,false);
    if (s->draining) {
      if (s->fenceQueued) return;
      s->fenceEpoch=s->epoch; s->fenceQueued=usbd_try_defer_func(fence,s);
      if (!s->fenceQueued) espusb::internal::postTinyUsbRuntimeJob(s->job);
      return;
    }
    s->arm();
  }
  static void init() {}
  static void resetAll(uint8_t) {
    for (auto *s=first;s;s=s->next) { s->reset(EspUsbTransferStatus::BusReset); s->interface=255; }
  }
  static bool deinit() {
    for (auto *s=first;s;s=s->next) { s->reset(EspUsbTransferStatus::Stopped); s->interface=255; }
    return true;
  }
  static uint16_t open(uint8_t port,const tusb_desc_interface_t *itf,uint16_t available) {
    if (available<9 || itf->bLength!=9 || itf->bInterfaceClass!=14 || itf->bInterfaceSubClass!=1) return 0;
    auto *s=first;
    while (s && (s->owner.instance_!=itf->bInterfaceNumber || !s->owner.device_.config().startTinyUsb)) s=s->next;
    if (!s) return 0;
    const auto *data=reinterpret_cast<const uint8_t *>(itf); size_t offset=0; bool endpoint=false;
    while (offset+2<=available) {
      const auto *d=data+offset;
      if (d[0]<2 || d[0]>available-offset) return 0;
      if (offset && d[1]==11) break;
      if (d[1]==4 && (d[0]!=9 || (d[2]!=itf->bInterfaceNumber && d[2]!=itf->bInterfaceNumber+1))) break;
      if (d[1]==5) {
        const unsigned kind=s->owner.bulkStreaming() ? 2 : 1;
        if (d[0]!=7 || !(d[2]&0x80) || (d[3]&3)!=kind || endpoint) return 0;
        if (!usbd_edpt_open(port,reinterpret_cast<const tusb_desc_endpoint_t *>(d))) return 0;
        s->endpoint=d[2]; s->packetSize=get16(d+4); endpoint=true;
      }
      offset+=d[0];
    }
    if (!endpoint) return 0;
    VideoGuard guard; s->port=port; s->interface=itf->bInterfaceNumber; s->opened=true;
    s->defaults(s->probe); memcpy(s->commit,s->probe,48); return uint16_t(offset);
  }
  static bool control(uint8_t port,uint8_t stage,const tusb_control_request_t *r) {
    auto *s=first;
    while (s && r->wIndex!=s->interface && r->wIndex!=s->interface+1) s=s->next;
    if (!s || s->owner.stopping_.load()) return false;
    const bool streamingInterface=r->wIndex==s->interface+1;
    if (stage==CONTROL_STAGE_SETUP) s->pendingValid=false;
    if (r->bmRequestType==0x81 && r->bRequest==10 && !r->wValue && r->wLength==1) {
      if (stage!=CONTROL_STAGE_SETUP) return true;
      s->scratch[0]=streamingInterface ? s->alternate : 0; return tud_control_xfer(port,r,s->scratch,1);
    }
    if (r->bmRequestType==1 && r->bRequest==11 && !r->wLength) {
      const uint16_t max=streamingInterface && !s->owner.bulkStreaming() ? 1 : 0;
      if (r->wValue>max) return s->reject(4);
      if (stage!=CONTROL_STAGE_SETUP) return true;
      if (!streamingInterface) return tud_control_status(port,r);
      VideoGuard guard; s->alternate=uint8_t(r->wValue);
      return s->requestFence(EspUsbTransferStatus::Cancelled,s->owner.bulkStreaming()) && tud_control_status(port,r);
    }
    if (r->bmRequestType!=0xa1 && r->bmRequestType!=0x21) return false;
    if (r->wValue&255) return s->reject(6);
    const uint8_t selector=r->wValue>>8;
    if ((!streamingInterface && selector==2) || (streamingInterface && selector==6)) {
      if (r->bmRequestType!=0xa1 || !r->wLength) return s->reject(7);
      if (r->bRequest==0x81) {
        if (stage==CONTROL_STAGE_SETUP) { s->scratch[0]=streamingInterface ? s->streamError : s->error; return tud_control_xfer(port,r,s->scratch,1); }
        if (stage==CONTROL_STAGE_ACK) { if (streamingInterface) s->streamError=0; else s->error=0; }
        return true;
      }
      if (r->bRequest==0x86 && stage==CONTROL_STAGE_SETUP) { s->scratch[0]=1; return tud_control_xfer(port,r,s->scratch,1); }
      return stage!=CONTROL_STAGE_SETUP ? true : s->reject(7);
    }
    if (!streamingInterface || (selector!=1 && selector!=2)) return s->reject(6);
    if (r->bmRequestType==0xa1) {
      if (!r->wLength) return s->reject(7);
      if (stage!=CONTROL_STAGE_SETUP) return true;
      const uint8_t *source=nullptr; uint16_t bytes=48;
      switch (r->bRequest) {
      case 0x81: source=selector==1 ? s->probe : s->commit; break;
      case 0x82: case 0x83: case 0x87:
        s->defaults(s->scratch);
        if (r->bRequest==0x82) put32(s->scratch+22,3);
        source=s->scratch; break;
      case 0x84:
        memset(s->scratch,0,48); put32(s->scratch+22,1);
        source=s->scratch; break;
      case 0x85: put16(s->scratch,48); source=s->scratch; bytes=2; break;
      case 0x86: s->scratch[0]=3; source=s->scratch; bytes=1; break;
      default: return s->reject(7);
      }
      return tud_control_xfer(port,r,const_cast<uint8_t *>(source),bytes);
    }
    if (r->bRequest!=1 || (r->wLength!=26 && r->wLength!=34 && r->wLength!=48)) return s->reject(7);
    if (stage==CONTROL_STAGE_SETUP) { memset(s->scratch,0,48); return tud_control_xfer(port,r,s->scratch,r->wLength); }
    if (stage==CONTROL_STAGE_DATA) {
      if (usbd_control_received_bytes()!=r->wLength) return s->reject(7);
      s->pendingValid=s->normalize(s->pending,s->scratch,selector==2); return s->pendingValid;
    }
    if (stage==CONTROL_STAGE_ACK) {
      if (!s->pendingValid) return false;
      if (selector==1) { memcpy(s->probe,s->pending,48); s->pendingValid=false; return true; }
      EspUsbDeviceVideoCommit value;
      { VideoGuard guard;
        memcpy(s->commit,s->pending,48); s->pendingValid=false; s->committed=true;
        if (s->frame || s->busy) s->requestFence(EspUsbTransferStatus::Cancelled,false);
        value.formatIndex=s->commit[2]; value.frameIndex=s->commit[3]; value.frameInterval=get32(s->commit+4);
        value.maxVideoFrameSize=get32(s->commit+18); value.maxPayloadTransferSize=get32(s->commit+22);
      }
      s->owner.handleCommit(value);
    }
    return true;
  }
  static bool complete(uint8_t,uint8_t ep,xfer_result_t result,uint32_t bytes) {
    EspUsbVideoState *s; bool finished=false, known=true; EspUsbTransferStatus status=EspUsbTransferStatus::Success;
    {
      VideoGuard guard; s=first; while (s && ep!=s->endpoint) s=s->next;
      if (!s) return false;
      if (s->draining || !s->busy) return true;
      s->busy=false;
      if (!s->frame) return true;
      if (result!=XFER_RESULT_SUCCESS || bytes!=(s->zlp ? 0 : s->armed)) { status=EspUsbTransferStatus::TransferError; s->streamError=3; known=false; finished=true; }
      else {
        if (!s->zlp) s->done+=s->armedData;
        const bool needsZlp=!s->zlp && s->owner.bulkStreaming() && s->armed%s->packetSize==0 && s->armed<get32(s->commit+22);
        if (needsZlp) {
          s->zlp=true; s->busy=true;
          if (!usbd_edpt_xfer(s->port,s->endpoint,nullptr,0,false)) { s->busy=false; s->requestFence(EspUsbTransferStatus::TransferError,false); }
          return true;
        }
        s->zlp=false; finished=s->done==s->length;
        if (!finished && !s->owner.stopping_.load()) s->arm();
      }
    }
    if (finished) s->finish(status,known);
    return true;
  }
};
EspUsbVideoState *EspUsbVideoState::first=nullptr;
namespace {
const usbd_class_driver_t &videoDriver() {
  static const usbd_class_driver_t d={.name="EspVideo",.init=EspUsbVideoState::init,.deinit=EspUsbVideoState::deinit,
    .reset=EspUsbVideoState::resetAll,.open=EspUsbVideoState::open,.control_xfer_cb=EspUsbVideoState::control,
    .xfer_cb=EspUsbVideoState::complete,.sof=nullptr}; return d;
}
}
namespace espusb { namespace internal {
esp_err_t VideoTransport::begin(EspUsbDeviceVideo &o) {
  const auto &c=o.videoConfig_;
  const size_t physical=(c.payloadBytes+63)&~size_t(63);
  if (c.payloadBuffer && (c.payloadBufferBytes<physical || uintptr_t(c.payloadBuffer)%64 || uintptr_t(c.payloadBuffer)>UINTPTR_MAX-physical ||
      !esp_ptr_internal(c.payloadBuffer) || !esp_ptr_dma_capable(c.payloadBuffer) || !esp_ptr_internal(c.payloadBuffer+physical-1) || !esp_ptr_dma_capable(c.payloadBuffer+physical-1))) return ESP_ERR_INVALID_ARG;
  auto *s=newUsbObject<EspUsbVideoState>(o);
  if (s) { s->owned=!c.payloadBuffer; s->packet=c.payloadBuffer ? c.payloadBuffer : static_cast<uint8_t *>(allocateUsbMemory(c.payloadBytes,64,true)); }
  if (!s || !s->packet || (!EspUsbVideoState::first && !espUsbDeviceRegisterAppDrivers(&videoDriver(),1))) { deleteUsbObject(s); return ESP_ERR_NO_MEM; }
  VideoGuard guard; s->next=EspUsbVideoState::first; EspUsbVideoState::first=s; o.state_=s; return ESP_OK;
}
void VideoTransport::end(EspUsbDeviceVideo &o) {
  EspUsbVideoState *s;
  { VideoGuard guard; s=o.state_; o.state_=nullptr;
    auto **link=&EspUsbVideoState::first; while (*link && *link!=s) link=&(*link)->next;
    if (*link) *link=s->next;
  }
  deleteUsbObject(s); if (!EspUsbVideoState::first) espUsbDeviceUnregisterAppDrivers(&videoDriver(),1);
}
bool VideoTransport::streaming(const EspUsbDeviceVideo &o) { VideoGuard guard; return o.state_ && o.state_->canStream(); }
size_t VideoTransport::capacity(const EspUsbDeviceVideo &o) { VideoGuard guard; return o.state_ ? o.videoConfig_.payloadBytes : 0; }
esp_err_t VideoTransport::submit(EspUsbDeviceVideo &o,const void *data,size_t n,uint64_t &id) {
  id=0; VideoGuard guard; auto *s=o.state_;
  if (!s || !s->canStream()) return ESP_ERR_INVALID_STATE;
  if (!data || !n || n>o.maxFrameSize() || uintptr_t(data)>UINTPTR_MAX-n || (o.format_==EspUsbDeviceVideoFormat::Yuy2 && n!=o.maxFrameSize())) return ESP_ERR_INVALID_ARG;
  if (s->frame) return ESP_ERR_INVALID_STATE;
  if (!nextFrameId) return ESP_ERR_INVALID_STATE;
  s->frame=static_cast<const uint8_t *>(data); s->length=n; s->done=0; s->fid^=1; s->id=nextFrameId++;
  o.inFlight_.store(true);
  if (!postTinyUsbRuntimeJob(s->job)) { s->frame=nullptr; o.inFlight_.store(false); return ESP_ERR_INVALID_STATE; }
  id=s->id; return ESP_OK;
}
esp_err_t VideoTransport::cancel(EspUsbDeviceVideo &o,uint64_t id) {
  VideoGuard guard; auto *s=o.state_;
  if (!s || !id || s->id!=id || !s->frame) return ESP_ERR_NOT_FOUND;
  if (o.stopping_.load()) return ESP_ERR_INVALID_STATE;
  s->cancelRequested=true; return postTinyUsbRuntimeJob(s->job) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
} }
