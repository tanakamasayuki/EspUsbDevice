#include <EspUsbDevice.h>

EspUsbDevice device;
// Payload bytes per report, excluding the report ID. 63 fills one full-speed
// packet; larger values (up to maxReportSize()) are split over several packets
// at FS and use up to 1024-byte packets at HS.
constexpr uint16_t reportBytes = 63;
EspUsbDeviceHidVendor vendor(device, reportBytes);
uint32_t inputCounter = 0;

// report.data is borrowed for the duration of the callback only.
void printReport(const char *prefix, const EspUsbDeviceHidReport &report) {
  Serial.printf("%s report_id=%u type=%u len=%u data=", prefix,
      report.reportId, report.reportType, report.length);
  for (uint16_t i = 0; i < report.length && report.data; ++i) {
    Serial.printf(i + 1 < report.length ? "%02x " : "%02x", report.data[i]);
  }
  Serial.println();
}

void sendInputReport() {
  uint8_t payload[reportBytes] = {'E', 'S', 'P', uint8_t(inputCounter),
      uint8_t(inputCounter >> 8), uint8_t(millis() / 1000)};
  if (vendor.sendInput(payload, sizeof(payload))) {
    Serial.printf("VENDOR_INPUT counter=%lu\n", (unsigned long)inputCounter);
    inputCounter++;
  } else {
    Serial.printf("VENDOR_INPUT_FAILED %s\n", device.lastErrorName());
  }
}

void setup() {
  Serial.begin(115200);
  // Both run on the USB worker. Register before begin(); afterwards they
  // return ESP_ERR_INVALID_STATE.
  if (vendor.onOutputReport([](const EspUsbDeviceHidReport &r) { printReport("VENDOR_OUTPUT", r); }) != ESP_OK ||
      vendor.onFeatureReport([](const EspUsbDeviceHidReport &r) { printReport("VENDOR_FEATURE", r); }) != ESP_OK) {
    Serial.println("CALLBACK_REGISTER_FAILED");
    return;
  }

  EspUsbDeviceConfig config;
  config.pid = 0x4005;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Vendor HID";
  config.serialNumber = "espusb-vendor-hid";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s (HID %s)\n", device.lastErrorName(),
        esp_err_to_name(vendor.startError()));
    return;
  }
  // The value a Host GET_REPORT(Feature) reads until it sends its own. A
  // shorter payload is stored shorter, not zero-padded, so send the full size.
  const uint8_t feature[reportBytes] = {'E', 'S', 'P', 3};
  const esp_err_t error = vendor.setFeatureReport(feature, sizeof(feature));
  if (error != ESP_OK) Serial.printf("FEATURE_SET_FAILED %s\n", esp_err_to_name(error));
  // Capacities include the report ID byte.
  Serial.printf("USB vendor HID ready (payload=%u input=%u output=%u feature=%u bytes)\n",
      vendor.reportSize(), unsigned(vendor.inputReportCapacity()),
      unsigned(vendor.outputReportCapacity()), unsigned(vendor.featureReportCapacity()));
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (device.ready() && uint32_t(now - last) >= 1000) {
    last = now;
    sendInputReport();
  }
  delay(1);
}
