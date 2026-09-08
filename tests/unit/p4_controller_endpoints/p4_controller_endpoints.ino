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

// The combination the multi-port CDC work is aimed at: two serial ports beside
// HID and Vendor. IN endpoints are 1 (HID) + 1 (Vendor) + 2 x 2 (CDC) = 6, one
// under the HS controller's 7, and it is also five registered functions.
static void testHighSpeedAcceptsHidVendorAndTwoSerialPorts()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceVendor vendor(device);
  EspUsbDeviceCdcSerial console(device, "Console");
  EspUsbDeviceCdcSerial data(device, "Data Link");

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::HighSpeed;
  config.startTinyUsb = false;
  check(device.begin(config), "p4_hs_hid_vendor_two_ports");
  check(device.lastError() == ESP_OK, "p4_hs_hid_vendor_two_ports_ok");
  check(console.port() == 0, "p4_hs_first_port_instance");
  check(data.port() == 1, "p4_hs_second_port_instance");
  device.end();
}

// Three ports is the bare HS maximum (6 IN), and it is also the compiled
// capacity - a fourth port could never be enumerated, so none is built.
static void testHighSpeedAcceptsThreeSerialPorts()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");
  EspUsbDeviceCdcSerial third(device, "C");

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::HighSpeed;
  config.startTinyUsb = false;
  check(device.begin(config), "p4_hs_three_ports");
  check(device.lastError() == ESP_OK, "p4_hs_three_ports_ok");
  check(third.port() == 2, "p4_hs_third_port_instance");
  check(EspUsbDevice::maxCdcPorts() == 3, "p4_port_capacity");
  device.end();
}

// A fourth port needs 8 IN endpoints; the HS controller has 7.
static void testHighSpeedRejectsFourSerialPorts()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");
  EspUsbDeviceCdcSerial third(device, "C");
  EspUsbDeviceCdcSerial fourth(device, "D");

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::HighSpeed;
  config.startTinyUsb = false;
  check(!device.begin(config), "p4_hs_four_ports_rejected");
  check(device.lastError() == ESP_ERR_INVALID_SIZE,
        "p4_hs_four_ports_error");
}

// The FS controller has the same 4 non-control IN endpoints as the S3, so it
// stops at two ports however many the build compiled.
static void testFullSpeedRejectsThreeSerialPorts()
{
  EspUsbDevice device;
  EspUsbDeviceCdcSerial first(device, "A");
  EspUsbDeviceCdcSerial second(device, "B");
  EspUsbDeviceCdcSerial third(device, "C");

  EspUsbDeviceConfig config;
  config.controller = EspUsbController::FullSpeed;
  config.startTinyUsb = false;
  check(!device.begin(config), "p4_fs_three_ports_rejected");
  check(device.lastError() == ESP_ERR_INVALID_SIZE,
        "p4_fs_three_ports_error");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN p4_controller_endpoints");
  testFullSpeedRejectsFiveInEndpoints();
  testHighSpeedAcceptsFiveInEndpoints();
  testAutoUsesHighSpeedLimits();
  testHighSpeedAcceptsHidVendorAndTwoSerialPorts();
  testHighSpeedAcceptsThreeSerialPorts();
  testHighSpeedRejectsFourSerialPorts();
  testFullSpeedRejectsThreeSerialPorts();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
