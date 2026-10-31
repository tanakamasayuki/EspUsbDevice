// Several independent USB CDC ACM ports on one device. Each port has its own
// rings, line coding and DTR; the Host sees one COM port / ttyACM per port.
#include <EspUsbDevice.h>

EspUsbDevice device;
// Distinct names let the Host and the user tell otherwise identical ports apart.
EspUsbDeviceCdcSerial consolePort(device, "Console");
EspUsbDeviceCdcSerial dataPort(device, "Data Link");
// Each CDC port uses two IN endpoints. S2/S3 and the P4 full-speed controller
// have four, so two ports; the P4 high-speed controller (Auto) has seven.
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#define SERIAL_MULTI_HAS_THIRD_PORT 1
EspUsbDeviceCdcSerial telemetryPort(device, "Telemetry");
#else
#define SERIAL_MULTI_HAS_THIRD_PORT 0
#endif

struct Port {
  EspUsbDeviceCdcSerial &serial;
  const char *label;
  size_t rxCapacity, txCapacity;
};

Port ports[] = {
    {consolePort, "CONSOLE", 128, 1024},
    {dataPort, "DATALINK", 1024, 1024},
#if SERIAL_MULTI_HAS_THIRD_PORT
    {telemetryPort, "TELEMETRY", 64, 2048},
#endif
};

uint32_t lastTickMs = 0;

bool configurePort(Port &port) {
  EspUsbBulkStreamConfig buffers;
  buffers.rxCapacity = port.rxCapacity;
  buffers.txCapacity = port.txCapacity;
  const char *label = port.label;  // the lambda copies the pointer
  if (port.serial.configure(buffers) != ESP_OK ||
      port.serial.onLineState([label](const EspUsbDeviceCdcLineState &state) {
        Serial.printf("%s dtr=%u rts=%u\n", label, state.dtr, state.rts);
      }) != ESP_OK ||
      port.serial.onLineCoding([label](const EspUsbDeviceCdcLineCoding &coding) {
        Serial.printf("%s baud=%lu\n", label, (unsigned long)coding.baud);
      }) != ESP_OK) {
    Serial.printf("%s configuration rejected\n", label);
    return false;
  }
  port.serial.setTxTimeoutMs(50);  // One stalled terminal must not stall the others.
  return true;
}

void echo(Port &port) {
  while (port.serial.available() > 0) {
    const int value = port.serial.read();
    if (value < 0) break;
    const char c = char(value);
    if (c == '\r') continue;
    if (c == '\n') {
      port.serial.print("\r\n");
      continue;
    }
    Serial.printf("%s RX %c\n", port.label, c);
    port.serial.printf("%s: %c\r\n", port.label, c);
  }
}

void setup() {
  Serial.begin(115200);  // Debug port; separate from the USB CDC data ports.
  delay(1500);
  for (Port &port : ports) {
    if (!configurePort(port)) return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4018;
  // Auto selects the P4 high-speed controller, which has the endpoints for the
  // third port. FullSpeed there makes begin() fail with ESP_ERR_INVALID_SIZE.
  config.controller = EspUsbController::Auto;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice DualSerial";
  // A stable serial number lets the Host keep port names across replugs.
  config.serialNumber = "espusb-dualserial-0001";
  if (!device.begin(config)) {
    // The whole configuration, including the endpoint budget, is checked here.
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  for (Port &port : ports) {
    Serial.printf("USB CDC ready: %s=port%u rx=%u tx=%u\n", port.label,
        port.serial.port(), unsigned(port.serial.readCapacity()),
        unsigned(port.serial.writeCapacity()));
  }
}

void loop() {
  for (Port &port : ports) echo(port);

  const uint32_t now = millis();
  if (now - lastTickMs >= 3000) {
    lastTickMs = now;
    for (Port &port : ports) {
      if (port.serial.connected()) {
        port.serial.printf("%s tick=%lu\r\n", port.label, (unsigned long)(now / 1000));
      }
    }
  }
  delay(1);
}
