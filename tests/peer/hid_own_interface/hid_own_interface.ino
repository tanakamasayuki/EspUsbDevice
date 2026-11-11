// Manual IDF host: claim every HID interface and address each one by number.
#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#if CONFIG_IDF_TARGET_ESP32P4
#include <esp_cache.h>
// P4 host DMA reads OUT and control data from memory, not from the cache.
bool flush(usb_transfer_t *t) { return esp_cache_msync(t->data_buffer, t->data_buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) == ESP_OK; }
#else
bool flush(usb_transfer_t *) { return true; }
#endif
struct Hid { uint8_t number = 0, subclass = 0, protocol = 0, in = 0; uint16_t packet = 0, reportLength = 0; bool claimed = false; };
usb_host_client_handle_t client = nullptr;
usb_device_handle_t handle = nullptr;
usb_transfer_t *control = nullptr, *input = nullptr;
std::atomic<bool> running{false}, exited{true}, allFree{false}, done{true}, enumerated{false};
Hid hids[4];
unsigned hidCount = 0;
void daemon(void *) {
  while (running.load()) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree.store(true);
  }
  exited.store(true); vTaskDelete(nullptr);
}
void clientEvent(const usb_host_client_event_msg_t *event, void *) {
  if (event->event != USB_HOST_CLIENT_EVENT_NEW_DEV || handle) return;
  if (usb_host_device_open(client, event->new_dev.address, &handle) != ESP_OK) return;
  const usb_config_desc_t *config = nullptr;
  if (usb_host_get_active_config_descriptor(handle, &config) != ESP_OK) return;
  const uint8_t *d = reinterpret_cast<const uint8_t *>(config);
  Hid *current = nullptr;
  for (size_t i = 0; i + 2 <= config->wTotalLength && d[i] >= 2 && d[i] <= config->wTotalLength - i; i += d[i]) {
    const uint8_t *p = d + i;
    if (p[1] == 4 && p[0] >= 9) {
      current = p[5] == 3 && hidCount < 4 ? &hids[hidCount++] : nullptr;
      if (current) { *current = {}; current->number = p[2]; current->subclass = p[6]; current->protocol = p[7]; }
    } else if (current && p[1] == 0x21 && p[0] >= 9) current->reportLength = p[7] | uint16_t(p[8]) << 8;
    else if (current && p[1] == 5 && p[0] >= 7 && (p[2] & 0x80)) { current->in = p[2]; current->packet = (p[4] | uint16_t(p[5]) << 8) & 0x7ff; }
  }
  for (unsigned n = 0; n < hidCount; ++n) hids[n].claimed = usb_host_interface_claim(client, handle, hids[n].number, 0) == ESP_OK;
  enumerated.store(true);
}
void events(unsigned ms) {
  const uint32_t until = millis() + ms;
  do { usb_host_client_handle_events(client, pdMS_TO_TICKS(1)); } while (int32_t(until - millis()) > 0);
}
// An index, not a Hid *: Arduino's generated prototypes precede the struct.
int find(unsigned number) {
  for (unsigned n = 0; n < hidCount; ++n) if (hids[n].number == number) return int(n);
  return -1;
}
bool wait() {
  const uint32_t until = millis() + 3000;
  while (!done.load() && int32_t(until - millis()) > 0) events(1);
  return done.load();
}
// Control request with an optional data stage of `bytes`; OUT data is taken
// from control->data_buffer + 8. Returns false on a STALL or a short transfer.
bool request(uint8_t type, uint8_t code, uint16_t value, uint16_t index, uint16_t bytes) {
  if (!done.load()) return false;
  auto *s = reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *s = {}; s->bmRequestType = type; s->bRequest = code; s->wValue = value; s->wIndex = index; s->wLength = bytes;
  control->device_handle = handle; control->bEndpointAddress = 0; control->num_bytes = 8 + bytes;
  control->callback = [](usb_transfer_t *) { done.store(true); };
  if (!flush(control)) return false;
  done.store(false);
  if (usb_host_transfer_submit_control(client, control) != ESP_OK) { done.store(true); return false; }
  return wait() && control->status == USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes == 8 + bytes;
}
bool stop() {
  if (!done.load()) return false;
  bool ok = true;
  for (auto **t : {&control, &input}) if (*t) { ok &= usb_host_transfer_free(*t) == ESP_OK; *t = nullptr; }
  for (unsigned n = 0; n < hidCount; ++n) if (hids[n].claimed) ok &= usb_host_interface_release(client, handle, hids[n].number) == ESP_OK;
  hidCount = 0;
  if (handle) { ok &= usb_host_device_close(client, handle) == ESP_OK; handle = nullptr; }
  if (client) { ok &= usb_host_client_deregister(client) == ESP_OK; client = nullptr; }
  const uint32_t until = millis() + 3000;
  while (!allFree.load() && int32_t(until - millis()) > 0) delay(1);
  running.store(false); while (!exited.load()) delay(1);
  return ok && allFree.load() && usb_host_uninstall() == ESP_OK;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (client) usb_host_client_handle_events(client, 0);
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    usb_host_config_t config = {}; config.intr_flags = ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    // This Core enables filtering: a null callback rejects every device.
    config.enum_filter_cb = [](const usb_device_desc_t *d, uint8_t *) { return d->idVendor == 0x303a && d->idProduct == 0x4000; };
#endif
    hidCount = 0; enumerated.store(false); done.store(true);
    bool ok = usb_host_install(&config) == ESP_OK;
    running.store(true); exited.store(false); allFree.store(false);
    ok &= xTaskCreate(daemon, "usb-lib", 4096, nullptr, 5, nullptr) == pdPASS;
    usb_host_client_config_t c = {}; c.max_num_event_msg = 8; c.async.client_event_callback = clientEvent;
    ok &= usb_host_client_register(&c, &client) == ESP_OK;
    ok &= usb_host_transfer_alloc(8 + 256, 0, &control) == ESP_OK && usb_host_transfer_alloc(64, 0, &input) == ESP_OK;
    const uint32_t until = millis() + 10000;
    while (ok && !enumerated.load() && int32_t(until - millis()) > 0) events(1);
    ok &= enumerated.load();
    for (unsigned n = 0; n < hidCount; ++n) ok &= hids[n].claimed;
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command == "layout") {
    // Number, subclass and protocol of each HID interface, in descriptor order.
    String line = "LAYOUT " + String(hidCount);
    for (unsigned n = 0; n < hidCount; ++n)
      line += " " + String(hids[n].number) + ":" + String(hids[n].subclass) + "/" + String(hids[n].protocol);
    Serial.printf("%s\r\n", line.c_str());
  } else if (command.startsWith("report ")) {
    // The interface's own report descriptor, and the report IDs it declares.
    const int index = find(command.substring(7).toInt());
    Hid *hid = index < 0 ? nullptr : &hids[index];
    const bool ok = hid && hid->reportLength && hid->reportLength <= 256 && request(0x81, 6, 0x2200, hid->number, hid->reportLength);
    String ids;
    for (size_t i = 0; ok && i < hid->reportLength;) {
      const uint8_t prefix = control->data_buffer[8 + i];
      const size_t size = (prefix & 3) == 3 ? 4 : prefix & 3;
      if (prefix == 0x85 && i + 1 < hid->reportLength) ids += (ids.length() ? "," : "") + String(control->data_buffer[9 + i]);
      i += 1 + size;
    }
    Serial.printf("REPORT %u ids=%s\r\n", unsigned(ok), ids.length() ? ids.c_str() : "none");
  } else if (command.startsWith("protocol ")) {
    unsigned number = 0, value = 0;
    sscanf(command.c_str(), "protocol %u %u", &number, &value);
    bool ok = request(0x21, 11, value, number, 0);
    ok &= request(0xa1, 3, 0, number, 1) && control->data_buffer[8] == value;
    Serial.printf("PROTOCOL %u\r\n", unsigned(ok));
  } else if (command.startsWith("read ")) {
    const int index = find(command.substring(5).toInt());
    Hid *hid = index < 0 ? nullptr : &hids[index];
    bool ok = hid && hid->in && hid->packet <= 64 && done.load();
    if (ok) {
      input->device_handle = handle; input->bEndpointAddress = hid->in; input->num_bytes = hid->packet;
      input->callback = [](usb_transfer_t *) { done.store(true); };
      done.store(false);
      ok = usb_host_transfer_submit(input) == ESP_OK;
      if (!ok) done.store(true);
      ok = ok && wait() && input->status == USB_TRANSFER_STATUS_COMPLETED;
    }
    String bytes;
    for (int i = 0; ok && i < input->actual_num_bytes; ++i) { char hex[4]; snprintf(hex, sizeof(hex), "%02x", input->data_buffer[i]); bytes += hex; }
    Serial.printf("READ %u %s\r\n", unsigned(ok), bytes.c_str());
  } else if (command.startsWith("led ")) {
    // SET_REPORT(Output) through the control pipe to one interface.
    unsigned number = 0, id = 0, value = 0;
    sscanf(command.c_str(), "led %u %u %u", &number, &id, &value);
    uint8_t *data = control->data_buffer + 8;
    if (id) *data++ = uint8_t(id);
    *data = uint8_t(value);
    Serial.printf("LED %u\r\n", unsigned(request(0x21, 9, 0x0200 | id, number, id ? 2 : 1)));
  } else if (command == "stop") {
    Serial.println(!client && !running.load() ? "STOPPED" : stop() ? "STOPPED" : "STOP_FAILED");
  }
}
