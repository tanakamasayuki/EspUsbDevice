// EspUsbDeviceBringUpCheck - the first sketch to run on a new board.
//
// Answers, in order:
//   1. Does this build and start on this target at all?
//   2. Which USB controller did the library pick, and did begin() succeed?
//   3. Does the host enumerate the board (device.ready() becomes true)?
//   4. At which speed, and does host -> device traffic reach the sketch?
//
// It registers a HID keyboard but never sends a key, so it is safe to leave
// plugged into a PC. Press CapsLock / NumLock on the host keyboard to prove the
// host -> device direction: the LED output report is printed here.

#include "EspUsbDevice.h"
#include "tusb.h"

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static bool beginOk = false;
static bool lastReady = false;
static bool checklistPrinted = false;
static uint32_t beginMs = 0;
static uint32_t lastStatusMs = 0;

static const char *targetName()
{
#if defined(CONFIG_IDF_TARGET_ESP32S2)
  return "ESP32-S2";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32P4)
  return "ESP32-P4";
#else
  return "unsupported-target";
#endif
}

static const char *controllerName(EspUsbController controller)
{
  switch (controller)
  {
  case EspUsbController::FullSpeed:
    return "FullSpeed";
  case EspUsbController::HighSpeed:
    return "HighSpeed";
  default:
    return "Auto";
  }
}

// Which controller Auto resolves to, so the log says what the hardware will
// actually do rather than repeating the request.
static const char *resolvedControllerName(EspUsbController controller)
{
  if (controller != EspUsbController::Auto)
  {
    return controllerName(controller);
  }
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  return "HighSpeed (Auto on P4)";
#else
  return "FullSpeed (Auto)";
#endif
}

static const char *speedName()
{
  switch (tud_speed_get())
  {
  case TUSB_SPEED_FULL:
    return "Full (12 Mbps)";
  case TUSB_SPEED_HIGH:
    return "High (480 Mbps)";
  case TUSB_SPEED_LOW:
    return "Low (1.5 Mbps)";
  default:
    return "unknown";
  }
}

static void printChecklist()
{
  Serial.println("CHECKLIST the host has not enumerated this board yet:");
  Serial.println("  1. Cable      - is it a data cable? A charge-only cable has no D+/D-.");
  Serial.println("  2. Connector  - is the cable in the OTG/device connector, not the");
  Serial.println("                  USB-UART or USB Serial/JTAG one? Check the schematic;");
  Serial.println("                  the silkscreen is not reliable.");
  Serial.println("  3. Build      - is 'USB Mode' left at the default (TinyUSB/OTG)?");
  Serial.println("                  Do not call USB.begin(); this library owns the stack.");
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  Serial.println("  4. P4 port    - HighSpeed uses rhport 1 and the external UTMI PHY;");
  Serial.println("                  FullSpeed uses rhport 0 (GPIO26/27 by default).");
  Serial.println("                  Set config.controller to match the connector you used.");
#else
  Serial.println("  4. Power      - is the board powered? A bus-powered board that lost");
  Serial.println("                  VBUS when you moved the cable is not running at all.");
#endif
  Serial.println("  5. Host side  - Linux: 'dmesg -w' while plugging. Windows: Device");
  Serial.println("                  Manager. If the host logs nothing, it is electrical.");
  Serial.println("  6. Descriptor - if the host logs an error instead of nothing, run");
  Serial.println("                  EspUsbDeviceDescriptorDump and compare.");
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== EspUsbDevice bring-up check ===");
  Serial.printf("TARGET %s\n", targetName());
  Serial.printf("CORE arduino-esp32 %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR,
                ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);

  // Reading the LED report is what proves host -> device works. Nothing is ever
  // sent in the other direction by this sketch.
  keyboard.onOutputReport([](const EspUsbDeviceHidKeyboardOutputReport &report)
                          {
                            Serial.printf("HOST_OUTPUT_REPORT leds=0x%02x num=%u caps=%u scroll=%u\n",
                                          report.leds,
                                          report.numLock ? 1 : 0,
                                          report.capsLock ? 1 : 0,
                                          report.scrollLock ? 1 : 0);
                          });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4050;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice BringUpCheck";
  config.serialNumber = "espusb-bringup";
  // On ESP32-P4, set this to FullSpeed if the cable goes to the FS connector.
  // config.controller = EspUsbController::FullSpeed;

  Serial.printf("CONTROLLER requested=%s resolved=%s\n",
                controllerName(config.controller),
                resolvedControllerName(config.controller));

  beginOk = device.begin(config);
  beginMs = millis();
  if (!beginOk)
  {
    Serial.printf("BEGIN failed error=%s\n", device.lastErrorName());
    Serial.println("  A failure here is local to the board - the host is not involved yet.");
    Serial.println("  ESP_ERR_INVALID_SIZE  : the descriptor or endpoint budget does not fit");
    Serial.println("                          this controller. Remove a class or use the P4 HS port.");
    Serial.println("  ESP_ERR_NOT_SUPPORTED : this target has no usable USB device controller.");
    Serial.println("  ESP_ERR_NO_MEM        : out of heap for the descriptor buffers.");
    return;
  }

  Serial.println("BEGIN ok");
  Serial.printf("DESCRIPTOR config_bytes=%u hid_endpoint_size=%u\n",
                (unsigned)(device.configurationDescriptor(0)[2] |
                           (device.configurationDescriptor(0)[3] << 8)),
                (unsigned)device.hidEndpointSize());
  Serial.printf("VID_PID %04x:%04x\n", config.vid, config.pid);
  Serial.println("Now connect the device connector to a host and watch for MOUNTED.");
}

void loop()
{
  if (!beginOk)
  {
    delay(1000);
    return;
  }

  const uint32_t now = millis();
  const bool ready = device.ready();

  if (ready != lastReady)
  {
    lastReady = ready;
    if (ready)
    {
      Serial.printf("MOUNTED t=%lums speed=%s\n", (unsigned long)(now - beginMs),
                    speedName());
      Serial.println("  The host completed enumeration and selected a configuration.");
      Serial.println("  Press CapsLock on the host keyboard to check host -> device.");
      checklistPrinted = true; // enumerated at least once; stop nagging
    }
    else
    {
      Serial.printf("UNMOUNTED t=%lums\n", (unsigned long)(now - beginMs));
      Serial.println("  Cable removed, host suspended/reset the bus, or the host");
      Serial.println("  deselected the configuration.");
    }
  }

  if (!ready && !checklistPrinted && now - beginMs > 10000)
  {
    checklistPrinted = true;
    printChecklist();
  }

  if (now - lastStatusMs >= 5000)
  {
    lastStatusMs = now;
    Serial.printf("STATUS mounted=%u t=%lums leds=0x%02x\n", ready ? 1 : 0,
                  (unsigned long)(now - beginMs), keyboard.ledState().leds);
  }

  delay(10);
}
