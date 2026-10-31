#include <EspUsbDevice.h>

EspUsbDevice device;
// Two HID Functions in one interface, told apart by report ID.
EspUsbDeviceHidConsumerControl media(device);
EspUsbDeviceHidSystemControl systemControl(device);

struct MediaStep {
  const char *name;
  uint16_t usage;  // Consumer page usage ID.
};

const MediaStep steps[] = {
    {"volume up", ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP},
    {"volume down", ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN},
    {"mute", ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE},
    {"play/pause", ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE},
    {"next track", ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK},
    {"previous track", ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK},
};
uint8_t step = 0;

void sendMediaStep() {
  const MediaStep &s = steps[step++ % (sizeof(steps) / sizeof(steps[0]))];
  // click() sends the press and the release.
  if (media.click(s.usage)) Serial.printf("MEDIA_KEY %s usage=0x%04x\n", s.name, s.usage);
  else Serial.printf("MEDIA_KEY_FAILED %s %s\n", s.name, device.lastErrorName());
}

void setup() {
  Serial.begin(115200);
  EspUsbDeviceConfig config;
  config.pid = 0x4006;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Media Keys";
  config.serialNumber = "espusb-media-keys";
  if (!device.begin(config)) {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("USB media keys ready");
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  if (device.ready() && uint32_t(now - last) >= 3000) {
    last = now;
    sendMediaStep();
  }

  // System Control can put the Host to sleep or power it off. Wire these to an
  // explicit user action rather than a timer. They take Generic Desktop usages
  // (0x81-0x83); WAKE_HOST is an ordinary input, not a remote wakeup.
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF);
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST);

  delay(1);
}
