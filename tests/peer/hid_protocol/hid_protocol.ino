// Manual IDF host: leave interrupt IN unpolled to hold an old report in DMA.
#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client = nullptr;
usb_device_handle_t handle = nullptr;
usb_transfer_t *control = nullptr, *input = nullptr;
std::atomic<bool> running{false}, exited{true}, allFree{false};
std::atomic<bool> controlDone{false}, inputDone{false};
bool claimed = false, inputActive = false;
uint8_t endpoint = 0;
uint16_t packetSize = 0;
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
  esp_err_t error = usb_host_device_open(client, event->new_dev.address, &handle);
  Serial.printf("OPEN %d address=%u\r\n", int(error), unsigned(event->new_dev.address));
  if (error != ESP_OK) return;
  const usb_config_desc_t *config = nullptr;
  if (usb_host_get_active_config_descriptor(handle, &config) != ESP_OK) return;
  const uint8_t *data = reinterpret_cast<const uint8_t *>(config);
  for (size_t i = 0; i + 2 <= config->wTotalLength && data[i] >= 2; i += data[i]) {
    if (i + data[i] > config->wTotalLength) break;
    if (data[i + 1] == 5 && data[i] >= 7 && (data[i + 2] & 0x80)) {
      endpoint = data[i + 2]; packetSize = data[i + 4] | uint16_t(data[i + 5]) << 8; break;
    }
  }
  error = endpoint ? usb_host_interface_claim(client, handle, 0, 0) : ESP_ERR_NOT_FOUND;
  claimed = error == ESP_OK;
  Serial.printf("CLAIM %d endpoint=%u mps=%u\r\n", int(error), unsigned(endpoint), unsigned(packetSize));
}
void events(unsigned ms) {
  const uint32_t until = millis() + ms;
  do { usb_host_client_handle_events(client, pdMS_TO_TICKS(1)); } while (int32_t(until - millis()) > 0);
}
bool request(uint8_t type, uint8_t code, uint16_t value, uint16_t bytes) {
  auto *s = reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *s = {}; s->bmRequestType = type; s->bRequest = code; s->wValue = value; s->wIndex = 0; s->wLength = bytes;
  control->device_handle = handle; control->bEndpointAddress = 0; control->num_bytes = 8 + bytes;
  control->callback = [](usb_transfer_t *) { controlDone.store(true); };
  controlDone.store(false);
  if (usb_host_transfer_submit_control(client, control) != ESP_OK) return false;
  const uint32_t until = millis() + 3000;
  while (!controlDone.load() && int32_t(until - millis()) > 0) events(1);
  return controlDone.load() && control->status == USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes == 8 + bytes;
}
bool stop() {
  bool ok = true;
  if (inputActive && !inputDone.load()) {
    ok &= usb_host_endpoint_halt(handle, endpoint) == ESP_OK;
    ok &= usb_host_endpoint_flush(handle, endpoint) == ESP_OK;
    const uint32_t until = millis() + 3000;
    while (!inputDone.load() && int32_t(until - millis()) > 0) events(1);
    ok &= inputDone.load();
  }
  // Do not free DMA-owned transfers after an unexpected timeout.
  if (!ok || (control && !controlDone.load())) return false;
  if (input) { ok &= usb_host_transfer_free(input) == ESP_OK; input = nullptr; }
  if (control) { ok &= usb_host_transfer_free(control) == ESP_OK; control = nullptr; }
  if (claimed) ok &= usb_host_interface_release(client, handle, 0) == ESP_OK;
  claimed = false;
  if (handle) { ok &= usb_host_device_close(client, handle) == ESP_OK; handle = nullptr; }
  if (client) { ok &= usb_host_client_deregister(client) == ESP_OK; client = nullptr; }
  const uint32_t until = millis() + 3000;
  while (!allFree.load() && int32_t(until - millis()) > 0) delay(1);
  running.store(false); while (!exited.load()) delay(1);
  ok &= allFree.load() && usb_host_uninstall() == ESP_OK;
  endpoint = 0; inputActive = false;
  return ok;
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
    esp_err_t error = usb_host_install(&config);
    Serial.printf("INSTALL %d\r\n", int(error));
    bool ok = error == ESP_OK;
    if (ok) {
      running.store(true); exited.store(false); allFree.store(false);
      ok = xTaskCreate(daemon, "usb-lib", 4096, nullptr, 5, nullptr) == pdPASS;
    }
    usb_host_client_config_t c = {}; c.max_num_event_msg = 8; c.async.client_event_callback = clientEvent;
    if (ok) { error = usb_host_client_register(&c, &client); ok = error == ESP_OK; Serial.printf("CLIENT %d\r\n", int(error)); }
    const uint32_t until = millis() + 10000;
    while (ok && !claimed && int32_t(until - millis()) > 0) events(1);
    ok &= claimed;
    if (ok) ok = usb_host_transfer_alloc(72, 0, &control) == ESP_OK && usb_host_transfer_alloc(packetSize, 0, &input) == ESP_OK;
    controlDone.store(true);
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command.startsWith("protocol ")) {
    unsigned value = command.substring(9).toInt();
    bool ok = request(0x21, 11, value, 0);
    ok &= request(0xa1, 3, 0, 1) && control->data_buffer[8] == value;
    Serial.printf("PROTOCOL_SET %u\r\n", unsigned(ok));
  } else if (command == "quiet") {
    input->device_handle = handle; input->bEndpointAddress = endpoint; input->num_bytes = packetSize;
    input->callback = [](usb_transfer_t *) { inputDone.store(true); };
    inputDone.store(false); inputActive = usb_host_transfer_submit(input) == ESP_OK;
    events(30);
    Serial.printf("QUIET %u\r\n", unsigned(inputActive && !inputDone.load()));
  } else if (command.startsWith("result ")) {
    unsigned bytes = 0, value = 0, mouse = 0;
    sscanf(command.c_str(), "result %u %u %u", &bytes, &value, &mouse);
    const uint32_t until = millis() + 3000;
    while (!inputDone.load() && int32_t(until - millis()) > 0) events(1);
    bool ok = inputDone.load() && input->status == USB_TRANSFER_STATUS_COMPLETED && unsigned(input->actual_num_bytes) == bytes;
    uint8_t expected[32] = {};
    if (mouse) { expected[0] = 1; expected[1] = value; expected[2] = uint8_t(-int(value)); if (bytes == 4) expected[3] = 7; }
    else if (bytes == 8) expected[2] = value;
    else expected[1 + value / 8] = uint8_t(1u << (value % 8));
    ok &= bytes <= sizeof(expected) && memcmp(input->data_buffer, expected, bytes) == 0;
    if (inputDone.load()) inputActive = false;
    Serial.printf("RECEIVED %u bytes=%u\r\n", unsigned(ok), unsigned(input->actual_num_bytes));
  } else if (command == "stop") {
    Serial.println(!client && !running.load() ? "STOPPED" : stop() ? "STOPPED" : "STOP_FAILED");
  }
}
