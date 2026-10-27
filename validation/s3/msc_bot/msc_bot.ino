// Manual IDF host for BOT transport disagreement, sense and reset recovery.
#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client = nullptr;
usb_device_handle_t handle = nullptr;
usb_transfer_t *control = nullptr, *input = nullptr, *output = nullptr;
std::atomic<bool> running{false}, exited{true}, allFree{false};
std::atomic<bool> controlDone{false}, inputDone{false};
bool claimed = false, inputActive = false;
uint8_t endpoint = 0, outEndpoint = 0, selectedInterface = 0;
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
  bool selected = false;
  for (size_t i = 0; i + 2 <= config->wTotalLength && data[i] >= 2; i += data[i]) {
    if (i + data[i] > config->wTotalLength) break;
    if (data[i + 1] == 4 && data[i] >= 9) selected = data[i + 2] == selectedInterface;
    if (selected && data[i + 1] == 5 && data[i] >= 7) {
      if (data[i + 2] & 0x80) { endpoint = data[i + 2]; packetSize = data[i + 4] | uint16_t(data[i + 5]) << 8; }
      else outEndpoint = data[i + 2];
    }
  }
  error = endpoint ? usb_host_interface_claim(client, handle, selectedInterface, 0) : ESP_ERR_NOT_FOUND;
  claimed = error == ESP_OK;
  Serial.printf("CLAIM %d endpoint=%u mps=%u\r\n", int(error), unsigned(endpoint), unsigned(packetSize));
}
void events(unsigned ms) {
  const uint32_t until = millis() + ms;
  do { usb_host_client_handle_events(client, pdMS_TO_TICKS(1)); } while (int32_t(until - millis()) > 0);
}
bool request(uint8_t type, uint8_t code, uint16_t value, uint16_t bytes, uint16_t index = selectedInterface) {
  auto *s = reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *s = {}; s->bmRequestType = type; s->bRequest = code; s->wValue = value; s->wIndex = index; s->wLength = bytes;
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
  if (output) { ok &= usb_host_transfer_free(output) == ESP_OK; output = nullptr; }
  if (input) { ok &= usb_host_transfer_free(input) == ESP_OK; input = nullptr; }
  if (control) { ok &= usb_host_transfer_free(control) == ESP_OK; control = nullptr; }
  if (claimed) ok &= usb_host_interface_release(client, handle, selectedInterface) == ESP_OK;
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

uint32_t tag = 100;
uint16_t sectorBytes = 512;
uint32_t readLe(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void putLe(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (8 * i)); }
uint32_t actual = 0;
usb_transfer_status_t lastStatus = USB_TRANSFER_STATUS_ERROR;
bool bulk(bool in, uint32_t bytes) {
  usb_transfer_t *t = in ? input : output;
  t->device_handle = handle; t->bEndpointAddress = in ? endpoint : outEndpoint;
  t->num_bytes = in ? ((bytes + packetSize - 1) / packetSize) * packetSize : bytes;
  t->callback = [](usb_transfer_t *) { inputDone.store(true); };
  inputDone.store(false); inputActive = in;
  if (usb_host_transfer_submit(t) != ESP_OK) { inputActive = false; return false; }
  const uint32_t until = millis() + 3000;
  while (!inputDone.load() && int32_t(until - millis()) > 0) events(1);
  if (!inputDone.load()) {
    usb_host_endpoint_halt(handle, t->bEndpointAddress); usb_host_endpoint_flush(handle, t->bEndpointAddress);
    while (!inputDone.load()) events(1);
  }
  inputActive = false; actual = t->actual_num_bytes; lastStatus = t->status;
  return lastStatus == USB_TRANSFER_STATUS_COMPLETED;
}
bool reclaim() {
  bool ok = usb_host_interface_release(client, handle, selectedInterface) == ESP_OK;
  claimed = false;
  if (ok) claimed = usb_host_interface_claim(client, handle, selectedInterface, 0) == ESP_OK;
  return ok && claimed;
}
bool clear(uint8_t ep) {
  bool ok = usb_host_endpoint_halt(handle, ep) == ESP_OK;
  ok &= usb_host_endpoint_flush(handle, ep) == ESP_OK;
  ok &= usb_host_endpoint_clear(handle, ep) == ESP_OK;
  ok &= request(2, 1, 0, 0, ep);
  return ok;
}
bool reset() {
  const bool r = request(0x21, 255, 0, 0), i = clear(endpoint), o = clear(outEndpoint);
  Serial.printf("RESET r=%u in=%u out=%u\r\n", unsigned(r), unsigned(i), unsigned(o));
  return r && i && o && reclaim();
}
bool cbw(uint8_t opcode, bool in, uint32_t bytes, unsigned blocks = 1, unsigned lba = 1) {
  memset(output->data_buffer, 0, 31); putLe(output->data_buffer, 0x43425355);
  putLe(output->data_buffer + 4, ++tag); putLe(output->data_buffer + 8, bytes);
  output->data_buffer[12] = in ? 0x80 : 0; output->data_buffer[14] = opcode < 0x20 ? 6 : 10;
  output->data_buffer[15] = opcode;
  if (opcode == 0x28 || opcode == 0x2a) { output->data_buffer[20] = lba; output->data_buffer[23] = blocks; }
  if (opcode == 3) output->data_buffer[19] = 18;
  return bulk(false, 31) && actual == 31;
}
bool csw(unsigned expectedStatus, uint32_t residue) {
  if (!bulk(true, 13) || actual != 13) { Serial.printf("CSW_TRANSFER status=%u actual=%u\r\n", unsigned(lastStatus), unsigned(actual)); return false; }
  const auto *b = input->data_buffer;
  const bool ok = readLe(b) == 0x53425355 && readLe(b + 4) == tag && readLe(b + 8) == residue && b[12] == expectedStatus;
  if (!ok) Serial.printf("CSW status=%u residue=%u tag=%u actual=%u\r\n", unsigned(b[12]), unsigned(readLe(b + 8)), unsigned(readLe(b + 4)), unsigned(actual));
  return ok;
}
bool matrixCase(unsigned n) {
  const uint16_t hostLength[] = {0,0,0,0,64,uint16_t(2 * sectorBytes),sectorBytes,64,sectorBytes,64,sectorBytes,uint16_t(2 * sectorBytes),sectorBytes,64};
  static const uint8_t op[] = {0,0,0x28,0x2a,0,0x28,0x28,0x28,0x2a,0,0x28,0x2a,0x2a,0x2a};
  const bool in = n >= 4 && n <= 8;
  const unsigned length = hostLength[n];
  const bool stall = n == 4 || n == 5 || n == 8 || n == 9 || n == 10 || n == 11;
  const unsigned status = n == 2 || n == 3 || n == 7 || n == 8 || n == 10 || n == 13 ? 2 : 0;
  const unsigned processed = n == 5 || n == 6 || n == 11 || n == 12 ? sectorBytes : n == 7 || n == 13 ? 64 : 0;
  bool ok = cbw(op[n], in, length);
  if (length) {
    memset(output->data_buffer, 0x6a, length);
    const bool completed = bulk(in, length);
    ok &= stall ? ((!completed && lastStatus == USB_TRANSFER_STATUS_STALL) || (n == 5 && completed && actual == sectorBytes)) : completed && actual == length;
    if (stall) ok &= clear(endpoint) && clear(outEndpoint) && reclaim();
  }
  ok &= csw(status, length - processed);
  ok &= reset();
  // A successful no-data command after every case proves the next CBW works.
  const bool next = cbw(0, false, 0) && csw(0, 0);
  if (!next) Serial.println("NEXT_FAILED");
  ok &= next;
  ok &= reset();
  return ok;
}
bool invalidCbw(unsigned variant) {
  memset(output->data_buffer, 0, 31);
  putLe(output->data_buffer, variant == 0 ? 0 : 0x43425355);
  output->data_buffer[14] = 6;
  if (variant == 1) output->data_buffer[12] = 1;
  if (variant == 2) output->data_buffer[13] = 0xf0;
  if (variant == 3) output->data_buffer[14] = 0;
  if (variant == 4) output->data_buffer[14] = 17;
  const unsigned bytes = variant == 5 ? 30 : 31;
  bool ok = bulk(false, bytes) && actual == bytes;
  events(5);
  for (uint8_t ep : {endpoint, outEndpoint}) {
    ok &= request(0x82, 0, 0, 2, ep) && (control->data_buffer[8] & 1);
    ok &= clear(ep);
    ok &= request(0x82, 0, 0, 2, ep) && (control->data_buffer[8] & 1);
  }
  // RESET must preserve halt until the subsequent CLEAR_FEATURE requests.
  ok &= request(0x21, 255, 0, 0) && request(0x21, 255, 0, 0);
  for (uint8_t ep : {endpoint, outEndpoint}) {
    ok &= request(0x82, 0, 0, 2, ep) && (control->data_buffer[8] & 1);
    ok &= clear(ep);
  }
  ok &= reclaim();
  const bool next = cbw(0, false, 0) && csw(0, 0);
  if (!next) Serial.println("NEXT_FAILED");
  ok &= next;
  ok &= reset();
  return ok;
}
bool alternate() {return request(1,11,0,0) && reclaim();}
bool nextCommand() {return cbw(0,false,0) && csw(0,0);}
bool interfaceCheck() {
  bool ok=reset();
  // Command, pending CSW, partially completed READ, and partially committed WRITE.
  for(unsigned phase=0;phase<(selectedInterface ? 3u:4u);++phase) {
    if(phase==1)ok &= cbw(0,false,0);
    if(phase==2)ok &= cbw(0x28,true,sectorBytes,1,4) && bulk(true,64) && actual==64;
    if(phase==3) {
      ok &= cbw(0x2a,false,sectorBytes,1,4);
      memset(output->data_buffer,0x91,64);ok &= bulk(false,64) && actual==64;
    }
    events(5);
    ok &= alternate() && nextCommand();
    if(!ok){Serial.printf("INTERFACE_PHASE %u\r\n",phase);return false;}
  }
  if(!selectedInterface) {
    ok &= cbw(0x28,true,sectorBytes,1,4) && bulk(true,sectorBytes) && actual==sectorBytes;
    for(unsigned i=0;i<sectorBytes;++i)ok &= input->data_buffer[i]==(i<64 ? 0x91:0);
    ok &= csw(0,0);
  }
  if(!ok){Serial.println("INTERFACE_WRITE_COMMIT");return false;}
  // SET_INTERFACE is not BOT Reset Recovery for an invalid CBW.
  memset(output->data_buffer,0,31);ok &= bulk(false,31) && actual==31;events(5);
  ok &= alternate();
  for(uint8_t ep:{endpoint,outEndpoint}) {
    ok &= request(0x82,0,0,2,ep) && (control->data_buffer[8]&1);
    ok &= clear(ep);
    ok &= request(0x82,0,0,2,ep) && (control->data_buffer[8]&1);
  }
  // A new interface selection during class reset still requires both clears.
  ok &= request(0x21,255,0,0) && alternate();
  for(uint8_t ep:{endpoint,outEndpoint}) {
    ok &= request(0x82,0,0,2,ep) && (control->data_buffer[8]&1);
    ok &= clear(ep);
  }
  return ok && reclaim() && nextCommand();
}
bool senseCheck() {
  bool ok = cbw(0xff, false, 0) && csw(1, 0);
  ok &= cbw(3, true, 18) && bulk(true, 18) && actual == 18;
  ok &= input->data_buffer[2] == 5 && input->data_buffer[12] == 0x20;
  ok &= csw(0, 0);
  ok &= cbw(3, true, 18) && bulk(true, 18) && actual == 18 && !input->data_buffer[2];
  ok &= csw(0, 0);
  return ok;
}

bool errorSense(bool write) {
  bool ok = reset() && cbw(write ? 0x2a : 0x28, !write, 512);
  memset(output->data_buffer, 0x77, 512);
  ok &= !bulk(!write, 512) && lastStatus == USB_TRANSFER_STATUS_STALL;
  ok &= clear(endpoint) && clear(outEndpoint) && reclaim();
  ok &= csw(1, 512);
  ok &= cbw(3, true, 18) && bulk(true, 18) && actual == 18;
  ok &= input->data_buffer[2] == (write ? 7 : 3) && input->data_buffer[12] == (write ? 0x27 : 0x11);
  ok &= csw(0, 0);
  return ok;
}
bool checkRead() {
  bool ok = reset() && cbw(0x28, true, 512) && bulk(true, 512) && actual == 512;
  for (unsigned i = 0; i < 512; ++i) ok &= input->data_buffer[i] == 0x6a;
  ok &= csw(0, 0); return ok;
}

bool isolated(bool second) {
  bool ok = cbw(0x25, true, 8) && bulk(true, 8) && actual == 8;
  ok &= input->data_buffer[3] == (second ? 7 : 31) && input->data_buffer[6] == 2 && !input->data_buffer[7];
  ok &= csw(0, 0);
  ok &= cbw(0x28, true, 512) && bulk(true, 512) && actual == 512;
  for (unsigned i = 0; i < 512; ++i) ok &= input->data_buffer[i] == (second ? 0xa5 : 0);
  ok &= csw(0, 0); return ok;
}

bool sectorGuard() {
  bool ok = reset();
  for (unsigned lba : {1u, 2u}) {
    ok &= cbw(0x28, true, sectorBytes, 1, lba) && bulk(true, sectorBytes) && actual == sectorBytes;
    for (unsigned i = 0; i < sectorBytes; ++i) ok &= input->data_buffer[i] == (lba == 1 ? 0x6a : 0);
    ok &= csw(0, 0);
  }
  return ok;
}

void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (client) usb_host_client_handle_events(client, 0);
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start" || command.startsWith("start ")) {
    selectedInterface = command == "start" ? 0 : command.substring(6).toInt();
    sectorBytes = 512;
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
    if (ok) ok = usb_host_transfer_alloc(72, 0, &control) == ESP_OK && usb_host_transfer_alloc(4096, 0, &input) == ESP_OK && usb_host_transfer_alloc(4096, 0, &output) == ESP_OK;
    controlDone.store(true);
    Serial.printf("HOST_READY %u\r\n", unsigned(ok));
  } else if (command.startsWith("sector ")) {
    sectorBytes = command.substring(7).toInt(); Serial.println("GEOMETRY_SET");
  } else if (command.startsWith("case ")) {
    unsigned n = command.substring(5).toInt();
    Serial.printf("CASE %u ok=%u\r\n", n, unsigned(n >= 1 && n <= 13 && matrixCase(n)));
  } else if (command.startsWith("invalid ")) {
    const unsigned variant = command.substring(8).toInt();
    Serial.printf("INVALID_CBW %u variant=%u\r\n", unsigned(invalidCbw(variant)), variant);
  } else if (command == "faultread" || command == "protected") {
    Serial.printf("ERROR_SENSE %u\r\n", unsigned(errorSense(command == "protected")));
  } else if (command == "check") {
    Serial.printf("READ_RECOVERED %u\r\n", unsigned(checkRead()));
  } else if (command == "guard") {
    Serial.printf("SECTOR_GUARD %u\r\n", unsigned(sectorGuard()));
  } else if (command == "primary" || command == "second") {
    Serial.printf("ISOLATED %u\r\n", unsigned(isolated(command == "second")));
  } else if (command == "interface") {
    Serial.printf("INTERFACE_CHECK %u\r\n",unsigned(interfaceCheck()));
  } else if (command == "sense") {
    Serial.printf("SENSE_CHECK %u\r\n", unsigned(senseCheck()));
  } else if (command == "stop") {
    Serial.println(!client && !running.load() ? "STOPPED" : stop() ? "STOPPED" : "STOP_FAILED");
  }
}
