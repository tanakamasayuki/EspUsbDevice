#include "EspUsbDevice.h"

// Host-independent unit test for composite constraints.
//
// Verifies that Audio participates in the normal composite descriptor builder
// and that the independent MAX_CLASSES registration limit remains enforced.

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

static void checkAudioComposite(const char *name, EspUsbDevice &device,
                                EspUsbAudioFunction &audio)
{
  auto &playback = audio.addPlaybackStream();
  check(playback.addFormat({48000, 2, 2, 16}), name);
  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4030;
  config.startTinyUsb = false;
  check(device.begin(config), name);
  check(device.lastError() == ESP_OK, name);
  device.end();
}

static void testAudioWithHid()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  EspUsbDeviceHidKeyboard keyboard(device);
  checkAudioComposite("audio_plus_hid", device, audio);
}

static void testAudioWithCdc()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  EspUsbDeviceCdcSerial cdc(device);
  checkAudioComposite("audio_plus_cdc", device, audio);
}

static void testAudioWithVendor()
{
  EspUsbDevice device;
  EspUsbAudioFunction audio(device);
  EspUsbDeviceVendor vendor(device);
  checkAudioComposite("audio_plus_vendor", device, audio);
}

// Allocation is independent of registration order.
static void testAudioAddedLast()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard keyboard(device);
  EspUsbAudioFunction audio(device);
  checkAudioComposite("hid_plus_audio", device, audio);
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

static void testS3EndpointLimit()
{
  EspUsbDevice device;
  EspUsbDeviceHidKeyboard hid(device);
  EspUsbDeviceCdcSerial cdc(device);
  EspUsbDeviceMidi midi(device);
  EspUsbDeviceVendor vendor(device);

  EspUsbDeviceConfig config;
  config.startTinyUsb = false;
  check(!device.begin(config), "s3_in_endpoint_limit_rejected");
  check(device.lastError() == ESP_ERR_INVALID_SIZE,
        "s3_in_endpoint_limit_error");
}

void setup()
{
  Serial.begin(115200);
  delay(5000);

  Serial.println("TEST_BEGIN composite_constraints");
  testAudioWithHid();
  testAudioWithCdc();
  testAudioWithVendor();
  testAudioAddedLast();
  testMaxClasses();
  testS3EndpointLimit();
  Serial.printf("TEST_END pass=%d fail=%d\n", passCount, failCount);
  Serial.println(failCount == 0 ? "OK" : "NG");
  Serial.flush();
}

void loop()
{
}
