#include "EspUsbDevice.h"

// Host-independent unit test for composite constraints.
//
// Verifies the two structural limits documented in tests/TEST_PLAN.ja.md
// ("複合デバイス（composite）テスト"):
//   1. Audio is exclusive: Audio + any other class must fail begin() with
//      ESP_ERR_NOT_SUPPORTED (checked in EspUsbDevice::begin()).
//   2. MAX_CLASSES: the 5th addClass() must fail with ESP_FAIL.
//
// The Audio-reject path returns before any real USB init, so this needs no
// USB host and startTinyUsb can stay at its default (true).

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

// Each reject case builds a fresh device with Audio + one other class and
// expects begin() to fail early with ESP_ERR_NOT_SUPPORTED.
static void checkAudioReject(const char *name, EspUsbDevice &device, EspUsbDeviceClass &other)
{
  (void)other; // registered via its base constructor (addClass(this)).
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4030;
  const bool began = device.begin(config);
  check(!began, name);
  check(device.lastError() == ESP_ERR_NOT_SUPPORTED, name);
}

static void testAudioRejectsHid()
{
  EspUsbDevice device;
  EspUsbDeviceAudio audio(device);
  EspUsbDeviceHidKeyboard keyboard(device);
  checkAudioReject("audio_plus_hid_rejected", device, keyboard);
}

static void testAudioRejectsCdc()
{
  EspUsbDevice device;
  EspUsbDeviceAudio audio(device);
  EspUsbDeviceCdcSerial cdc(device);
  checkAudioReject("audio_plus_cdc_rejected", device, cdc);
}

static void testAudioRejectsMidi()
{
  EspUsbDevice device;
  EspUsbDeviceAudio audio(device);
  EspUsbDeviceMidi midi(device);
  checkAudioReject("audio_plus_midi_rejected", device, midi);
}

static void testAudioRejectsVendor()
{
  EspUsbDevice device;
  EspUsbDeviceAudio audio(device);
  EspUsbDeviceVendor vendor(device);
  checkAudioReject("audio_plus_vendor_rejected", device, vendor);
}

// Audio must also be rejected when it is the last class added (order-independent).
static void testAudioRejectsWhenAddedFirst()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbDeviceAudio audio(device);
  EspUsbDeviceConfig config;
  config.pid = 0x4031;
  check(!device.begin(config), "hid_plus_audio_rejected");
  check(device.lastError() == ESP_ERR_NOT_SUPPORTED, "hid_plus_audio_error");
}

// The 5th class must be refused by addClass() (MAX_CLASSES == 4).
static void testMaxClasses()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard c1(device);
  EspUsbDeviceHidMouse c2(device);
  EspUsbDeviceCdcSerial c3(device);
  EspUsbDeviceMidi c4(device);
  check(device.lastError() == ESP_OK, "four_classes_ok");

  EspUsbDeviceVendor c5(device); // base ctor calls addClass(this); must fail.
  check(device.lastError() == ESP_FAIL, "fifth_class_rejected");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN composite_reject");
  testAudioRejectsHid();
  testAudioRejectsCdc();
  testAudioRejectsMidi();
  testAudioRejectsVendor();
  testAudioRejectsWhenAddedFirst();
  testMaxClasses();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
