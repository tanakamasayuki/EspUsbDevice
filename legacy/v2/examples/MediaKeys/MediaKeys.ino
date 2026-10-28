// en: Send consumer media actions and declare a system-control function.
// ja: メディア操作を送り、System Control機能も宣言する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
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
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4006;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Media Keys";
  config.serialNumber = "espusb-media-keys";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
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

  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_STANDBY);
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF);
  // systemControl.click(ESP_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST);

  delay(1);
}
