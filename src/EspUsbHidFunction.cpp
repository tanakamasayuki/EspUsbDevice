#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include "device/usbd.h"
#include <algorithm>
#include <string.h>

namespace {
portMUX_TYPE hidLock = portMUX_INITIALIZER_UNLOCKED;
struct Global { uint32_t size = 0, count = 0; uint8_t id = 0; };
struct Report {
  uint32_t bits[3] = {};
  uint8_t id = 0, idle = 0;
  size_t bytes(unsigned type) const { return (bits[type] + 7) / 8 + (id && bits[type] ? 1 : 0); }
};
struct Item { uint8_t tag; uint32_t value; };
bool item(const uint8_t *data, size_t size, size_t &offset, Item &out) {
  if (offset >= size) return false;
  const uint8_t prefix = data[offset++];
  if (prefix == 0xfe) {
    if (size - offset < 2 || data[offset] > size - offset - 2) return false;
    offset += 2 + data[offset]; out = {0xfe, 0}; return true;
  }
  const size_t length = (prefix & 3) == 3 ? 4 : prefix & 3;
  if (length > size - offset) return false;
  out = {uint8_t(prefix & 0xfc), 0};
  for (size_t i = 0; i < length; ++i) out.value |= uint32_t(data[offset++]) << (8 * i);
  return true;
}
template <typename Consume>
bool walk(const uint8_t *data, size_t bytes, Global *stack, size_t depthLimit, Consume consume) {
  Global current; size_t depth = 0;
  for (size_t offset = 0; offset < bytes;) {
    Item next; if (!item(data, bytes, offset, next)) return false;
    switch (next.tag) {
    case 0x74: current.size = next.value; break;
    case 0x94: current.count = next.value; break;
    case 0x84: if (!next.value || next.value > 255) return false; current.id = uint8_t(next.value); break;
    case 0xa4: if (depth == depthLimit) return false; stack[depth++] = current; break;
    case 0xb4: if (!depth) return false; current = stack[--depth]; break;
    case 0x80: case 0x90: case 0xb0: {
      const uint64_t bits = uint64_t(current.size) * current.count;
      if (!bits || bits > uint64_t(UINT16_MAX - (current.id ? 1 : 0)) * 8) return false;
      const unsigned type = next.tag == 0x80 ? 0 : next.tag == 0x90 ? 1 : 2;
      if (!consume(current.id, type, uint32_t(bits))) return false;
      break;
    }
    default: break;
    }
  }
  return !depth;
}
extern const usbd_class_driver_t hidDriver;
}

