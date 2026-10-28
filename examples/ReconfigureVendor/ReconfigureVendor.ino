#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
size_t capacity = 128;
uint8_t pending[64];
size_t length = 0, sent = 0;

bool start(size_t bytes) {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = buffers.txCapacity = bytes;
  buffers.transferBytes = 64;
  const esp_err_t error = vendor.configure(buffers);
  if (error != ESP_OK) {
    Serial.println(esp_err_to_name(error));
    return false;
  }
  if (!device.begin()) {
    Serial.println(device.lastErrorName());
    return false;
  }
  Serial.printf("Started: RX=%u TX=%u\n",
      unsigned(vendor.readCapacity()), unsigned(vendor.writeCapacity()));
  return true;
}

void setup() {
  Serial.begin(115200);
  start(capacity);
  Serial.println("Send r on the separate debug port to switch 128/2048-byte rings.");
}

void loop() {
  if (Serial.available() && Serial.read() == 'r') {
    // The Host observes disconnect/re-enumeration. Old stream data is discarded.
    const esp_err_t error = device.end();
    if (error == ESP_OK) {
      length = sent = 0;
      capacity = capacity == 128 ? 2048 : 128;
      start(capacity);
    } else Serial.println(esp_err_to_name(error));
  }
  if (!device.ready()) { length = sent = 0; delay(1); return; }
  if (!length) { length = vendor.read(pending, sizeof(pending)); sent = 0; }
  if (length) {
    sent += vendor.write(pending + sent, length - sent);
    if (sent == length) length = sent = 0;
  }
  delay(1);
}
