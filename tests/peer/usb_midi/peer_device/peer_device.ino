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

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
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
