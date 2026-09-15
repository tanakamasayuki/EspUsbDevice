// rebootToRomDfu() must refuse on an ESP32-S3 whose USB_PHY_SEL eFuse is not
// burned, and it must refuse *without restarting*.
//
// The ROM routes the shared internal PHY from that eFuse at boot, whatever the
// application left behind, so with the eFuse unburned the ROM's DFU stack ends
// up on a controller with no pads: measured, the chip reaches the download
// loader and nothing at all enumerates. On a one-connector board, restarting
// anyway is the difference between a call that did nothing and a board that
// needs someone to press BOOT - so the guard is the behaviour under test, and
// "the sketch is still running afterwards" is half of what is being asserted.
//
// This test is safe to run on a shared rig precisely because it reads the same
// eFuse first: on a board where the call *would* restart, it reports and skips.
// A board that logs over USB Serial/JTAG - which is how this one is being read -
// cannot have the eFuse burned, because burning it takes USB Serial/JTAG away.
//
// EspUsbDeviceConfig::startTinyUsb stays false: nothing here needs the PHY, and
// leaving it alone keeps the USB Serial/JTAG log alive.
#include "EspUsbDevice.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#define ROM_DFU_GUARD_APPLIES 1
#else
#define ROM_DFU_GUARD_APPLIES 0
#endif

static int passCount = 0;
static int failCount = 0;

static void check(bool condition, const char *name)
{
  if (condition)
  {
    passCount++;
  }
  else
  {
    Serial.print("FAIL ");
    Serial.println(name);
    failCount++;
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("TEST_BEGIN rom_dfu_guard");

#if ROM_DFU_GUARD_APPLIES
  const bool phySelBurned = esp_efuse_read_field_bit(ESP_EFUSE_USB_PHY_SEL);
  Serial.print("USB_PHY_SEL=");
  Serial.println(phySelBurned ? "burned" : "unburned");

  if (phySelBurned)
  {
    // Calling here would genuinely restart into ROM DFU and take the board off
    // the rig, so stop rather than assert. Nothing about the guard can be
    // observed on this board.
    Serial.println("SKIP rom_dfu_guard_needs_unburned_efuse");
  }
  else
  {
    EspUsbDevice device;
    // A Runtime DFU interface, the shape a sketch that offers this call would
    // actually have - and it gives begin() an interface to build.
    EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime);
    EspUsbDeviceConfig config;
    config.pid = 0x4072;
    config.startTinyUsb = false;
    check(device.begin(config), "begin");

    check(!device.rebootToRomDfu(), "refuses");
    check(device.lastError() == ESP_ERR_NOT_SUPPORTED, "reports_not_supported");
    // Reached at all only because the call did not restart the chip. Without
    // the guard, execution never returns here.
    Serial.println("STILL_RUNNING");
    check(true, "did_not_restart");
  }
#else
  Serial.println("SKIP rom_dfu_guard_esp32s3_only");
#endif

  Serial.print("TEST_END pass=");
  Serial.print(passCount);
  Serial.print(" fail=");
  Serial.println(failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
}

void loop()
{
  delay(1000);
}
