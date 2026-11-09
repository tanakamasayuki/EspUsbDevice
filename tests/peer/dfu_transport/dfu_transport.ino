#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client = nullptr;
usb_device_handle_t handle = nullptr;
usb_transfer_t *control = nullptr;
std::atomic<bool> running{false}, exited{true}, allFree{false}, done{true};
bool claimed[3] = {};
uint16_t transferBytes = 0;
void daemon(void *) {
  while (running.load()) {
    uint32_t flags = 0; usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
  }
  exited = true; vTaskDelete(nullptr);
}
void event(const usb_host_client_event_msg_t *e, void *) {
  if (e->event != USB_HOST_CLIENT_EVENT_NEW_DEV || handle) return;
  esp_err_t error = usb_host_device_open(client, e->new_dev.address, &handle); Serial.printf("DEVICE_OPEN %d\r\n",int(error));
  if (error != ESP_OK) return;
  const usb_config_desc_t *config = nullptr;
  error = usb_host_get_active_config_descriptor(handle, &config); Serial.printf("CONFIG %d\r\n",int(error));
  if (error != ESP_OK) return;
  const uint8_t *d = reinterpret_cast<const uint8_t *>(config); unsigned found = 0;
  for (size_t i = 0; i + 18 <= config->wTotalLength && d[i] >= 2; i += d[i]) {
    if (d[i + 1] == 4 && d[i] == 9 && d[i + 5] == 0xfe && d[i + 6] == 1 && d[i + 9] == 9 && d[i + 10] == 0x21) {
      const unsigned index = d[i + 2]; if (index >= 3) continue;
      const unsigned n = d[i + 14] | unsigned(d[i + 15]) << 8;
      if (n != (index == 0 ? transferBytes : index == 1 ? 65 : 1024)) continue;
      claimed[index] = usb_host_interface_claim(client, handle, index, 0) == ESP_OK;
      found += claimed[index];
    }
  }
  Serial.printf("CLAIMED %u\r\n", found);
}
void events(unsigned ms) {
  const uint32_t until = millis() + ms;
  do { usb_host_client_handle_events(client, pdMS_TO_TICKS(1)); } while (int32_t(until - millis()) > 0);
}
usb_transfer_status_t result = USB_TRANSFER_STATUS_ERROR;
uint32_t actual = 0;
bool request(uint8_t type, uint8_t code, uint16_t value, uint16_t n, uint16_t index = 0) {
  auto *s = reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *s = {}; s->bmRequestType = type; s->bRequest = code; s->wValue = value; s->wIndex = index; s->wLength = n;
  if (!(type & 0x80)) for (unsigned i = 0; i < n; ++i) control->data_buffer[i + 8] = uint8_t((value * 17u + i) % 251);
  control->device_handle = handle; control->bEndpointAddress = 0; control->num_bytes = 8 + n;
  control->callback = [](usb_transfer_t *) { done = true; }; done = false;
  if (usb_host_transfer_submit_control(client, control) != ESP_OK) { done = true; return false; }
  const uint32_t until = millis() + 3000;
  while (!done.load() && int32_t(until - millis()) > 0) events(1);
  result = control->status; actual = control->actual_num_bytes;
  return done.load() && result == USB_TRANSFER_STATUS_COMPLETED && actual == 8u + n;
}
bool stall(uint8_t type, uint8_t code, uint16_t value, uint16_t n, uint16_t index = 0) {
  request(type, code, value, n, index); return done.load() && result == USB_TRANSFER_STATUS_STALL;
}
bool state(uint8_t expected, uint16_t index = 0) { return request(0xa1, 5, 0, 1, index) && control->data_buffer[8] == expected; }
bool status(uint8_t expectedState, uint8_t expectedStatus = 0, uint32_t expectedPoll = 0, uint16_t index = 0) {
  if (!request(0xa1, 3, 0, 6, index)) return false;
  const auto *d = control->data_buffer + 8;
  const uint32_t poll = d[1] | uint32_t(d[2]) << 8 | uint32_t(d[3]) << 16;
  bool ok = d[0] == expectedStatus && d[4] == expectedState && !d[5] && poll == expectedPoll;
  if (!ok) Serial.printf("STATUS_BAD state=%u status=%u poll=%u expected=%u/%u/%u\r\n",d[4],d[0],unsigned(poll),expectedState,expectedStatus,unsigned(expectedPoll));
  delay(poll); return ok;
}
bool download(uint16_t block, uint16_t n, uint16_t index = 0) {
  return request(0x21, 1, block, n, index) && state(3,index) && status(4,0,7,index) && status(5,0,0,index);
}
bool manifest(uint16_t block, uint16_t index = 0) {
  return request(0x21,1,block,0,index) && state(6,index) && status(7,0,11,index) && status(2,0,0,index);
}
bool clear(uint8_t expected = 15) { return status(10,expected) && request(0x21,4,0,0) && state(2); }
bool stop() {
  if (!done.load()) return false;
  bool ok = true;
  if (control) { ok &= usb_host_transfer_free(control) == ESP_OK; control = nullptr; }
  for (unsigned i = 0; i < 3; ++i) { if (claimed[i]) ok &= usb_host_interface_release(client,handle,i) == ESP_OK; claimed[i] = false; }
  if (handle) { ok &= usb_host_device_close(client,handle) == ESP_OK; handle = nullptr; }
  if (client) { ok &= usb_host_client_deregister(client) == ESP_OK; client = nullptr; }
  const uint32_t until = millis() + 3000;
  while (!allFree.load() && int32_t(until - millis()) > 0) delay(1);
  running = false; while (!exited.load()) delay(1);
  return ok && allFree.load() && usb_host_uninstall() == ESP_OK;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("start ")) {
    transferBytes = command.substring(6).toInt();
    usb_host_config_t cfg = {}; cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb = [](const usb_device_desc_t *d, uint8_t *) { return d->idVendor == 0x303a && d->idProduct == 0x4000; };
#endif
    esp_err_t error = usb_host_install(&cfg); Serial.printf("INSTALL %d\r\n",int(error)); bool ok = error == ESP_OK;
    running = true; exited = false; allFree = false;
    ok &= xTaskCreate(daemon,"dfu-host",4096,nullptr,5,nullptr) == pdPASS;
    usb_host_client_config_t cc = {}; cc.max_num_event_msg = 8; cc.async.client_event_callback = event;
    error = usb_host_client_register(&cc,&client); Serial.printf("REGISTER %d\r\n",int(error)); ok &= error == ESP_OK;
    error = usb_host_transfer_alloc(65544,0,&control); Serial.printf("ALLOC %d\r\n",int(error)); ok &= error == ESP_OK;
    const uint32_t until = millis() + 10000;
    while (ok && !claimed[2] && int32_t(until - millis()) > 0) events(1);
    Serial.printf("HOST_READY %u\r\n", unsigned(ok && claimed[0] && claimed[1] && claimed[2] && state(2) && state(2,1) && state(0,2)));
  } else if (command == "roundtrip") {
    bool ok = true; uint16_t block = 0; size_t bytes = 0;
    for (unsigned n : {1u,63u,64u,65u,127u,128u,129u,unsigned(transferBytes)}) {
      if (n > transferBytes) continue;
      ok &= download(block++,n); bytes += n; if (!ok) break;
    }
    ok &= manifest(block) && state(2,1);
    Serial.printf("ROUNDTRIP %u bytes=%u blocks=%u\r\n",unsigned(ok),unsigned(bytes),unsigned(block));
  } else if (command == "errors") {
    bool ok = stall(0xa1,3,0,5) && clear();
    ok &= stall(0x21,1,1,64) && clear(9);
    ok &= stall(0xa1,2,0,64) && clear();
    if (transferBytes < 65535) ok &= stall(0x21,1,0,transferBytes+1) && clear();
    ok &= stall(1,11,1,0) && state(2);
    ok &= stall(0x21,4,0,0) && clear();
    ok &= download(0,64) && request(0x21,6,0,0) && state(2);
    ok &= request(0x21,1,0,64) && state(3) && request(0x21,6,0,0) && state(2);
    Serial.printf("ERRORS %u\r\n",unsigned(ok));
  } else if (command == "runtime") {
    bool ok = status(0,0,0,2) && stall(0x21,1,0,64,2) && state(0,2);
    ok &= request(0x21,0,500,0,2) && state(1,2); delay(550);
    ok &= state(0,2); Serial.printf("RUNTIME %u\r\n",unsigned(ok));
  } else if (command == "multi") {
    bool ok = download(0,64,0) && download(0,65,1) && request(0x21,6,0,0,0) && state(2,0) && state(5,1) && manifest(1,1);
    Serial.printf("MULTI %u\r\n",unsigned(ok));
  } else if (command == "manifest_wait") {
    bool ok = download(0,64) && request(0x21,1,1,0) && status(7,0,11);
    ok &= stall(0xa1,5,0,1) && stall(0x21,6,0,0) && stall(0x21,1,0,64);
    ok &= request(0x81,10,0,1) && control->data_buffer[8] == 0;
    Serial.printf("MANIFEST_WAIT %u\r\n",unsigned(ok));
  } else if (command == "write_error") {
    bool ok = request(0x21,1,0,64) && status(4,0,7) && clear(3);
    Serial.printf("WRITE_ERROR %u\r\n",unsigned(ok));
  } else if (command == "manifest_error") {
    bool ok = download(0,64) && request(0x21,1,1,0) && status(7,0,11) && clear(7);
    Serial.printf("MANIFEST_ERROR %u\r\n",unsigned(ok));
  } else if (command == "open") { Serial.printf("OPENED %u\r\n",unsigned(download(0,64)));
  } else if (command == "stop") { Serial.println(!client || stop() ? "STOPPED" : "STOP_FAILED"); }
}
