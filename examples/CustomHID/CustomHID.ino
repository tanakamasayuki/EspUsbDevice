#include <EspUsbDevice.h>

// One 8-byte Input report without a report ID. The library reads the report
// sizes from this descriptor at begin(), so it must stay valid and unchanged
// while the device runs.
const uint8_t reportDescriptor[] = {
    0x06, 0x00, 0xff, // Usage Page (Vendor Defined 0xff00)
    0x09, 0x01,       // Usage (1)
    0xa1, 0x01,       // Collection (Application)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xff, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x08,       //   Report Count (8)
    0x09, 0x01,       //   Usage (1)
    0x81, 0x02,       //   Input (Data,Var,Abs)
    0xc0,             // End Collection
};

EspUsbDevice device;
// The last argument is the interrupt IN packet size to declare (wMaxPacketSize),
// not a buffer size; the report storage comes from the descriptor.
EspUsbDeviceHidCustom customHid(device, reportDescriptor, sizeof(reportDescriptor), 8);
uint8_t counter = 0;

void sendCustomReport() {
  const uint32_t now = millis();
  const uint8_t report[8] = {'E', 'S', 'P', counter, uint8_t(now), uint8_t(now >> 8), 0xaa, 0x55};
  // Report ID 0: this descriptor declares none. Pass the ID for one that does.
  if (customHid.sendReport(report, sizeof(report))) {
    Serial.printf("CUSTOM_HID_TX counter=%u\n", counter);
    counter++;
  } else {
    Serial.printf("CUSTOM_HID_FAILED %s\n", device.lastErrorName());
  }
}

void setup() {
  Serial.begin(115200);
  EspUsbDeviceConfig config;
  config.pid = 0x4004;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Custom HID";
  config.serialNumber = "espusb-custom-hid";
  // A malformed descriptor is rejected here, before the Host sees it.
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s (HID %s)\n", device.lastErrorName(),
        esp_err_to_name(customHid.startError()));
    return;
  }
  Serial.printf("USB custom HID ready (input=%u output=%u feature=%u bytes)\n",
      unsigned(customHid.inputReportCapacity()), unsigned(customHid.outputReportCapacity()),
      unsigned(customHid.featureReportCapacity()));
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (device.ready() && uint32_t(now - last) >= 1000) {
    last = now;
    sendCustomReport();
  }
  delay(1);
}
