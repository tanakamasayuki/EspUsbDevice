// EspUsbDeviceConsole - drive the USB device by hand from a serial terminal.
//
// The device-side counterpart of a protocol console: instead of rebuilding a
// sketch for every experiment, type a command into the UART serial monitor and
// watch what the host does with it. Useful for
//   - finding out which HID usage a host application actually reacts to,
//   - sending a hand-built raw report before committing to a report descriptor,
//   - watching what the host sends *down* to the device (HID output reports,
//     vendor control requests, vendor bulk OUT) while its own software runs.
//
// Two serial connections are involved. Commands are typed on the UART/JTAG
// serial port used for flashing; the USB device connector goes to the host
// under test. On a board with only one connector, use a peer board or accept
// that the serial log disappears when you unplug.
//
// Commands (type `help` for the same list at runtime):
//   help                       this list
//   state                      mount state, speed, LEDs, HID protocol
//   text <string>              send the string as keystrokes
//   key <usage> [modifiers]    tap one raw HID usage, e.g. `key 0x04 0x02`
//   hold <usage> [modifiers]   press and keep held
//   release                    release everything held
//   mouse <dx> <dy> [buttons] [wheel]
//   click <1|2|4>              mouse left / right / middle
//   report <id> <hex...>       raw HID report, e.g. `report 1 00 00 04 00 00 00 00 00`
//   vendor <hex...>            raw bytes on the vendor bulk IN endpoint
//   vendortext <string>        the same, as ASCII
//
// Everything the host sends back is printed with a `HOST_` prefix.

#include "EspUsbDevice.h"
#include "tusb.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceHidMouse mouse(device);
EspUsbDeviceVendor vendor(device);

static bool beginOk = false;
static bool lastReady = false;
static String line;

static void printHex(const uint8_t *data, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    if (i)
    {
      Serial.print(' ');
    }
    Serial.printf("%02x", data[i]);
  }
}

// Splits off the first whitespace-delimited token, leaving the remainder in
// `rest`. Commands take free-form text after the verb, so a full tokenizer
// would get in the way of `text hello world`.
static String nextToken(String &rest)
{
  rest.trim();
  const int space = rest.indexOf(' ');
  if (space < 0)
  {
    const String token = rest;
    rest = "";
    return token;
  }
  const String token = rest.substring(0, space);
  rest = rest.substring(space + 1);
  rest.trim();
  return token;
}

// Accepts `04`, `0x04` and decimal, because captures and datasheets mix all
// three and retyping them in one base is where transcription errors come from.
static bool parseNumber(const String &text, long &value)
{
  if (text.length() == 0)
  {
    return false;
  }
  char *end = nullptr;
  const char *start = text.c_str();
  const int base = (text.startsWith("0x") || text.startsWith("0X")) ? 16 : 10;
  value = strtol(base == 16 ? start + 2 : start, &end, base);
  return end && *end == '\0';
}

static size_t parseHexBytes(String rest, uint8_t *buffer, size_t capacity)
{
  size_t count = 0;
  while (count < capacity)
  {
    const String token = nextToken(rest);
    if (token.length() == 0)
    {
      break;
    }
    long value = 0;
    const String withPrefix =
        (token.startsWith("0x") || token.startsWith("0X")) ? token : "0x" + token;
    if (!parseNumber(withPrefix, value) || value < 0 || value > 0xff)
    {
      Serial.printf("ERROR not a byte: %s\n", token.c_str());
      return 0;
    }
    buffer[count++] = static_cast<uint8_t>(value);
  }
  return count;
}

static const char *speedName()
{
  switch (tud_speed_get())
  {
  case TUSB_SPEED_FULL:
    return "full";
  case TUSB_SPEED_HIGH:
    return "high";
  case TUSB_SPEED_LOW:
    return "low";
  default:
    return "unknown";
  }
}

