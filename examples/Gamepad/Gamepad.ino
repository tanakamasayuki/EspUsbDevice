#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceHidGamepad gamepad(device);

static uint32_t lastStepMs = 0;
static uint8_t stepIndex = 0;

static const uint8_t HATS[] = {
    ESP_USB_DEVICE_GAMEPAD_HAT_CENTER,
    ESP_USB_DEVICE_GAMEPAD_HAT_UP,
    ESP_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT,
    ESP_USB_DEVICE_GAMEPAD_HAT_RIGHT,
    ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT,
    ESP_USB_DEVICE_GAMEPAD_HAT_DOWN,
    ESP_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT,
    ESP_USB_DEVICE_GAMEPAD_HAT_LEFT,
    ESP_USB_DEVICE_GAMEPAD_HAT_UP_LEFT,
};

static void sendStep()
{
  static const int8_t AXIS[] = {0, 64, 127, 64, 0, -64, -127, -64};
  const uint8_t axisIndex = stepIndex % 8;
  const int8_t x = AXIS[axisIndex];
  const int8_t y = AXIS[(axisIndex + 2) % 8];
  const int8_t z = AXIS[(axisIndex + 4) % 8];
  const int8_t rz = AXIS[(axisIndex + 6) % 8];
  const uint8_t hat = HATS[stepIndex % (sizeof(HATS) / sizeof(HATS[0]))];
  const uint32_t buttons = 1UL << (stepIndex % 8);

  if (gamepad.send(x, y, z, rz, -x, -y, hat, buttons))
  {
    Serial.printf("GAMEPAD_TX x=%d y=%d z=%d rz=%d hat=%u buttons=0x%08lx\n",
                  x,
                  y,
                  z,
                  rz,
                  hat,
                  static_cast<unsigned long>(buttons));
  }
  else
  {
    Serial.printf("GAMEPAD_SEND_FAILED %s\n", device.lastErrorName());
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
  config.pid = 0x4008;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice Gamepad";
  config.serialNumber = "espusb-gamepad";

  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB gamepad ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastStepMs >= 500)
  {
    lastStepMs = now;
    sendStep();
  }

  delay(1);
}
