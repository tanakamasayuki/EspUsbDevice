#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidConsumerControl media(device);
EspUsbDeviceHidSystemControl systemControl(device);

static uint32_t lastStepMs = 0;
static uint8_t stepIndex = 0;

struct MediaStep
{
  const char *name;
  uint16_t usage;
};

static const MediaStep MEDIA_STEPS[] = {
    {"volume up", ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP},
    {"volume down", ESP_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN},
    {"mute", ESP_USB_DEVICE_CONSUMER_CONTROL_MUTE},
    {"play/pause", ESP_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE},
    {"next track", ESP_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK},
    {"previous track", ESP_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK},
};

static void sendMediaStep()
{
  const MediaStep &step = MEDIA_STEPS[stepIndex % (sizeof(MEDIA_STEPS) / sizeof(MEDIA_STEPS[0]))];
  if (media.click(step.usage))
  {
    Serial.printf("MEDIA_KEY %s usage=0x%04x\n", step.name, step.usage);
  }
  else
  {
    Serial.printf("MEDIA_KEY_FAILED %s error=%s\n", step.name, device.lastErrorName());
  }
  stepIndex++;
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  config.speed = ESP_USB_DEVICE_SPEED_FULL;
  config.vid = 0x303a;
  config.pid = 0x4006;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Media Keys";
  config.serialNumber = "espusb-media-keys";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB media keys ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastStepMs >= 3000)
  {
    lastStepMs = now;
    sendMediaStep();
  }

  // System control keys can affect the host power state. Send them only from
  // an explicit user action in your own sketch.
  //
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF);
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST);

  delay(1);
}