struct EspUsbHidState {
  static EspUsbHidState *first;
  EspUsbHidState *next = nullptr;
  EspUsbDevice &device;
  unsigned references = 0;
  Report *reports = nullptr;
  size_t reportCount = 0, capacity[3] = {};
  uint8_t *input = nullptr, *output = nullptr, *packet = nullptr, *control = nullptr;
  size_t assembled = 0;
  const uint8_t *hidDescriptor = nullptr;
  uint8_t rhport = 0, interface = 0xff, in = 0, out = 0, protocol = 1, response = 0, boot = 0;
  uint16_t packetCapacity = 0, packetSize = 0;
  bool stopping = false, busy = false;
  explicit EspUsbHidState(EspUsbDevice &owner) : device(owner) {}
  ~EspUsbHidState() {
    espusb::internal::freeUsbMemory(reports);
    for (auto *p : {input, output, packet, control}) espusb::internal::freeUsbMemory(p);
  }
  Report *find(uint8_t id) const {
    for (size_t i = 0; i < reportCount; ++i) if (reports[i].id == id) return reports + i;
    return nullptr;
  }
  esp_err_t allocate() {
    const auto *descriptor = device.hidReportDescriptor(0);
    const size_t bytes = device.hidReportDescriptorLength(0);
    if (!descriptor || !bytes) return ESP_ERR_INVALID_ARG;
    size_t depth = 0, maximum = 0;
    for (size_t offset = 0; offset < bytes;) {
      Item next; if (!item(descriptor, bytes, offset, next)) return ESP_ERR_INVALID_ARG;
      if (next.tag == 0xa4) maximum = std::max(maximum, ++depth);
      if (next.tag == 0xb4) { if (!depth) return ESP_ERR_INVALID_ARG; --depth; }
    }
    if (depth) return ESP_ERR_INVALID_ARG;
    auto *stack = maximum ? static_cast<Global *>(espusb::internal::allocateUsbMemory(maximum * sizeof(Global))) : nullptr;
    if (maximum && !stack) return ESP_ERR_NO_MEM;
    uint8_t ids[32] = {};
    bool valid = walk(descriptor, bytes, stack, maximum, [&](uint8_t id, unsigned, uint32_t) {
      ids[id / 8] |= uint8_t(1u << (id % 8)); return true;
    });
    for (unsigned id = 0; id < 256; ++id) if (ids[id / 8] & (1u << (id % 8))) ++reportCount;
    if (!valid || !reportCount || ((ids[0] & 1) && reportCount > 1)) {
      espusb::internal::freeUsbMemory(stack); return ESP_ERR_INVALID_ARG;
    }
    reports = static_cast<Report *>(espusb::internal::allocateUsbMemory(reportCount * sizeof(Report)));
    if (!reports) { espusb::internal::freeUsbMemory(stack); return ESP_ERR_NO_MEM; }
    size_t slot = 0;
    for (unsigned id = 0; id < 256; ++id) if (ids[id / 8] & (1u << (id % 8))) {
      new (reports + slot) Report(); reports[slot++].id = uint8_t(id);
    }
    valid = walk(descriptor, bytes, stack, maximum, [&](uint8_t id, unsigned type, uint32_t bits) {
      auto *r = find(id);
      if (!r || bits > uint32_t(UINT16_MAX - (id ? 1 : 0)) * 8 - r->bits[type]) return false;
      r->bits[type] += bits; return true;
    });
    espusb::internal::freeUsbMemory(stack);
    if (!valid) return ESP_ERR_INVALID_SIZE;
    for (size_t i = 0; i < reportCount; ++i)
      for (unsigned type = 0; type < 3; ++type) capacity[type] = std::max(capacity[type], reports[i].bytes(type));
    // The receive packet is distinct from report assembly: completing each bus
    // packet also completes exact-MPS reports without waiting for a ZLP.
    const auto *config = device.configDescriptor_;
    if (!config) return ESP_ERR_INVALID_STATE;
    const auto receivePacket = [&](const uint8_t *config) -> uint16_t {
      if (!config) return 0;
      const size_t total = config[2] | size_t(config[3]) << 8;
      bool selected = false;
      for (size_t offset = 9; offset + 2 <= total;) {
        const auto *d = config + offset;
        if (d[0] < 2 || d[0] > total - offset) return 0;
        if (d[1] == 4) selected = d[0] >= 9 && d[5] == 3;
        if (selected && d[1] == 5 && d[0] >= 7 && !(d[2] & 0x80)) return uint16_t(d[4] | uint16_t(d[5]) << 8);
        offset += d[0];
      }
      return 0;
    };
    packetCapacity = std::max(receivePacket(config), receivePacket(device.configDescriptorHighSpeed_));
    input = capacity[0] ? static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(capacity[0], 64, true)) : nullptr;
    if (capacity[1] && packetCapacity) {
      output = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(capacity[1]));
      packet = static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(packetCapacity, 64, true));
    }
    const size_t controlBytes = std::max(capacity[1], capacity[2]);
    control = controlBytes ? static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(controlBytes)) : nullptr;
    if ((capacity[0] && !input) || (capacity[1] && packetCapacity && (!output || !packet)) || (controlBytes && !control)) return ESP_ERR_NO_MEM;
    return ESP_OK;
  }
  void receive() {
    if (!stopping && out && packet) usbd_edpt_xfer(rhport, out, packet, packetSize, false);
  }
  void reset() {
    portENTER_CRITICAL(&hidLock);
    interface = 0xff; in = out = 0; busy = false; assembled = 0; protocol = 1; hidDescriptor = nullptr;
    for (size_t i = 0; i < reportCount; ++i) reports[i].idle = 0;
    portEXIT_CRITICAL(&hidLock);
    device.handleHidSetProtocol(0, 1);
  }
  static void init() {}
  static void resetAll(uint8_t) { for (auto *s = first; s; s = s->next) s->reset(); }
  static bool deinit() { resetAll(0); return true; }
  static uint16_t open(uint8_t port, const tusb_desc_interface_t *itf, uint16_t available) {
    if (itf->bInterfaceClass != 3 || available < 18 || itf->bLength != 9) return 0;
    auto *s = first;
    while (s && !s->device.config().startTinyUsb) s = s->next;
    if (!s) return 0;
    // One merged HID interface per currently active Device.
    const auto *d = reinterpret_cast<const uint8_t *>(itf);
    if (d[9] != 9 || d[10] != 0x21 || d[15] != 0x22 ||
        uint16_t(d[16] | uint16_t(d[17]) << 8) != s->device.hidReportDescriptorLength(0)) return 0;
    const size_t required = 18 + 7 * itf->bNumEndpoints;
    if (required > available || !itf->bNumEndpoints || itf->bNumEndpoints > 2) return 0;
    uint8_t in = 0, out = 0; uint16_t packetSize = 0;
    for (unsigned n = 0; n < itf->bNumEndpoints; ++n) {
      const auto *ep = reinterpret_cast<const tusb_desc_endpoint_t *>(d + 18 + 7 * n);
      if (ep->bLength != 7 || ep->bDescriptorType != 5 || ep->bmAttributes.xfer != TUSB_XFER_INTERRUPT) return 0;
      if (ep->bEndpointAddress & 0x80) { if (in) return 0; in = ep->bEndpointAddress; }
      else { if (out) return 0; out = ep->bEndpointAddress; packetSize = tu_edpt_packet_size(ep); }
      if (!usbd_edpt_open(port, ep)) return 0;
    }
    if (!in || (out && s->capacity[1] && packetSize > s->packetCapacity)) return 0;
    portENTER_CRITICAL(&hidLock);
    s->rhport = port; s->interface = itf->bInterfaceNumber; s->hidDescriptor = d + 9;
    s->boot = itf->bInterfaceSubClass == 1 ? itf->bInterfaceProtocol : 0;
    s->in = in; s->out = out; s->packetSize = packetSize; s->protocol = 1; s->assembled = 0; s->busy = false;
    s->receive(); portEXIT_CRITICAL(&hidLock);
    return uint16_t(required);
  }
  static bool controlRequest(uint8_t port, uint8_t stage, const tusb_control_request_t *r) {
    auto *s = first;
    while (s && s->interface != r->wIndex) s = s->next;
    if (!s) return false;
    if (r->bmRequestType == 0x81 && r->bRequest == 6 && !(r->wValue & 0xff)) {
      if (stage != CONTROL_STAGE_SETUP) return true;
      if (r->wValue == 0x2100) return s->hidDescriptor && tud_control_xfer(port, r, const_cast<uint8_t *>(s->hidDescriptor), 9);
      if (r->wValue == 0x2200) return tud_control_xfer(port, r, const_cast<uint8_t *>(s->device.hidReportDescriptor(0)), s->device.hidReportDescriptorLength(0));
      return false;
    }
    const uint8_t id = uint8_t(r->wValue), type = uint8_t(r->wValue >> 8);
    Report *report = s->find(id);
    if (r->bmRequestType == 0x21 && r->bRequest == 9) {
      if (!report || type < 2 || type > 3 || !report->bits[type - 1] || !r->wLength || r->wLength > report->bytes(type - 1) || !s->control) return false;
      if (stage == CONTROL_STAGE_SETUP) {
        memset(s->control, 0, r->wLength);
        return tud_control_xfer(port, r, s->control, r->wLength);
      }
      if (stage == CONTROL_STAGE_DATA && (usbd_control_received_bytes() != r->wLength ||
          (id && r->wLength == report->bytes(type - 1) && s->control[0] != id))) return false;
      if (stage == CONTROL_STAGE_ACK) {
        const bool prefixed = id && r->wLength == report->bytes(type - 1) && s->control[0] == id;
        if (id && r->wLength == report->bytes(type - 1) && !prefixed) return false;
        s->device.handleHidSetReport(0, id, type, s->control + (prefixed ? 1 : 0), r->wLength - (prefixed ? 1 : 0));
      }
      return true;
    }
    if (r->bmRequestType == 0xa1 && r->bRequest == 2 && !type && r->wLength == 1 && report && report->bits[0]) {
      s->response = report->idle;
      return stage != CONTROL_STAGE_SETUP || tud_control_xfer(port, r, &s->response, 1);
    }
    if (r->bmRequestType == 0x21 && r->bRequest == 10 && !r->wLength && (!id || (report && report->bits[0]))) {
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) for (size_t i = 0; i < s->reportCount; ++i) if (!id || s->reports[i].id == id) s->reports[i].idle = type;
      return true;
    }
    if (s->boot && r->bmRequestType == 0xa1 && r->bRequest == 3 && !r->wValue && r->wLength == 1) {
      s->response = s->protocol;
      return stage != CONTROL_STAGE_SETUP || tud_control_xfer(port, r, &s->response, 1);
    }
    if (s->boot && r->bmRequestType == 0x21 && r->bRequest == 11 && r->wValue <= 1 && !r->wLength) {
      if (stage == CONTROL_STAGE_SETUP) return tud_control_status(port, r);
      if (stage == CONTROL_STAGE_ACK) { s->protocol = uint8_t(r->wValue); s->device.handleHidSetProtocol(0, s->protocol); }
      return true;
    }
    // GET_REPORT snapshots and periodic idle retransmission are separate from
    // this first transport replacement; do not fabricate a report ID as data.
    return false;
  }
  static bool complete(uint8_t, uint8_t endpoint, xfer_result_t result, uint32_t length) {
    auto *s = first;
    while (s && s->in != endpoint && s->out != endpoint) s = s->next;
    if (!s) return false;
    if (s->in == endpoint) {
      portENTER_CRITICAL(&hidLock); s->busy = false; portEXIT_CRITICAL(&hidLock); return true;
    }
    bool deliver = false; uint8_t id = 0; size_t bytes = 0;
    if (result == XFER_RESULT_SUCCESS && length <= s->packetSize && s->output && length <= s->capacity[1] - s->assembled) {
      memcpy(s->output + s->assembled, s->packet, length); s->assembled += length;
      id = s->reportCount && s->reports[0].id ? (s->assembled ? s->output[0] : 0) : 0;
      const auto *report = s->find(id);
      if (report && report->bits[1] && s->assembled <= report->bytes(1)) {
        deliver = s->assembled && (s->assembled == report->bytes(1) || length < s->packetSize);
        bytes = s->assembled;
      } else s->assembled = 0;
    } else s->assembled = 0;
    if (deliver) {
      s->assembled = 0;
      s->device.handleHidSetReport(0, id, 2, s->output + (id ? 1 : 0), uint16_t(bytes - (id ? 1 : 0)));
    }
    portENTER_CRITICAL(&hidLock); s->receive(); portEXIT_CRITICAL(&hidLock);
    return true;
  }
};
EspUsbHidState *EspUsbHidState::first = nullptr;
namespace {
const usbd_class_driver_t hidDriver = {
  .name = "EspHid", .init = EspUsbHidState::init, .deinit = EspUsbHidState::deinit,
  .reset = EspUsbHidState::resetAll, .open = EspUsbHidState::open,
  .control_xfer_cb = EspUsbHidState::controlRequest, .xfer_cb = EspUsbHidState::complete, .sof = nullptr
};
}

