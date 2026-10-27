#include "internal/EspUsbDeviceEndpoint.h"
#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include "internal/EspUsbNetLock.h"
#include "internal/EspUsbDeviceBus.h"
#include "esp_memory_utils.h"
#include "esp_mac.h"
#include <algorithm>
#include <string.h>
#include <utility>
namespace {
portMUX_TYPE netLock = portMUX_INITIALIZER_UNLOCKED;
using espusb::internal::NetGuard;
const espusb::internal::UsbClassDriver &netDriver();
uint16_t get16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t get32(const uint8_t *p) { return get16(p) | uint32_t(get16(p + 2)) << 16; }
void put16(uint8_t *p, uint16_t n) { p[0] = uint8_t(n); p[1] = uint8_t(n >> 8); }
void put32(uint8_t *p, uint32_t n) { put16(p,uint16_t(n)); put16(p+2,uint16_t(n>>16)); }
size_t aligned(size_t n, size_t a) { return (n + a - 1) & ~(a - 1); }
struct NetSlot {
  enum Phase { Free, Queued, Hardware, Processing } phase = Free;
  uint8_t *data = nullptr;
  NetSlot *next = nullptr;
  uint16_t used = 0, count = 0;
};
}
namespace espusb { namespace internal {
NetGuard::NetGuard() { portENTER_CRITICAL(&netLock); }
NetGuard::~NetGuard() { portEXIT_CRITICAL(&netLock); }
} }
struct EspUsbNetState {
  static EspUsbNetState *first;
  EspUsbNetState *next = nullptr;
  EspUsbDeviceNet &owner;
  NetSlot *slots = nullptr, *head = nullptr, *tail = nullptr, *tx = nullptr, *rx = nullptr;
  uint8_t *txStorage = nullptr, *rxStorage = nullptr, *notification = nullptr;
  size_t txStride = 0, rxStride = 0;
  bool ownTx = false, ownRx = false, active = false, draining = false, fenceQueued = false, zlp = false, notifyBusy = false, zlpPending = false;
  bool resetDataPid = false, resetNotifyPid = false;
  std::atomic<bool> stopping{false};
  uint8_t port = 0, controlInterface = 255, in = 0, out = 0, notify = 0, desired = 0, notificationStage = 2;
  uint16_t packet = 64, rxArm = 0, maxIn = 0, datagrams = 0, sequence = 0;
  uint8_t control[28] = {};
  uint32_t epoch = 0, fenceEpoch = 0;
  std::atomic<uint32_t> txFrames{0}, rxFrames{0}, invalidNtbs{0};
  espusb::internal::TinyUsbRuntimeJob job{pump, this};
  explicit EspUsbNetState(EspUsbDeviceNet &o) : owner(o) {}
  ~EspUsbNetState() {
    espusb::internal::removeTinyUsbRuntimeJob(job);
    espusb::internal::freeUsbMemory(slots);
    if (ownTx) espusb::internal::freeUsbMemory(txStorage);
    if (ownRx) espusb::internal::freeUsbMemory(rxStorage);
    espusb::internal::freeUsbMemory(notification);
  }
  bool allocate() {
    const auto &c = owner.netConfig_;
    const size_t count = c.txBuffers + c.rxBuffers;
    if (count) {
      slots = static_cast<NetSlot *>(espusb::internal::allocateUsbMemory(count * sizeof(NetSlot)));
      if (!slots) return false;
      for (size_t i = 0; i < count; ++i) new (slots + i) NetSlot;
    }
    const auto make = [](uint8_t *external, size_t bytes, bool &owned) {
      owned = !external && bytes;
      return external ? external : bytes ? static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(bytes,64,true)) : nullptr;
    };
    txStorage = make(c.txBuffer, txStride * c.txBuffers, ownTx);
    rxStorage = make(c.rxBuffer, rxStride * c.rxBuffers, ownRx);
    if ((c.txBuffers && !txStorage) || (c.rxBuffers && !rxStorage)) return false;
    for (size_t i = 0; i < c.txBuffers; ++i) slots[i].data = txStorage + i * txStride;
    for (size_t i = 0; i < c.rxBuffers; ++i) slots[c.txBuffers+i].data = rxStorage + i * rxStride;
    notification = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(16,64,true));
    return notification;
  }
  void limits(uint16_t bytes, uint16_t requested) {
    maxIn = bytes;
    const size_t fit = (size_t(bytes) - owner.netConfig_.maxFrameBytes - 24) / 4;
    datagrams = uint16_t(std::min({owner.netConfig_.maxDatagrams, fit, requested ? size_t(requested) : SIZE_MAX}));
  }
  void clear() {
    const auto &c = owner.netConfig_;
    for (size_t i = 0; i < c.txBuffers + c.rxBuffers; ++i) slots[i].phase = NetSlot::Free;
    head = tail = tx = rx = nullptr; zlp = zlpPending = false;
  }
  void reset() {
    ++epoch; active = draining = false; desired = 0; notificationStage = 2;
    clear(); limits(uint16_t(owner.netConfig_.ntbInBytes), 0);
    sequence = 0; notifyBusy = resetDataPid = resetNotifyPid = false;
  }
  void armRx() {
    if (stopping.load() || !active || draining || rx) return;
    const auto &c = owner.netConfig_;
    for (size_t i = 0; i < c.rxBuffers; ++i) {
      auto *s = &slots[c.txBuffers + i];
      if (s->phase != NetSlot::Free) continue;
      s->phase = NetSlot::Hardware; rx = s;
      if (!espusb_device_edpt_xfer(port,out,s->data,rxArm,false)) { s->phase = NetSlot::Free; rx = nullptr; espusb::internal::postTinyUsbRuntimeJob(job); }
      return;
    }
  }
  void armTx() {
    if (stopping.load() || !active || draining || tx || !head) return;
    tx = head; head = head->next; if (!head) tail = nullptr;
    tx->phase = NetSlot::Hardware; put16(tx->data + 6,sequence++);
    if (!espusb_device_edpt_xfer(port,in,tx->data,tx->used,false)) {
      tx->phase = NetSlot::Queued; tx->next = head; head = tx;
      if (!tail) tail = tx; tx = nullptr;
      espusb::internal::postTinyUsbRuntimeJob(job);
    }
  }
  void armNotification() {
    if (stopping.load() || draining || notifyBusy || notificationStage >= 2) return;
    memset(notification,0,16); notification[0] = 0xa1;
    put16(notification+4,controlInterface);
    const uint16_t bytes = notificationStage == 0 ? 16 : 8;
    if (notificationStage == 0) {
      notification[1] = 0x2a; put16(notification+6,8);
      const uint32_t speed = espusb_device_speed() == ESPUSB_SPEED_HIGH ? 480000000 : 12000000;
      put32(notification+8,speed); put32(notification+12,speed);
    } else { notification[1] = 0; put16(notification+2,active ? 1 : 0); }
    notifyBusy = espusb_device_edpt_xfer(port,notify,notification,bytes,false);
    if (!notifyBusy) espusb::internal::postTinyUsbRuntimeJob(job);
  }
  bool change(uint8_t alternate, bool resetData = false, bool resetNotify = false) {
    resetDataPid |= resetData; resetNotifyPid |= resetNotify;
    active = false; desired = alternate; draining = true; ++epoch;
    if (!espusb_device_edpt_abort(port,in) || !espusb_device_edpt_abort(port,out) || !espusb_device_edpt_abort(port,notify)) return false;
    return espusb::internal::postTinyUsbRuntimeJob(job);
  }
  static void fence(void *context) {
    auto *s = static_cast<EspUsbNetState *>(context); NetGuard guard;
    s->fenceQueued = false;
    if (s->stopping.load()) return;
    if (s->epoch != s->fenceEpoch) { espusb::internal::postTinyUsbRuntimeJob(s->job); return; }
    s->clear(); s->notifyBusy = false;
    if (s->resetDataPid) { espusb_device_edpt_clear_stall(s->port,s->in); espusb_device_edpt_clear_stall(s->port,s->out); }
    if (s->resetNotifyPid) espusb_device_edpt_clear_stall(s->port,s->notify);
    s->resetDataPid = s->resetNotifyPid = false; s->draining = false; s->active = s->desired == 1;
    s->notificationStage = s->active ? 0 : 1;
    s->armNotification(); s->armRx(); s->armTx();
  }
  static void pump(void *context) {
    auto *s = static_cast<EspUsbNetState *>(context); NetGuard guard;
    if (s->stopping.load()) return;
    if (s->draining) {
      if (s->fenceQueued) return;
      s->fenceEpoch = s->epoch;
      s->fenceQueued = espusb_device_try_defer(fence,s);
      if (!s->fenceQueued) espusb::internal::postTinyUsbRuntimeJob(s->job);
      return;
    }
    if (s->zlpPending) {
      s->zlpPending = !espusb_device_edpt_xfer(s->port,s->in,nullptr,0,false);
      if (s->zlpPending) espusb::internal::postTinyUsbRuntimeJob(s->job);
    }
    s->armTx(); s->armRx(); s->armNotification();
  }
  bool enqueue(const uint8_t *data, size_t bytes) {
    if (!data || bytes < 14 || bytes > owner.netConfig_.maxFrameBytes || stopping.load() || !active || draining) return false;
    NetSlot *s = tail;
    if (!s || s->count == datagrams || aligned(s->used,4) + bytes > maxIn) {
      s = nullptr;
      for (size_t i = 0; i < owner.netConfig_.txBuffers; ++i) if (slots[i].phase == NetSlot::Free) { s = &slots[i]; break; }
      if (!s) return false;
      s->phase = NetSlot::Queued; s->next = nullptr; s->count = 0;
      s->used = uint16_t(24 + 4 * datagrams);
      memset(s->data,0,s->used); put32(s->data,0x484d434e); put16(s->data+4,12); put16(s->data+10,12);
      put32(s->data+12,0x304d434e); put16(s->data+16,uint16_t(12+4*datagrams));
      if (tail) tail->next = s; else head = s; tail = s;
    }
    const size_t offset = aligned(s->used,4);
    memset(s->data+s->used,0,offset-s->used);
    memcpy(s->data+offset,data,bytes);
    put16(s->data+20+4*s->count,uint16_t(offset)); put16(s->data+22+4*s->count,uint16_t(bytes));
    ++s->count; s->used = uint16_t(offset+bytes); put16(s->data+8,s->used);
    espusb::internal::postTinyUsbRuntimeJob(job); return true;
  }
  bool parse(const uint8_t *data, size_t bytes) {
    const auto &c = owner.netConfig_;
    if (bytes < 12 || get32(data) != 0x484d434e || get16(data+4) != 12) return false;
    const size_t length = get16(data+8) ? get16(data+8) : bytes;
    if (length < 12 || length > bytes || length > c.ntbOutBytes) return false;
    const size_t index = get16(data+10);
    if (index < 12 || index % 4 || index > length || length-index < 12) return false;
    const uint8_t *ndp = data+index;
    const size_t table = get16(ndp+4);
    if (get32(ndp) != 0x304d434e || get16(ndp+6) || table < 12 || table % 4 || table > length-index) return false;
    const size_t entries = (table-8)/4;
    if (entries-1 > c.maxDatagrams || get32(ndp+table-4)) return false;
    size_t count = 0; bool ended = false;
    for (size_t i = 0; i < entries; ++i) {
      const size_t start = get16(ndp+8+4*i), n = get16(ndp+10+4*i);
      if (!start && !n) { ended = true; continue; }
      if (ended || !start || n < 14 || n > c.maxFrameBytes || start < 12 || start % 4 || start > length || n > length-start || (start < index+table && start+n > index)) return false;
      for (size_t j = 0; j < i; ++j) {
        const size_t other = get16(ndp+8+4*j), size = get16(ndp+10+4*j);
        if (start < other+size && other < start+n) return false;
      }
      ++count;
    }
    for (size_t i = 0; i < count; ++i) { owner.handleRecv(data+get16(ndp+8+4*i),get16(ndp+10+4*i)); ++rxFrames; }
    return true;
  }
  static void init() {}
  static void resetAll(uint8_t) { NetGuard guard; for (auto *s = first; s; s = s->next) { s->reset(); s->controlInterface = 255; } }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t port, const EspUsbInterfaceDescriptor *itf, uint16_t available) {
    if (available < 77 || itf->bLength != 9 || itf->bInterfaceClass != 2 || itf->bInterfaceSubClass != 0x0d) return 0;
    auto *s = first;
    while (s && (s->owner.interface_ != itf->bInterfaceNumber || !s->owner.device_.config().startTinyUsb)) s = s->next;
    if (!s) return 0;
    const auto *data = reinterpret_cast<const uint8_t *>(itf); size_t offset = 0;
    while (offset+2 <= available) {
      const uint8_t *d = data+offset;
      if (d[0] < 2 || d[0] > available-offset) return 0;
      if (offset && d[1] == 11) break;
      if (d[1] == 4 && (d[0] != 9 || (d[2] != itf->bInterfaceNumber && d[2] != itf->bInterfaceNumber+1))) break;
      if (d[1] == 5) {
        if (d[0] != 7) return 0;
        const uint8_t address = d[2];
        if ((d[3]&3) == 3 && (address&0x80)) s->notify = address;
        else if ((d[3]&3) == 2) { if (address&0x80) s->in = address; else s->out = address; s->packet = get16(d+4); }
        else return 0;
        if (!espusb_device_edpt_open(port,reinterpret_cast<const EspUsbEndpointDescriptor *>(d))) return 0;
      }
      offset += d[0];
    }
    if (!s->in || !s->out || !s->notify) return 0;
    s->port = port; s->controlInterface = itf->bInterfaceNumber;
    s->rxArm = uint16_t(aligned(s->owner.netConfig_.ntbOutBytes,s->packet));
    return uint16_t(offset);
  }
  static bool controlRequest(uint8_t port, uint8_t stage, const EspUsbControlRequest *r) {
    auto *s = first;
    while (s && r->wIndex != s->controlInterface && r->wIndex != s->controlInterface+1) s = s->next;
    if (!s || s->stopping.load()) return false;
    if (r->bmRequestType == 0x81 && r->bRequest == 10 && !r->wValue && r->wLength == 1) {
      if (stage != ESPUSB_CONTROL_STAGE_SETUP) return true;
      s->control[0] = r->wIndex == s->controlInterface ? 0 : s->desired;
      return espusb_device_control_transfer(port,r,s->control,1);
    }
    if (r->bmRequestType == 1 && r->bRequest == 11 && r->wValue <= 1 && !r->wLength) {
      if (stage != ESPUSB_CONTROL_STAGE_SETUP) return true;
      NetGuard guard;
      if (r->wIndex == s->controlInterface) return !r->wValue && s->change(s->desired,false,true) && espusb_device_control_status(port,r);
      return s->change(uint8_t(r->wValue),true) && espusb_device_control_status(port,r);
    }
    if (r->wIndex != s->controlInterface || (r->bmRequestType != 0xa1 && r->bmRequestType != 0x21)) return false;
    if (r->bmRequestType == 0xa1 && !r->wValue && r->wLength) {
      if (stage != ESPUSB_CONTROL_STAGE_SETUP) return true;
      NetGuard guard; memset(s->control,0,sizeof(s->control)); uint16_t n = 0;
      switch (r->bRequest) {
      case 0x80:
        n = 28; put16(s->control,28); put16(s->control+2,1);
        put32(s->control+4,s->owner.netConfig_.ntbInBytes);
        put16(s->control+8,4); put16(s->control+12,4);
        put32(s->control+16,s->owner.netConfig_.ntbOutBytes);
        put16(s->control+20,4); put16(s->control+24,4); put16(s->control+26,uint16_t(s->owner.netConfig_.maxDatagrams)); break;
      case 0x85: n = r->wLength >= 8 ? 8 : 4; put32(s->control,s->maxIn); put16(s->control+4,s->datagrams); break;
      case 0x87: n = 2; put16(s->control,s->owner.netConfig_.maxFrameBytes); break;
      case 0x83: case 0x89: n = 2; break;
      default: return false;
      }
      return espusb_device_control_transfer(port,r,s->control,n);
    }
    if (r->bmRequestType != 0x21) return false;
    if (!r->wLength && (r->bRequest == 0x43 || ((r->bRequest == 0x84 || r->bRequest == 0x8a) && !r->wValue))) {
      if (r->bRequest == 0x43 && (r->wValue&~0x1f)) return false;
      if (stage != ESPUSB_CONTROL_STAGE_SETUP) return true;
      return espusb_device_control_status(port,r);
    }
    if (r->bRequest != 0x86 || r->wValue || (r->wLength != 4 && r->wLength != 8)) return false;
    if (stage == ESPUSB_CONTROL_STAGE_SETUP) { memset(s->control,0,8); return espusb_device_control_transfer(port,r,s->control,r->wLength); }
    if (stage == ESPUSB_CONTROL_STAGE_DATA) {
      const uint32_t size = get32(s->control);
      if (espusb_device_control_received_bytes() != r->wLength || size < 2048 || size > s->owner.netConfig_.ntbInBytes || size < uint32_t(s->owner.netConfig_.maxFrameBytes)+28) return false;
      NetGuard guard; s->limits(uint16_t(size),r->wLength == 8 ? get16(s->control+4) : 0);
      if (s->desired) return s->change(s->desired);
    }
    return true;
  }
  static bool complete(uint8_t, uint8_t ep, EspUsbControllerResult result, uint32_t bytes) {
    EspUsbNetState *s; NetSlot *received = nullptr;
    {
      NetGuard guard; s = first;
      while (s && ep != s->in && ep != s->out && ep != s->notify) s = s->next;
      if (!s) return false;
      if (s->stopping.load() || s->draining) return true;
      if (ep == s->notify) { s->notifyBusy = false; if (s->notificationStage < 2) { ++s->notificationStage; s->armNotification(); } return true; }
      if (!s->active) return true;
      if (ep == s->in) {
        if (!s->tx) return true;
        if (!s->zlp && result == ESPUSB_TRANSFER_SUCCESS && s->tx->used % s->packet == 0 && s->tx->used < s->maxIn) {
          s->zlp = true;
          s->zlpPending = !espusb_device_edpt_xfer(s->port,s->in,nullptr,0,false);
          if (s->zlpPending) espusb::internal::postTinyUsbRuntimeJob(s->job);
          return true;
        }
        if (result == ESPUSB_TRANSFER_SUCCESS) s->txFrames += s->tx->count;
        s->tx->phase = NetSlot::Free; s->tx = nullptr; s->zlp = false; s->armTx(); return true;
      }
      received = s->rx;
      if (!received) return true;
      received->phase = NetSlot::Processing; s->rx = nullptr; s->armRx();
    }
    if (result != ESPUSB_TRANSFER_SUCCESS || bytes > s->rxArm || !s->parse(received->data,bytes)) ++s->invalidNtbs;
    { NetGuard guard; received->phase = NetSlot::Free; s->armRx(); }
    return true;
  }
};
EspUsbNetState *EspUsbNetState::first = nullptr;
namespace {
const espusb::internal::UsbClassDriver &netDriver() {
  static const espusb::internal::UsbClassDriver driver = {
    .init = EspUsbNetState::init, .deinit = EspUsbNetState::deinit,
    .reset = EspUsbNetState::resetAll, .open = EspUsbNetState::open,
    .control_xfer_cb = EspUsbNetState::controlRequest, .xfer_cb = EspUsbNetState::complete, .sof = nullptr
  }; return driver;
}
}
EspUsbDeviceNet::EspUsbDeviceNet(EspUsbDevice &d) : EspUsbDeviceClass(d) {
  const uint8_t fallback[] = {2,2,0x84,0x6a,0x96,0}; memcpy(mac_,fallback,6);
  esp_read_mac(mac_,ESP_MAC_ETH); mac_[0] = (mac_[0] | 2) & ~1;
  defaultMacSuffix_ = uint16_t(mac_[4]) << 8 | mac_[5]; refreshMacString();
}
EspUsbDeviceNet::~EspUsbDeviceNet() { end(); }
void EspUsbDeviceNet::refreshMacString() {
  static const char hex[] = "0123456789ABCDEF";
  for (unsigned i = 0; i < 6; ++i) { macString_[2*i] = hex[mac_[i]>>4]; macString_[2*i+1] = hex[mac_[i]&15]; }
  macString_[12] = 0;
}
void EspUsbDeviceNet::assignFunctionIds(uint8_t instance, uint8_t nameIndex) {
  nameIndex_ = nameIndex;
  if (!macUserSet_) { const uint16_t n = defaultMacSuffix_ + 2*instance; mac_[4] = uint8_t(n>>8); mac_[5] = uint8_t(n); refreshMacString(); }
}
esp_err_t EspUsbDeviceNet::configure(const EspUsbNetConfig &c) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if (c.ntbInBytes < 2048 || c.ntbInBytes > 65535 || c.ntbOutBytes < 2048 || c.ntbOutBytes > 65535 ||
      c.maxFrameBytes < 14 || size_t(c.maxFrameBytes)+28 > std::min(c.ntbInBytes,c.ntbOutBytes) ||
      !c.maxDatagrams || c.maxDatagrams > (65535-24)/4 ||
      c.txBuffers > SIZE_MAX/sizeof(NetSlot) || c.rxBuffers > SIZE_MAX/sizeof(NetSlot) - c.txBuffers) return ESP_ERR_INVALID_SIZE;
  if ((c.txBuffer && (!c.txBuffers || !c.txBufferBytes)) || (c.rxBuffer && (!c.rxBuffers || !c.rxBufferBytes))) return ESP_ERR_INVALID_ARG;
  netConfig_ = c; return ESP_OK;
}
bool EspUsbDeviceNet::begin() {
  if (state_ || !registered_ || espusb::internal::tinyUsbRuntimeStarted()) { startError_ = ESP_ERR_INVALID_STATE; return false; }
  startError_ = configure(netConfig_); if (startError_ != ESP_OK) return false;
  bool high = device_.config().controller == EspUsbController::HighSpeed;
#if CONFIG_IDF_TARGET_ESP32P4
  high |= device_.config().controller == EspUsbController::Auto;
#endif
  const size_t packet = high ? 512 : 64;
  const size_t txStride = aligned(netConfig_.ntbInBytes,64), rxStride = aligned(netConfig_.ntbOutBytes,packet);
  if (rxStride > 65535 || netConfig_.txBuffers > SIZE_MAX/txStride || netConfig_.rxBuffers > SIZE_MAX/rxStride) { startError_ = ESP_ERR_INVALID_SIZE; return false; }
  const auto valid = [](uint8_t *p, size_t supplied, size_t needed) {
    return !p || (supplied >= needed && uintptr_t(p)%64 == 0 && uintptr_t(p) <= UINTPTR_MAX-needed && esp_ptr_internal(p) && esp_ptr_dma_capable(p) && esp_ptr_internal(p+needed-1) && esp_ptr_dma_capable(p+needed-1));
  };
  const size_t txBytes = txStride*netConfig_.txBuffers, rxBytes = rxStride*netConfig_.rxBuffers;
  const uintptr_t t = uintptr_t(netConfig_.txBuffer), r = uintptr_t(netConfig_.rxBuffer);
  if (!valid(netConfig_.txBuffer,netConfig_.txBufferBytes,txBytes) || !valid(netConfig_.rxBuffer,netConfig_.rxBufferBytes,rxBytes) ||
      (t && r && t < r+rxBytes && r < t+txBytes)) { startError_ = ESP_ERR_INVALID_ARG; return false; }
  auto *s = espusb::internal::newUsbObject<EspUsbNetState>(*this);
  if (s) { s->txStride = txStride; s->rxStride = rxStride; }
  if (!s || !s->allocate() || (!EspUsbNetState::first && !espUsbDeviceRegisterAppDrivers(&netDriver(),1))) {
    espusb::internal::deleteUsbObject(s); startError_ = ESP_ERR_NO_MEM; return false;
  }
  s->reset(); s->next = EspUsbNetState::first; EspUsbNetState::first = s; state_ = s;
  startError_ = ESP_OK; return true;
}
void EspUsbDeviceNet::prepareToStop() { NetGuard guard; if (state_) state_->stopping.store(true); }
void EspUsbDeviceNet::end() {
  if (state_ && device_.tinyusbStarted_) { device_.end(); return; }
  endNetwork();
  if (!state_) return;
  EspUsbNetState *s;
  { NetGuard guard;
    s = state_; state_ = nullptr;
    auto **link = &EspUsbNetState::first; while (*link && *link != s) link = &(*link)->next;
    if (*link) *link = s->next;
  }
  espusb::internal::deleteUsbObject(s);
  if (!EspUsbNetState::first) espUsbDeviceUnregisterAppDrivers(&netDriver(),1);
}
uint16_t EspUsbDeviceNet::configurationDescriptor(uint8_t *dst, uint8_t itf, uint8_t ep, uint16_t packet) {
  if (state_) { device_.setLastError(ESP_ERR_INVALID_STATE); return 0; }
  if (!ep || ep > 14) return 0;
  interface_ = itf;
  const uint16_t m = netConfig_.maxFrameBytes;
  const uint8_t bytes[] = {
    8,11,itf,2,2,0x0d,0,nameIndex_, 9,4,itf,0,1,2,0x0d,0,nameIndex_,
    5,0x24,0,0x10,1, 5,0x24,6,itf,uint8_t(itf+1),
    13,0x24,0x0f,macIndex_,0,0,0,0,uint8_t(m),uint8_t(m>>8),0,0,0,
    6,0x24,0x1a,0,1,0x20,
    7,5,uint8_t(0x80|ep),3,16,0,uint8_t(packet>64 ? 8 : 16),
    9,4,uint8_t(itf+1),0,0,10,0,1,0, 9,4,uint8_t(itf+1),1,2,10,0,1,0,
    7,5,uint8_t(0x80|(ep+1)),2,uint8_t(packet),uint8_t(packet>>8),0,
    7,5,uint8_t(ep+1),2,uint8_t(packet),uint8_t(packet>>8),0
  };
  if (dst) memcpy(dst,bytes,sizeof(bytes)); return sizeof(bytes);
}
esp_err_t EspUsbDeviceNet::macAddress(const uint8_t mac[6]) {
  if (state_) return ESP_ERR_INVALID_STATE;
  if (!mac || mac[0]&1 || !(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5])) return ESP_ERR_INVALID_ARG;
  memcpy(mac_,mac,6); macUserSet_ = true; refreshMacString(); return ESP_OK;
}
const uint8_t *EspUsbDeviceNet::macAddress() const { return mac_; }
esp_err_t EspUsbDeviceNet::onFrame(FrameCallback c) { if (state_) return ESP_ERR_INVALID_STATE; frameCallback_ = std::move(c); return ESP_OK; }
bool EspUsbDeviceNet::linkUp() const { NetGuard guard; return state_ && state_->active && !state_->stopping.load() && !state_->draining; }
bool EspUsbDeviceNet::sendFrame(const uint8_t *data, size_t n) { NetGuard guard; return state_ && state_->enqueue(data,n); }
size_t EspUsbDeviceNet::ntbInCapacity() const { NetGuard guard; return state_ && netConfig_.txBuffers ? netConfig_.ntbInBytes : 0; }
size_t EspUsbDeviceNet::ntbOutCapacity() const { NetGuard guard; return state_ && netConfig_.rxBuffers ? netConfig_.ntbOutBytes : 0; }
size_t EspUsbDeviceNet::txBufferCount() const { NetGuard guard; return state_ ? netConfig_.txBuffers : 0; }
size_t EspUsbDeviceNet::rxBufferCount() const { NetGuard guard; return state_ ? netConfig_.rxBuffers : 0; }
EspUsbNetStats EspUsbDeviceNet::stats() const {
  NetGuard guard; EspUsbNetStats result; if (state_) { result.txFrames = state_->txFrames.load(); result.rxFrames = state_->rxFrames.load(); result.invalidNtbs = state_->invalidNtbs.load(); } return result;
}
void EspUsbDeviceNet::handleRecv(const uint8_t *data, uint16_t bytes) {
  void (*receive)(void *,const uint8_t *,size_t); void *context;
  { NetGuard guard; receive = networkReceive_; context = networkContext_; if (receive) ++networkUsers_; }
  if (receive) { receive(context,data,bytes); --networkUsers_; }
  else if (frameCallback_) frameCallback_(data,bytes);
}
bool EspUsbDeviceNet::endNetwork() {
  if (espusb::internal::tinyUsbRuntimeInTask()) return false;
  void (*stop)(EspUsbDeviceNet &);
  { NetGuard guard; networkReceive_ = nullptr; networkContext_ = nullptr; stop = networkStop_; networkStop_ = nullptr; }
  while (networkUsers_.load()) vTaskDelay(1);
  if (stop) stop(*this); return true;
}
esp_err_t EspUsbDeviceNet::ipConfig(IPAddress ip, IPAddress gw, IPAddress mask) { if (netStarted_) return ESP_ERR_INVALID_STATE; cfgIp_=uint32_t(ip); cfgGateway_=uint32_t(gw); cfgNetmask_=uint32_t(mask); return ESP_OK; }
esp_err_t EspUsbDeviceNet::dhcpServer(bool enable) { if (netStarted_) return ESP_ERR_INVALID_STATE; dhcpServer_=enable; if (enable) dhcpClient_=false; return ESP_OK; }
esp_err_t EspUsbDeviceNet::dhcpClient(bool enable) { if (netStarted_) return ESP_ERR_INVALID_STATE; dhcpClient_=enable; if (enable) dhcpServer_=false; return ESP_OK; }
esp_err_t EspUsbDeviceNet::dhcpAdvertiseGateway(bool enable) { if (netStarted_) return ESP_ERR_INVALID_STATE; dhcpAdvertiseGateway_=enable; return ESP_OK; }
esp_err_t EspUsbDeviceNet::dhcpDns(IPAddress dns) { if (netStarted_) return ESP_ERR_INVALID_STATE; dhcpDns_=uint32_t(dns); return ESP_OK; }
esp_err_t EspUsbDeviceNet::defaultRoute(bool enable) { if (netStarted_) return ESP_ERR_INVALID_STATE; defaultRoute_=enable; return ESP_OK; }