static void printHelp()
{
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

static void printState()
{
  Serial.printf("STATE mounted=%u speed=%s leds=0x%02x hid_protocol=%s vendor_mounted=%u\n",
                device.ready() ? 1 : 0, speedName(), keyboard.ledState().leds,
                keyboard.protocol() ? "report" : "boot", vendor.mounted() ? 1 : 0);
}

static void handleCommand(String input)
{
  String rest = input;
  const String verb = nextToken(rest);
  if (verb.length() == 0)
  {
    return;
  }

  if (verb == "help")
  {
    printHelp();
    return;
  }
  if (verb == "state")
  {
    printState();
    return;
  }

  // Everything below puts something on the wire, and a report sent while the
  // host has not selected a configuration is silently dropped rather than
  // queued. Saying so is more useful than a bare `false` from the send call.
  if (!device.ready())
  {
    Serial.println("ERROR not mounted - the host has not enumerated this device");
    return;
  }

  if (verb == "text")
  {
    Serial.printf("SEND text \"%s\" ok=%u\n", rest.c_str(),
                  keyboard.write(rest.c_str()) ? 1 : 0);
    return;
  }
  if (verb == "key" || verb == "hold")
  {
    long usage = 0;
    long modifiers = 0;
    if (!parseNumber(nextToken(rest), usage))
    {
      Serial.println("ERROR usage: key <usage> [modifiers]");
      return;
    }
    const String modifierToken = nextToken(rest);
    if (modifierToken.length() > 0 && !parseNumber(modifierToken, modifiers))
    {
      Serial.println("ERROR modifiers must be a number");
      return;
    }
    const bool ok = (verb == "key")
                        ? keyboard.tapUsage(static_cast<uint8_t>(usage),
                                            static_cast<uint8_t>(modifiers))
                        : keyboard.pressUsage(static_cast<uint8_t>(usage),
                                              static_cast<uint8_t>(modifiers));
    Serial.printf("SEND %s usage=0x%02x modifiers=0x%02x ok=%u\n", verb.c_str(),
                  (unsigned)usage, (unsigned)modifiers, ok ? 1 : 0);
    return;
  }
  if (verb == "release")
  {
    Serial.printf("SEND release ok=%u\n", keyboard.releaseAll() ? 1 : 0);
    return;
  }
  if (verb == "mouse")
  {
    long dx = 0;
    long dy = 0;
    long buttons = 0;
    long wheel = 0;
    if (!parseNumber(nextToken(rest), dx) || !parseNumber(nextToken(rest), dy))
    {
      Serial.println("ERROR usage: mouse <dx> <dy> [buttons] [wheel]");
      return;
    }
    const String buttonToken = nextToken(rest);
    if (buttonToken.length() > 0)
    {
      parseNumber(buttonToken, buttons);
    }
    const String wheelToken = nextToken(rest);
    if (wheelToken.length() > 0)
    {
      parseNumber(wheelToken, wheel);
    }
    const bool ok = mouse.move(static_cast<int8_t>(dx), static_cast<int8_t>(dy),
                               static_cast<int8_t>(wheel),
                               static_cast<uint8_t>(buttons));
    Serial.printf("SEND mouse dx=%ld dy=%ld buttons=0x%02x wheel=%ld ok=%u\n", dx, dy,
                  (unsigned)buttons, wheel, ok ? 1 : 0);
    return;
  }
  if (verb == "click")
  {
    long button = 1;
    const String buttonToken = nextToken(rest);
    if (buttonToken.length() > 0 && !parseNumber(buttonToken, button))
    {
      Serial.println("ERROR usage: click <1|2|4>");
      return;
    }
    Serial.printf("SEND click buttons=0x%02x ok=%u\n", (unsigned)button,
                  mouse.click(static_cast<uint8_t>(button)) ? 1 : 0);
    return;
  }
  if (verb == "report")
  {
    long reportId = 0;
    if (!parseNumber(nextToken(rest), reportId))
    {
      Serial.println("ERROR usage: report <id> <hex...>");
      return;
    }
    uint8_t payload[64];
    const size_t length = parseHexBytes(rest, payload, sizeof(payload));
    if (length == 0)
    {
      Serial.println("ERROR no payload bytes");
      return;
    }
    // Instance 0 is the HID interface the library allocated first; every HID
    // class registered in this sketch shares it and is told apart by report ID.
    const bool ok = device.sendHidReport(0, static_cast<uint8_t>(reportId), payload,
                                         length);
    Serial.printf("SEND report id=%u length=%u ok=%u data=", (unsigned)reportId,
                  (unsigned)length, ok ? 1 : 0);
    printHex(payload, length);
    Serial.println();
    return;
  }
  if (verb == "vendor" || verb == "vendortext")
  {
    if (!vendor.mounted())
    {
      Serial.println("ERROR vendor interface is not open on the host side");
      return;
    }
    uint8_t payload[64];
    size_t length = 0;
    if (verb == "vendortext")
    {
      length = min(rest.length(), sizeof(payload));
      memcpy(payload, rest.c_str(), length);
    }
    else
    {
      length = parseHexBytes(rest, payload, sizeof(payload));
    }
    if (length == 0)
    {
      Serial.println("ERROR no payload bytes");
      return;
    }
    const size_t written = vendor.write(payload, length);
    vendor.flush();
    Serial.printf("SEND vendor length=%u written=%u data=", (unsigned)length,
                  (unsigned)written);
    printHex(payload, length);
    Serial.println();
    return;
  }

  Serial.printf("ERROR unknown command: %s (try `help`)\n", verb.c_str());
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== EspUsbDevice console ===");

  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("HOST_HID_OUTPUT leds=0x%02x num=%u caps=%u scroll=%u\n",
                                          report.leds, report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });

  keyboard.onProtocol([](const EspUsbDeviceHidProtocolEvent &event)
                      {
                        // Hosts switch to boot protocol in BIOS/UEFI and back to
                        // report protocol once the OS driver loads, so seeing this
                        // flip is normal - and it changes which report format the
                        // library puts on the wire.
                        Serial.printf("HOST_HID_PROTOCOL instance=%u protocol=%s\n",
                                      event.instance,
                                      event.protocol ? "report" : "boot");
                      });

  vendor.onRx([](size_t available)
              {
                uint8_t buffer[64];
                while (available > 0)
                {
                  const size_t chunk = vendor.read(buffer, min(available, sizeof(buffer)));
                  if (chunk == 0)
                  {
                    break;
                  }
                  Serial.print("HOST_VENDOR_OUT ");
                  Serial.print(chunk);
                  Serial.print(" ");
                  printHex(buffer, chunk);
                  Serial.println();
                  available = vendor.available();
                }
              });

  vendor.onControlRequest([](const EspUsbDeviceVendorControlRequest &request)
                          {
                            Serial.printf("HOST_VENDOR_CONTROL stage=%u type=0x%02x request=0x%02x value=0x%04x index=0x%04x length=%u\n",
                                          request.stage, request.bmRequestType,
                                          request.bRequest, request.wValue,
                                          request.wIndex, request.wLength);
                            // Answer IN requests with a fixed banner so the host
                            // sees a successful transfer rather than a stall; edit
                            // this to replay whatever the real protocol returns.
                            static const char banner[] = "EspUsbDeviceConsole";
                            if (request.bmRequestType & 0x80)
                            {
                              return vendor.sendControlResponse(
                                  request, banner,
                                  min(static_cast<size_t>(request.wLength),
                                      sizeof(banner) - 1));
                            }
                            return vendor.sendControlResponse(request);
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4052;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Console";
  config.serialNumber = "espusb-console";

  beginOk = device.begin(config);
  if (!beginOk)
  {
    Serial.printf("BEGIN failed error=%s\n", device.lastErrorName());
    return;
  }

  Serial.println("BEGIN ok - connect the device connector to the host under test");
  printHelp();
}

void loop()
{
  if (beginOk)
  {
    const bool ready = device.ready();
    if (ready != lastReady)
    {
      lastReady = ready;
      Serial.printf("%s\n", ready ? "MOUNTED" : "UNMOUNTED");
    }
  }

  while (Serial.available())
  {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r')
    {
      continue;
    }
    if (c == '\n')
    {
      handleCommand(line);
      line = "";
      continue;
    }
    if (line.length() < 200)
    {
      line += c;
    }
  }

  delay(1);
}