EspUsbHidFunction::EspUsbHidFunction(EspUsbDevice &device) : EspUsbDeviceClass(device) {}
EspUsbHidFunction::~EspUsbHidFunction() { end(); }
bool EspUsbHidFunction::begin() {
  if (!registered_ || hidState_) { hidStartError_ = ESP_ERR_INVALID_STATE; return false; }
  auto *s = EspUsbHidState::first;
  while (s && &s->device != &device_) s = s->next;
  if (!s) {
    s = espusb::internal::newUsbObject<EspUsbHidState>(device_);
    hidStartError_ = s ? s->allocate() : ESP_ERR_NO_MEM;
    if (hidStartError_ != ESP_OK) { espusb::internal::deleteUsbObject(s); return false; }
    if (!EspUsbHidState::first && !espUsbDeviceRegisterAppDrivers(&hidDriver, 1)) {
      espusb::internal::deleteUsbObject(s); hidStartError_ = ESP_ERR_NO_MEM; return false;
    }
    s->next = EspUsbHidState::first; EspUsbHidState::first = s;
  }
  portENTER_CRITICAL(&hidLock);
  hidState_ = s; ++s->references;
  portEXIT_CRITICAL(&hidLock);
  hidStartError_ = ESP_OK; return true;
}
void EspUsbHidFunction::prepareToStop() {
  portENTER_CRITICAL(&hidLock); if (hidState_) hidState_->stopping = true; portEXIT_CRITICAL(&hidLock);
}
void EspUsbHidFunction::end() {
  if (device_.tinyusbStarted_ && hidState_) { device_.end(); return; }
  portENTER_CRITICAL(&hidLock);
  auto *s = hidState_; hidState_ = nullptr;
  bool remove = s && !--s->references;
  if (remove) {
    auto **link = &EspUsbHidState::first;
    while (*link && *link != s) link = &(*link)->next;
    if (*link) *link = s->next;
  }
  const bool empty = !EspUsbHidState::first;
  portEXIT_CRITICAL(&hidLock);
  if (remove) espusb::internal::deleteUsbObject(s);
  if (empty) espUsbDeviceUnregisterAppDrivers(&hidDriver, 1);
}
bool EspUsbHidFunction::sendHidInput(uint8_t id, const void *data, size_t length, uint32_t timeoutMs) {
  if (timeoutMs && espusb::internal::tinyUsbRuntimeInTask()) { device_.setLastError(ESP_ERR_INVALID_STATE); return false; }
  const uint32_t start = millis();
  do {
    portENTER_CRITICAL(&hidLock);
    auto *s = hidState_;
    if (!s || s->stopping || !s->in || !tud_ready()) {
      portEXIT_CRITICAL(&hidLock); device_.setLastError(ESP_ERR_INVALID_STATE); return false;
    }
    const auto *report = s->find(id);
    if (length > size_t(UINT16_MAX) - (id ? 1 : 0)) {
      portEXIT_CRITICAL(&hidLock); device_.setLastError(ESP_ERR_INVALID_SIZE); return false;
    }
    const size_t bytes = length + (id ? 1 : 0);
    if (!data || !length || !report || !report->bits[0] || bytes > report->bytes(0) || bytes > s->capacity[0]) {
      portEXIT_CRITICAL(&hidLock); device_.setLastError(ESP_ERR_INVALID_SIZE); return false;
    }
    if (!s->busy && usbd_edpt_claim(s->rhport, s->in)) {
      if (id) s->input[0] = id;
      memcpy(s->input + (id ? 1 : 0), data, length);
      s->busy = true;
      const bool sent = usbd_edpt_xfer(s->rhport, s->in, s->input, uint16_t(bytes), false);
      if (!sent) { s->busy = false; usbd_edpt_release(s->rhport, s->in); }
      portEXIT_CRITICAL(&hidLock); device_.setLastError(sent ? ESP_OK : ESP_FAIL); return sent;
    }
    portEXIT_CRITICAL(&hidLock);
    if (millis() - start >= timeoutMs) { device_.setLastError(ESP_ERR_TIMEOUT); return false; }
    delay(1);
  } while (true);
}
size_t EspUsbHidFunction::inputReportCapacity() const {
  portENTER_CRITICAL(&hidLock); const size_t n = hidState_ ? hidState_->capacity[0] : 0; portEXIT_CRITICAL(&hidLock); return n;
}
size_t EspUsbHidFunction::outputReportCapacity() const {
  portENTER_CRITICAL(&hidLock); const size_t n = hidState_ ? hidState_->capacity[1] : 0; portEXIT_CRITICAL(&hidLock); return n;
}
size_t EspUsbHidFunction::featureReportCapacity() const {
  portENTER_CRITICAL(&hidLock); const size_t n = hidState_ ? hidState_->capacity[2] : 0; portEXIT_CRITICAL(&hidLock); return n;
}
