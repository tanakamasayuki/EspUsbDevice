#include <Arduino.h>
#include "EspUsbDevice.h"

// Maximal composite: HID keyboard + CDC + MSC + MIDI (4 classes = MAX_CLASSES).
// On S3 this is the endpoint budget ceiling (IN endpoints: HID 1 + CDC 2 +
// MSC 1 + MIDI 1 = 5 = CFG_TUD_NUM_IN_EPS). If this enumerates with all
// interfaces claimed and no duplicate endpoint, every 2-/3-class subset does
// too. Pairs with composite_quad_midi.ino (host). See tests/TEST_PLAN.ja.md.

EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
EspUsbDeviceCdcSerial UsbSerial(device);
EspUsbDeviceMsc MSC(device);
EspUsbDeviceMidi MIDI(device);

static bool beginOk = false;
static const char *beginError = "ESP_OK";

static constexpr uint32_t BLOCK_COUNT = 16;
static constexpr uint16_t BLOCK_SIZE = 512;
static uint8_t disk[BLOCK_COUNT][BLOCK_SIZE];

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
  {
    return -1;
  }
  uint8_t *out = static_cast<uint8_t *>(buffer);
  uint32_t remaining = bufsize, cur = lba, off = offset;
  while (remaining > 0)
  {
    if (cur >= BLOCK_COUNT)
    {
      return -1;
    }
    const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - off);
    memcpy(out, disk[cur] + off, chunk);
    out += chunk;
    remaining -= chunk;
    cur++;
    off = 0;
  }
  return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  if (lba >= BLOCK_COUNT || offset >= BLOCK_SIZE)
  {
    return -1;
  }
  uint32_t remaining = bufsize, cur = lba, off = offset;
  while (remaining > 0)
  {
    if (cur >= BLOCK_COUNT)
    {
      return -1;
    }
    const uint32_t chunk = min<uint32_t>(remaining, BLOCK_SIZE - off);
    memcpy(disk[cur] + off, buffer, chunk);
    buffer += chunk;
    remaining -= chunk;
    cur++;
    off = 0;
  }
  return bufsize;
}

static bool tapKeyWithRetry(char c)
{
  const uint32_t start = millis();
  while (millis() - start < 1000)
  {
    if (keyboard.tapKey(c))
    {
      return true;
    }
    delay(5);
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  memset(disk, 0, sizeof(disk));
  disk[0][0] = 0xeb;
  disk[0][510] = 0x55;
  disk[0][511] = 0xaa;

  MSC.vendorID("ESP32");
  MSC.productID("MSC_QUAD");
  MSC.productRevision("1.0");
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.isWritable(true);
  MSC.begin(BLOCK_COUNT, BLOCK_SIZE);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4022;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice HID+CDC+MSC+MIDI";
  config.serialNumber = "espusb-quad";

  beginOk = device.begin(config);
  beginError = device.lastErrorName();
  Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 'b')
    {
      Serial.printf("DEVICE_BEGIN %s %s\n", beginOk ? "ok" : "ng", beginError);
    }
    else if (command == 'k')
    {
      Serial.printf("DEVICE_KEY %u\n", tapKeyWithRetry('a') ? 1 : 0);
    }
    else if (command == 'd')
    {
      const uint8_t payload[] = "device to host";
      Serial.printf("DEVICE_TX %u\n", UsbSerial.write(payload, sizeof(payload) - 1) == sizeof(payload) - 1 ? 1 : 0);
    }
    else if (command == 'n')
    {
      Serial.println(MIDI.noteOn(0, 64, 110) ? "DEVICE_TX_NOTE_ON" : "DEVICE_TX_FAILED");
    }
  }

  while (UsbSerial.available() > 0)
  {
    Serial.print("DEVICE_RX ");
    while (UsbSerial.available() > 0)
    {
      Serial.write(UsbSerial.read());
    }
    Serial.println();
  }
  delay(1);
}
