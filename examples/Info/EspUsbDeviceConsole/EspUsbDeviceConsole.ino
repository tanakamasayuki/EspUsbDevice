// Drive HID and Vendor traffic by hand from the debug Serial console, and log
// everything the Host sends down (HID output/protocol, vendor control, bulk OUT).
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
EspUsbDeviceVendor vendor(device);

bool beginOk = false, lastReady = false;
String line;
uint8_t controlOutBuffer[64];  // Vendor control OUT data; valid until the ACK.

void printHex(const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) Serial.printf(i ? " %02x" : "%02x", data[i]);
}

// Splits off the first space-delimited token; `text hello world` keeps the rest.
String nextToken(String &rest) {
  rest.trim();
  const int space = rest.indexOf(' ');
  if (space < 0) {
    const String token = rest;
    rest = "";
    return token;
  }
  const String token = rest.substring(0, space);
  rest = rest.substring(space + 1);
  rest.trim();
  return token;
}

// Accepts `0x04` and `4`, as captures and datasheets mix both.
bool parseNumber(const String &text, long &value) {
  if (text.length() == 0) return false;
  char *end = nullptr;
  const bool hex = text.startsWith("0x") || text.startsWith("0X");
  value = strtol(text.c_str() + (hex ? 2 : 0), &end, hex ? 16 : 10);
  return end && *end == '\0';
}

size_t parseHexBytes(String rest, uint8_t *buffer, size_t capacity) {
  size_t count = 0;
  while (count < capacity) {
    const String token = nextToken(rest);
    if (token.length() == 0) break;
    long value = 0;
    const String withPrefix =
        (token.startsWith("0x") || token.startsWith("0X")) ? token : "0x" + token;
    if (!parseNumber(withPrefix, value) || value < 0 || value > 0xff) {
      Serial.printf("ERROR not a byte: %s\n", token.c_str());
      return 0;
    }
    buffer[count++] = uint8_t(value);
  }
  return count;
}

void printHelp() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  state");
  Serial.println("  text <string>");
  Serial.println("  key <usage> [modifiers]");
  Serial.println("  hold <usage> [modifiers]");
  Serial.println("  release");
  Serial.println("  mouse <dx> <dy> [buttons] [wheel]");
  Serial.println("  click <1|2|4>");
  Serial.println("  report <id> <hex...>");
  Serial.println("  vendor <hex...>");
  Serial.println("  vendortext <string>");
}

void printState() {
  Serial.printf("STATE mounted=%u suspended=%u leds=0x%02x hid_protocol=%s vendor_mounted=%u vendor_tx_free=%u\n",
      device.ready(), device.suspended(), keyboard.ledState().leds,
      keyboard.protocol() ? "report" : "boot", vendor.mounted(),
      unsigned(vendor.writeAvailable()));
}

void handleCommand(String input) {
  String rest = input;
  const String verb = nextToken(rest);
  if (verb.length() == 0) return;
  if (verb == "help") return printHelp();
  if (verb == "state") return printState();

  // Everything below sends something; nothing is queued for an unconfigured Host.
  if (!device.ready()) {
    Serial.println("ERROR not mounted - the host has not enumerated this device");
    return;
  }
  if (verb == "text") {
    // Keystrokes, not serial text: they land in whatever has focus on the Host.
    Serial.printf("SEND text \"%s\" ok=%u\n", rest.c_str(), keyboard.write(rest.c_str()));
    return;
  }
  if (verb == "key" || verb == "hold") {
    long usage = 0, modifiers = 0;
    if (!parseNumber(nextToken(rest), usage)) {
      Serial.println("ERROR usage: key <usage> [modifiers]");
      return;
    }
    const String modifierToken = nextToken(rest);
    if (modifierToken.length() > 0 && !parseNumber(modifierToken, modifiers)) {
      Serial.println("ERROR modifiers must be a number");
      return;
    }
    const bool ok = verb == "key" ? keyboard.tapUsage(uint8_t(usage), uint8_t(modifiers))
                                  : keyboard.pressUsage(uint8_t(usage), uint8_t(modifiers));
    Serial.printf("SEND %s usage=0x%02x modifiers=0x%02x ok=%u\n", verb.c_str(),
        unsigned(usage), unsigned(modifiers), ok);
    return;
  }
  if (verb == "release") {
    Serial.printf("SEND release ok=%u\n", keyboard.releaseAll());
    return;
  }
  if (verb == "mouse") {
    long dx = 0, dy = 0, buttons = 0, wheel = 0;
    if (!parseNumber(nextToken(rest), dx) || !parseNumber(nextToken(rest), dy)) {
      Serial.println("ERROR usage: mouse <dx> <dy> [buttons] [wheel]");
      return;
    }
    const String buttonToken = nextToken(rest);
    if (buttonToken.length() > 0) parseNumber(buttonToken, buttons);
    const String wheelToken = nextToken(rest);
    if (wheelToken.length() > 0) parseNumber(wheelToken, wheel);
    const bool ok = mouse.move(int8_t(dx), int8_t(dy), int8_t(wheel), uint8_t(buttons));
    Serial.printf("SEND mouse dx=%ld dy=%ld buttons=0x%02x wheel=%ld ok=%u\n", dx, dy,
        unsigned(buttons), wheel, ok);
    return;
  }
  if (verb == "click") {
    long button = 1;
    const String buttonToken = nextToken(rest);
    if (buttonToken.length() > 0 && !parseNumber(buttonToken, button)) {
      Serial.println("ERROR usage: click <1|2|4>");
      return;
    }
    Serial.printf("SEND click buttons=0x%02x ok=%u\n", unsigned(button),
        mouse.click(uint8_t(button)));
    return;
  }
  if (verb == "report") {
    long reportId = 0;
    if (!parseNumber(nextToken(rest), reportId)) {
      Serial.println("ERROR usage: report <id> <hex...>");
      return;
    }
    uint8_t payload[64];
    const size_t length = parseHexBytes(rest, payload, sizeof(payload));
    if (length == 0) {
      Serial.println("ERROR no payload bytes");
      return;
    }
    // Keyboard and mouse share HID interface 0 and differ by report ID
    // (keyboard 1, mouse 2). The payload excludes the report ID byte.
    const bool ok = device.sendHidReport(0, uint8_t(reportId), payload, length);
    Serial.printf("SEND report id=%u length=%u ok=%u error=%s data=", unsigned(reportId),
        unsigned(length), ok, ok ? "ESP_OK" : device.lastErrorName());
    printHex(payload, length);
    Serial.println();
    return;
  }
  if (verb == "vendor" || verb == "vendortext") {
    if (!vendor.mounted()) {
      Serial.println("ERROR vendor interface endpoints are not open");
      return;
    }
    uint8_t payload[64];
    size_t length = 0;
    if (verb == "vendortext") {
      length = min(size_t(rest.length()), sizeof(payload));
      memcpy(payload, rest.c_str(), length);
    } else {
      length = parseHexBytes(rest, payload, sizeof(payload));
    }
    if (length == 0) {
      Serial.println("ERROR no payload bytes");
      return;
    }
    // Accepted bytes, which can be fewer when the Host is not reading bulk IN.
    const size_t written = vendor.write(payload, length);
    Serial.printf("SEND vendor length=%u written=%u data=", unsigned(length),
        unsigned(written));
    printHex(payload, length);
    Serial.println();
    return;
  }
  Serial.printf("ERROR unknown command: %s (try `help`)\n", verb.c_str());
}

