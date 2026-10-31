// USB CDC ACM serial port: echoes characters, sends a periodic tick, and logs
// the Host's line coding, DTR/RTS and break requests on the debug Serial.
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceCdcSerial usbSerial(device, "EspUsbDevice Serial");

uint32_t lastTickMs = 0;
// Written on the USB worker, read in loop().
volatile uint32_t lineCodingCount = 0, lineStateCount = 0;

void printLineCoding(const EspUsbDeviceCdcLineCoding &coding) {
  Serial.printf("CDC_LINE_CODING baud=%lu stop=%u parity=%u data=%u\n",
      (unsigned long)coding.baud, coding.stopBits, coding.parity, coding.dataBits);
}

void setup() {
  Serial.begin(115200);  // Debug port; separate from the USB CDC data port.
  delay(1500);

  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = 256;
  buffers.txCapacity = 1024;
  // Callbacks run on the USB worker: keep them short, never block or end().
  // Like configure(), they are accepted only while the device is stopped.
  if (usbSerial.configure(buffers) != ESP_OK ||
      usbSerial.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding) {
        lineCodingCount++;
        printLineCoding(coding);
      }) != ESP_OK ||
      usbSerial.onLineState([](const EspUsbDeviceCdcLineState &state) {
        lineStateCount++;
        Serial.printf("CDC_LINE_STATE dtr=%u rts=%u\n", state.dtr, state.rts);
      }) != ESP_OK ||
      usbSerial.onBreak([](uint16_t durationMs) {
        Serial.printf("CDC_BREAK duration=%u\n", durationMs);
      }) != ESP_OK ||
      usbSerial.onRx([](size_t available) {
        Serial.printf("CDC_RX available=%u\n", unsigned(available));
      }) != ESP_OK) {
    Serial.println("CDC configuration rejected");
    return;
  }
  // write()/flush() wait for room up to this limit (default 1000 ms).
  usbSerial.setTxTimeoutMs(100);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4016;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Serial";
  config.serialNumber = "espusb-serial";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("USB CDC serial ready rx=%u tx=%u\n",
      unsigned(usbSerial.readCapacity()), unsigned(usbSerial.writeCapacity()));
}

void loop() {
  // Bytes arrive as a stream; a line or packet boundary is not preserved.
  while (usbSerial.available() > 0) {
    const int value = usbSerial.read();
    if (value < 0) break;
    const char c = char(value);
    if (c == '\r') continue;
    if (c == '\n') {
      usbSerial.print("\r\n");
      continue;
    }
    usbSerial.print("echo: ");
    usbSerial.write(uint8_t(c));
    usbSerial.print("\r\n");
  }

  const uint32_t now = millis();
  // connected() means configured and DTR=1 (a terminal has the port open).
  if (usbSerial.connected() && now - lastTickMs >= 3000) {
    lastTickMs = now;
    const EspUsbDeviceCdcLineCoding coding = usbSerial.lineCoding();  // a copy
    usbSerial.printf("tick=%lu baud=%lu line_coding=%lu line_state=%lu\r\n",
        (unsigned long)(now / 1000), (unsigned long)coding.baud,
        (unsigned long)lineCodingCount, (unsigned long)lineStateCount);
    usbSerial.flush();  // Waits for the DMA/ZLP, not for the PC application.
  }
  delay(1);
}
