#include <EspUsbDevice.h>
#include <string.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial console(device, "Composite console");
EspUsbDeviceMsc msc(device);

// 48 KiB FAT12 RAM disk, small enough to fit the ESP32-S2's DRAM next to the
// other Functions. Its contents are lost on reset.
uint8_t storage[48 * 1024];
EspUsbDeviceMscFatRamDisk disk(storage, sizeof(storage));
char line[128];
size_t lineLength = 0;

// "type <text>" types <text> on the keyboard; anything else is echoed.
void handleLine(const char *text) {
  if (strncmp(text, "type ", 5) == 0) {
    if (keyboard.write(text + 5) && keyboard.tapKey('\n')) console.printf("typed: %s\r\n", text + 5);
    else console.printf("type failed: %s\r\n", device.lastErrorName());
    return;
  }
  console.printf("echo: %s\r\n", text);
}

void setup() {
  Serial.begin(115200); // Separate debug connection.

  // MSC: format the volume before USB exposes it.
  if (!disk.format("ESPUSB") ||
      !disk.addTextFile("README.TXT",
          "EspUsbDevice composite device\r\n\r\n"
          "This drive is also a USB keyboard and a USB serial port.\r\n"
          "Open the serial port and send: type Hello\r\n")) {
    Serial.println("FAT RAM disk failed");
    return;
  }
  msc.vendorID("ESP32");
  msc.productID("COMPOSITE");
  msc.productRevision("1.0");
  msc.mediaPresent(true);
  msc.isWritable(true);
  EspUsbMscConfig mscConfig;
  mscConfig.transferBytes = 512; // A multiple of the bulk packet at FS and HS.
  esp_err_t error = msc.configure(mscConfig);
  if (error != ESP_OK || !disk.attach(msc)) {
    Serial.printf("MSC setup failed: %s\n", esp_err_to_name(error != ESP_OK ? error : msc.startError()));
    return;
  }

  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  // CDC: a short receive ring for command lines, a larger one for replies.
  EspUsbBulkStreamConfig serialConfig;
  serialConfig.rxCapacity = 256;
  serialConfig.txCapacity = 512;
  serialConfig.transferBytes = 64;
  error = console.configure(serialConfig);
  if (error == ESP_OK) {
    // Runs on the USB worker; Serial is the separate debug connection.
    error = console.onLineState([](const EspUsbDeviceCdcLineState &state) {
      Serial.printf("CDC dtr=%u rts=%u\n", unsigned(state.dtr), unsigned(state.rts));
    });
  }
  if (error != ESP_OK) {
    Serial.printf("CDC setup failed: %s\n", esp_err_to_name(error));
    return;
  }

  EspUsbDeviceConfig identity;
  identity.pid = 0x4030;
  identity.manufacturer = "EspUsb";
  identity.product = "EspUsbDevice Composite";
  identity.serialNumber = "espusb-composite";
  // One begin() starts all three Functions as one composite device.
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB composite (HID keyboard + CDC serial + MSC) ready");
}

void loop() {
  // Assemble CDC input into lines; longer lines are cut at the buffer size.
  while (console.available() > 0) {
    const int value = console.read();
    if (value < 0) break;
    const char c = char(value);
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLength] = '\0';
      handleLine(line);
      lineLength = 0;
    } else if (lineLength < sizeof(line) - 1) {
      line[lineLength++] = c;
    }
  }
  // Heartbeat, only while a terminal holds the port open (DTR).
  static uint32_t lastTick = 0;
  const uint32_t now = millis();
  if (console.connected() && uint32_t(now - lastTick) >= 5000) {
    lastTick = now;
    console.printf("tick=%lu (send 'type <text>' to use the keyboard)\r\n", static_cast<unsigned long>(now / 1000));
    console.flush();
  }
  delay(1);
}