bool handleVendorControl(const EspUsbDeviceVendorControlRequest &r) {
  Serial.printf("HOST_VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=0x%04x index=0x%04x length=%u actual=%u\n",
      r.stage, r.bmRequestType, r.bRequest, r.wValue, r.wIndex, r.wLength, r.actualBytes);
  // Answer every request so the Host sees success instead of a stall; edit this
  // to replay the protocol you are studying.
  if (r.stage != ESP_USB_DEVICE_CONTROL_STAGE_SETUP) {
    if (r.stage == ESP_USB_DEVICE_CONTROL_STAGE_ACK && !(r.bmRequestType & 0x80) &&
        r.actualBytes) {
      Serial.print("HOST_VENDOR_CONTROL_DATA ");
      printHex(controlOutBuffer, r.actualBytes);
      Serial.println();
    }
    return true;
  }
  static const char banner[] = "EspUsbDeviceConsole";
  if (r.bmRequestType & 0x80) {
    return vendor.sendControlResponse(r, banner, min(size_t(r.wLength), sizeof(banner) - 1));
  }
  if (r.wLength > sizeof(controlOutBuffer)) return false;  // stall: too long
  return vendor.sendControlResponse(r, controlOutBuffer, r.wLength);
}

void setup() {
  Serial.begin(115200);  // Command console; the USB device port goes to the Host.
  delay(1500);
  Serial.println();
  Serial.println("=== EspUsbDevice console ===");

  // All callbacks run on the USB worker and must be registered before begin().
  EspUsbVendorConfig vendorConfig;
  vendorConfig.rxMode = EspUsbVendorRxMode::Callback;  // bulk OUT straight to a callback
  vendorConfig.rxCapacity = 0;
  vendorConfig.txCapacity = 512;
  const bool configured =
      keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report) {
        Serial.printf("HOST_HID_OUTPUT leds=0x%02x num=%u caps=%u scroll=%u\n",
            report.leds, report.numLock, report.capsLock, report.scrollLock);
      }) == ESP_OK &&
      // BIOS/UEFI select boot protocol; an OS driver switches back to report.
      keyboard.onProtocol([](const EspUsbDeviceHidProtocolEvent &event) {
        Serial.printf("HOST_HID_PROTOCOL instance=%u protocol=%s\n", event.instance,
            event.protocol ? "report" : "boot");
      }) == ESP_OK &&
      vendor.configure(vendorConfig) == ESP_OK &&
      // data is valid only during the call; copy it to keep it.
      vendor.onRxData([](const uint8_t *data, size_t length) {
        Serial.printf("HOST_VENDOR_OUT %u ", unsigned(length));
        printHex(data, length);
        Serial.println();
      }) == ESP_OK &&
      vendor.onControlRequest(handleVendorControl) == ESP_OK;
  if (!configured) {
    Serial.println("Function configuration rejected");
    return;
  }

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4052;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Console";
  config.serialNumber = "espusb-console";
  beginOk = device.begin(config);
  if (!beginOk) {
    Serial.printf("BEGIN failed error=%s\n", device.lastErrorName());
    return;
  }
  Serial.println("BEGIN ok - connect the device connector to the host under test");
  printHelp();
}

void loop() {
  if (beginOk && device.ready() != lastReady) {
    lastReady = device.ready();
    Serial.println(lastReady ? "MOUNTED" : "UNMOUNTED");
  }
  while (Serial.available()) {
    const char c = char(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      handleCommand(line);
      line = "";
      continue;
    }
    if (line.length() < 200) line += c;
  }
  delay(1);
}
