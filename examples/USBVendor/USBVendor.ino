// Vendor-specific interface (class 0xff): echoes bulk OUT data on bulk IN,
// answers two vendor control requests and advertises a WebUSB landing page.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVendor vendor(device);

// The library answers WebUSB GET_URL (0x01, wIndex 2, when enabled) and the
// Microsoft OS 2.0 set (0x02, wIndex 7); other requests with those codes still
// reach handleControl(). This protocol uses its own codes to stay clear of both.
constexpr uint8_t REQUEST_GET_INFO = 0x10;     // IN: returns infoText
constexpr uint8_t REQUEST_SET_COMMAND = 0x11;  // OUT: up to 64 data bytes
const char infoText[] = "EspUsbDeviceVendor";
uint8_t commandBuffer[64];  // Must stay valid until the control transfer ends.

volatile size_t rxNotified = 0;  // Written on the USB worker.
uint32_t rxCount = 0, lastStatusMs = 0;
volatile uint32_t controlCount = 0;

void printHex(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) Serial.printf(i ? " %02x" : "%02x", data[i]);
}

bool handleControl(const EspUsbDeviceVendorControlRequest &r) {
  Serial.printf("VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=%u index=%u length=%u actual=%u\n",
      r.stage, r.bmRequestType, r.bRequest, r.wValue, r.wIndex, r.wLength, r.actualBytes);
  const bool in = r.bmRequestType & 0x80;
  if (in && r.bRequest == REQUEST_GET_INFO) {
    if (r.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP) return true;
    controlCount++;
    // A reply longer than wLength is cut to wLength by the transfer.
    return vendor.sendControlResponse(r, infoText,
        min(size_t(r.wLength), sizeof(infoText) - 1));
  }
  if (!in && r.bRequest == REQUEST_SET_COMMAND && r.wLength <= sizeof(commandBuffer)) {
    switch (r.stage) {
    case ESP_USB_DEVICE_CONTROL_STAGE_SETUP:
      // With wLength 0 this sends only the status stage; otherwise it receives.
      return vendor.sendControlResponse(r, commandBuffer, r.wLength);
    case ESP_USB_DEVICE_CONTROL_STAGE_DATA:
      return r.actualBytes == r.wLength;  // Validate here; false stalls.
    default:  // ACK: the transfer succeeded, apply it.
      controlCount++;
      Serial.print("VENDOR_COMMAND ");
      printHex(commandBuffer, r.actualBytes);
      Serial.println();
      return true;
    }
  }
  return false;  // Stall anything this protocol does not define.
}

void setup() {
  Serial.begin(115200);  // Debug port
  delay(1500);

  EspUsbVendorConfig buffers;
  buffers.rxCapacity = 512;
  buffers.txCapacity = 1024;
  // Callbacks run on the USB worker and are accepted only while stopped.
  if (vendor.configure(buffers) != ESP_OK ||
      vendor.onRx([](size_t available) { rxNotified = available; }) != ESP_OK ||
      vendor.onControlRequest(handleControl) != ESP_OK) {
    Serial.println("Vendor configuration rejected");
    return;
  }
  // Also show the WebUSB / Microsoft OS 2.0 requests the library answers itself.
  device.onAnyControlRequest([](const EspUsbDeviceControlRequestInfo &info) {
    if ((info.bmRequestType & 0x60) != 0x40) return;  // vendor requests only
    Serial.printf("LIBRARY_CONTROL stage=%u request=0x%02x index=%u handled=%u bytes=%u\n",
        info.stage, info.bRequest, info.wIndex, info.handled, info.responseLength);
  });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice USBVendor";
  config.serialNumber = "espusb-vendor";
  config.webusbEnabled = true;
  config.webusbUrl = "https://example.com/espusbdevice";
  // What a Windows application passes to SetupDiGetClassDevs. Make your own.
  config.deviceInterfaceGuid = "{8F7D3A4E-2C1B-4E5F-9A6D-0B1C2D3E4F50}";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("USB vendor device ready rx=%u tx=%u\n",
      unsigned(vendor.readCapacity()), unsigned(vendor.writeCapacity()));
}

void loop() {
  if (rxNotified) {
    Serial.printf("VENDOR_RX_NOTIFY available=%u\n", unsigned(rxNotified));
    rxNotified = 0;
  }

  // Bulk data is a byte stream: a chunk is not one Host write.
  uint8_t buffer[64];
  const size_t chunk = vendor.read(buffer, sizeof(buffer));
  if (chunk) {
    rxCount += chunk;
    Serial.printf("VENDOR_RX %u ", unsigned(chunk));
    printHex(buffer, chunk);
    Serial.println();
    // Wait for room for the whole reply so it is never cut short.
    if (vendor.waitWritable(chunk + 8)) {
      vendor.print("echo: ");
      vendor.write(buffer, chunk);
      vendor.print("\r\n");
    } else {
      Serial.println("VENDOR_TX no room; the Host is not reading bulk IN");
    }
  }

  const uint32_t now = millis();
  if (vendor.mounted() && now - lastStatusMs >= 3000) {
    lastStatusMs = now;
    if (vendor.writeAvailable() >= 48) {
      vendor.printf("status rx=%lu control=%lu\r\n", (unsigned long)rxCount,
          (unsigned long)controlCount);
    }
  }
  delay(1);
}
