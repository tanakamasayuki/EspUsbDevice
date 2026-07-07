#include "EspUsbDevice.h"

// Composite (all-in-one) USB device: HID keyboard + CDC ACM serial + MSC FAT
// RAM disk on a single EspUsbDevice. Register each function with the same
// EspUsbDevice instance and call begin() once; the library assigns interface
// numbers and endpoints and builds the composite configuration descriptor.
//
// This is the richest composite that fits the ESP32-S3 USB endpoint budget
// (three FIFO-consuming IN endpoints: keyboard, CDC data-in, MSC bulk-in).
// Adding a fourth FIFO-IN class (e.g. MIDI or bulk Vendor) exceeds the S3 FIFO
// budget and the device fails to enumerate. See docs/DESIGN_NOTES.ja.md
// "複合時の endpoint 予算の上限".

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial UsbSerial(device);
EspUsbDeviceMsc msc(device);

static uint8_t storage[96 * 1024];
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));

static char lineBuffer[128];
static size_t lineLength = 0;

// Handle one complete line received over USB CDC. "type <text>" is typed out on
// the HID keyboard; anything else is echoed back over CDC.
static void handleCdcLine(const char *line)
{
  if (strncmp(line, "type ", 5) == 0)
  {
    if (keyboard.write(line + 5) && keyboard.tapKey('\n'))
    {
      UsbSerial.printf("typed: %s\r\n", line + 5);
    }
    else
    {
      UsbSerial.printf("type_failed error=%s\r\n", device.lastErrorName());
    }
    return;
  }
  UsbSerial.printf("echo: %s\r\n", line);
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  // --- MSC: a FAT-formatted RAM disk that shows up as a removable drive. ---
  if (!disk.format("ESPUSB"))
  {
    Serial.println("FAT_FORMAT_FAILED");
    return;
  }
  disk.addTextFile("README.TXT",
                   "EspUsbDevice composite device\r\n"
                   "\r\n"
                   "This drive is also a USB keyboard and a USB serial port.\r\n"
                   "Open the serial port and send: type Hello\r\n");
  msc.vendorID("ESP32");
  msc.productID("COMPOSITE");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);
  if (!disk.attach(msc))
  {
    Serial.println("MSC_ATTACH_FAILED");
    return;
  }

  // --- HID keyboard ---
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // --- CDC serial ---
  UsbSerial.onLineState([](const EspUsbDeviceCdcLineState &state)
                        { Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n",
                                        state.dtr ? 1 : 0, state.rts ? 1 : 0); });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4030;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Composite";
  config.serialNumber = "espusb-composite";

  // A single begin() brings up all three functions as one composite device.
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB composite (HID keyboard + CDC serial + MSC) ready");
}

void loop()
{
  // Assemble CDC input into lines and act on each completed line.
  while (UsbSerial.available() > 0)
  {
    const int value = UsbSerial.read();
    if (value < 0)
    {
      break;
    }
    const char c = static_cast<char>(value);
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      lineBuffer[lineLength] = '\0';
      handleCdcLine(lineBuffer);
      lineLength = 0;
      continue;
    }
    if (lineLength < sizeof(lineBuffer) - 1)
    {
      lineBuffer[lineLength++] = c;
    }
  }

  // Periodic heartbeat over CDC so a connected host sees the device is alive.
  static uint32_t lastTickMs = 0;
  const uint32_t now = millis();
  if (UsbSerial.connected() && now - lastTickMs >= 5000)
  {
    lastTickMs = now;
    UsbSerial.printf("tick=%lu (send 'type <text>' to use the keyboard)\r\n",
                     static_cast<unsigned long>(now / 1000));
    UsbSerial.flush();
  }

  delay(1);
}
