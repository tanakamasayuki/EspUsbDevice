#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceMidi MIDI(device);

void setup()
{
  Serial.begin(115200);
  delay(500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4017;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice MIDI";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

// Block until the host has configured us, so every command below answers from a
// usable device whatever order the tests run in.
//
// device.ready() is tud_mounted(): the host completed SET_CONFIGURATION. That is
// the event the tests actually depend on, and it is answered on demand rather
// than announced once at boot - a test that does not run first never sees an
// announcement, which is what made most of this suite fail when its modules were
// run in reverse. tests/peer/usb_msc has had this shape all along and was the
// only peer module that survived that check.
static bool waitForHost(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (!device.ready() && millis() - startedAt < timeoutMs)
  {
    device.task();
    delay(10);
  }
  return device.ready();
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    const bool hostReady = waitForHost();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY %u\n", hostReady ? 1 : 0);
    }
    else if (command == 'n')
    {
      Serial.println(MIDI.noteOn(0, 64, 110) ? "DEVICE_TX_NOTE_ON" : "DEVICE_TX_FAILED");
    }
    else if (command == 'f')
    {
      Serial.println(MIDI.noteOff(0, 64, 0) ? "DEVICE_TX_NOTE_OFF" : "DEVICE_TX_FAILED");
    }
    else if (command == 'p')
    {
      Serial.println(MIDI.programChange(0, 10) ? "DEVICE_TX_PROGRAM" : "DEVICE_TX_FAILED");
    }
    else if (command == 'b')
    {
      Serial.println(MIDI.pitchBend(0, 8192 + 1024) ? "DEVICE_TX_BEND" : "DEVICE_TX_FAILED");
    }
    else if (command == 'a')
    {
      Serial.println(MIDI.channelPressure(0, 77) ? "DEVICE_TX_PRESSURE" : "DEVICE_TX_FAILED");
    }
    else if (command == 'y')
    {
      Serial.println(MIDI.polyPressure(0, 60, 80) ? "DEVICE_TX_POLY_PRESSURE" : "DEVICE_TX_FAILED");
    }
    else if (command == 'c')
    {
      Serial.println(MIDI.controlChange(0, 74, 64) ? "DEVICE_TX_CC" : "DEVICE_TX_FAILED");
    }
  }

  EspUsbDeviceMidiPacket packet;
  if (MIDI.readPacket(packet))
  {
    Serial.printf("DEVICE_RX cin=%02x status=%02x data1=%u data2=%u\n",
                  packet.header & 0x0f,
                  packet.byte1,
                  packet.byte2,
                  packet.byte3);
  }
  delay(1);
}
