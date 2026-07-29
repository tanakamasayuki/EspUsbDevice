#include "EspUsbDevice.h"

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    ++passCount;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    ++failCount;
  }
}

static void testFullSpeedRejectsFiveInEndpoints()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device); // one IN
  EspUsbDeviceCdcSerial cdc(device);         // two IN
  EspUsbDeviceMidi midi(device);             // one IN
  EspUsbDeviceVendor vendor(device);         // one IN

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::FullSpeed;
  config.startTinyUsb = false;
  check(!device.begin(config), "p4_fs_five_in_rejected");
  check(device.lastError() == ESP_ERR_INVALID_SIZE,
        "p4_fs_five_in_error");
}

static void testHighSpeedAcceptsFiveInEndpoints()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceCdcSerial cdc(device);
  EspUsbDeviceMidi midi(device);
  EspUsbDeviceVendor vendor(device);

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::HighSpeed;
  config.startTinyUsb = false;
  check(device.begin(config), "p4_hs_five_in_accepted");
  check(device.lastError() == ESP_OK, "p4_hs_five_in_ok");
  device.end();
}

static void testAutoUsesHighSpeedLimits()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceCdcSerial cdc(device);
  EspUsbDeviceMidi midi(device);
  EspUsbDeviceVendor vendor(device);

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::Auto;
  config.startTinyUsb = false;
  check(device.begin(config), "p4_auto_uses_hs_limits");
  check(device.lastError() == ESP_OK, "p4_auto_five_in_ok");
  device.end();
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN p4_controller_endpoints");
  testFullSpeedRejectsFiveInEndpoints();
  testHighSpeedAcceptsFiveInEndpoints();
  testAutoUsesHighSpeedLimits();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
