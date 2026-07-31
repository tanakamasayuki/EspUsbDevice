#include "EspUsbDevice.h"

// Device side of the NKRO peer test: an NKRO keyboard that holds multi-key
// chords all at once (impossible with the 6-key boot report) on command.
//   'c' -> an 8-key low-usage chord (A S D F G H J K)
//   'j' -> a chord of high usages (A + International1/3 + LANG1/2) that only fit
//          because the NKRO bitmap spans 0x00-0xDF; these are the JIS keys.
//   's' -> the same 8 keys plus 2 more and a modifier, sent as ONE state report
//          via sendReport(EspUsbDeviceNkroKeyboardReport), then read back through
//          heldState()
//   'x' -> sendReport(nkro state) on a keyboard whose NKRO is turned off, which
//          must fail rather than silently drop keys

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);

static const uint8_t chord[] = {
    ESP_USB_HID_KEY_A, ESP_USB_HID_KEY_S, ESP_USB_HID_KEY_D, ESP_USB_HID_KEY_F,
    ESP_USB_HID_KEY_G, ESP_USB_HID_KEY_H, ESP_USB_HID_KEY_J, ESP_USB_HID_KEY_K,
};

// High-usage keys past the 6KRO array's practical low range: International1
// (0x87), International3 (0x89), LANG1 (0x90), LANG2 (0x91), plus a plain 'A'.
static const uint8_t chordJis[] = {
    ESP_USB_HID_KEY_A,
    ESP_USB_HID_KEY_INTERNATIONAL1,
    ESP_USB_HID_KEY_INTERNATIONAL3,
    ESP_USB_HID_KEY_LANG1,
    ESP_USB_HID_KEY_LANG2,
};

// Ten keys plus Left Shift (usage 0xE1, which belongs in the modifier byte).
// Deliberately more than the 6 a boot report can carry.
static const uint8_t chordState[] = {
    ESP_USB_HID_KEY_A, ESP_USB_HID_KEY_S, ESP_USB_HID_KEY_D, ESP_USB_HID_KEY_F,
    ESP_USB_HID_KEY_G, ESP_USB_HID_KEY_H, ESP_USB_HID_KEY_J, ESP_USB_HID_KEY_K,
    ESP_USB_HID_KEY_L, ESP_USB_HID_KEY_SEMICOLON,
};
static const uint8_t chordStateModifier = 0xe1;

static void sendChord(const uint8_t *keys, size_t count)
{
  // pressUsage() accumulates key bits and sends the full bitmap each call but
  // ignores holdMs, so back-to-back calls can overwrite a report before the
  // host polls it. Space the presses out so every incremental report is
  // delivered, then hold briefly and release everything.
  for (size_t i = 0; i < count; i++)
  {
    keyboard.pressUsage(keys[i]);
    delay(25);
  }
  delay(150);
  keyboard.releaseAll();
}

// One report carries the whole chord, so the host cannot observe the keys
// arriving one at a time the way the incremental path above allows.
static void sendChordState()
{
  EspUsbDeviceNkroKeyboardReport report;
  for (size_t i = 0; i < sizeof(chordState); i++)
  {
    report.press(chordState[i]);
  }
  report.press(chordStateModifier);
  const bool sent = keyboard.sendReport(report);

  // heldState() must mirror what was just sent, including the modifier that
  // press() routed out of the bitmap.
  const EspUsbDeviceNkroKeyboardReport &held = keyboard.heldState();
  unsigned heldCount = 0;
  for (size_t i = 0; i < sizeof(chordState); i++)
  {
    if (held.isDown(chordState[i]))
    {
      heldCount++;
    }
  }
  Serial.printf("SENT_STATE ok=%u n=%u held=%u mod=%u protocol=%s\n",
                sent ? 1 : 0,
                (unsigned)sizeof(chordState),
                heldCount,
                held.isDown(chordStateModifier) ? 1 : 0,
                keyboard.protocol() == 0 ? "boot" : "report");

  delay(150);
  keyboard.releaseAll();
  Serial.printf("RELEASED held_a=%u mod=%u\n",
                keyboard.heldState().isDown(ESP_USB_HID_KEY_A) ? 1 : 0,
                keyboard.heldState().isDown(chordStateModifier) ? 1 : 0);
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  keyboard.enableNkro();
  keyboard.setLayout(ESP_USB_DEVICE_KEYBOARD_LAYOUT_EN_US);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4033;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NKRO Keyboard";
  config.serialNumber = "espusb-keyboard-nkro";
  device.begin(config);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY nkro=%u ready=%u\n",
                    keyboard.nkroEnabled() ? 1 : 0,
                    device.ready() ? 1 : 0);
    }
    else if (command == 'c')
    {
      sendChord(chord, sizeof(chord));
      Serial.printf("SENT_CHORD n=%u protocol=%s\n",
                    (unsigned)sizeof(chord),
                    keyboard.protocol() == 0 ? "boot" : "report");
    }
    else if (command == 's')
    {
      sendChordState();
    }
    else if (command == 'x')
    {
      // A second keyboard object that never had enableNkro() called: the state
      // overload must refuse instead of pretending it sent 11 keys. It is not
      // attached to a running device, so a false result also cannot be mistaken
      // for a transport failure being reported correctly.
      static EspUsbDevice spareDevice;
      static EspUsbDeviceHidKeyboard spareKeyboard(spareDevice);
      EspUsbDeviceNkroKeyboardReport report;
      report.press(ESP_USB_HID_KEY_A);
      Serial.printf("NKRO_DISABLED_SEND ok=%u nkro=%u\n",
                    spareKeyboard.sendReport(report) ? 1 : 0,
                    spareKeyboard.nkroEnabled() ? 1 : 0);
    }
    else if (command == 'j')
    {
      sendChord(chordJis, sizeof(chordJis));
      Serial.printf("SENT_CHORD_JIS n=%u protocol=%s\n",
                    (unsigned)sizeof(chordJis),
                    keyboard.protocol() == 0 ? "boot" : "report");
    }
  }
  delay(1);
}
