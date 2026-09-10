#include "EspUsbHost.h"

EspUsbHost usb;

// Latched from the connect event so the test can re-read the device's identity
// without forcing a replug.
static uint16_t connectedVid = 0;
static uint16_t connectedPid = 0;
static bool connectedSupported = false;
static uint8_t connectedInterfaces = 0;

// Latched from the connect event so a test can ask whether the peer is attached
// instead of having to be the one that saw it arrive.
static volatile uint8_t deviceAddress = 0;
static uint16_t deviceVid = 0;
static uint16_t devicePid = 0;

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          deviceVid = device.vid;
                          devicePid = device.pid;
                          // supported / interfaces are reported so the test can check that a
                          // MIDI-only device counts as supported. Before EspUsbHost 2.6.0 the
                          // flag was built from HID / CDC / audio / MSC / vendor-serial
                          // detection only, so this device enumerated as unsupported.
                          connectedVid = device.vid;
                          connectedPid = device.pid;
                          connectedSupported = device.supported;
                          connectedInterfaces = device.configurationInterfaceCount;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x supported=%u interfaces=%u\n",
                                        device.vid,
                                        device.pid,
                                        device.supported ? 1 : 0,
                                        device.configurationInterfaceCount);
                        });

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    {
                      Serial.printf("MIDI_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                                    message.cable,
                                    message.codeIndex,
                                    message.status,
                                    message.data1,
                                    message.data2);
                    });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

// Block until the peer has been enumerated, so every command below answers about
// a device that is actually attached, whatever order the tests run in.
//
// deviceAddress is latched in onDeviceConnected, which fires after the host has
// claimed the interfaces - the right side of the event for anything that reads
// the device's interfaces or endpoints. Waiting here rather than announcing once
// at boot is what lets a test run in any position: a boot announcement is only
// visible to whichever test happens to be first.
static bool waitForDevice(uint32_t timeoutMs = 5000)
{
  const uint32_t startedAt = millis();
  while (deviceAddress == 0 && millis() - startedAt < timeoutMs)
  {
    delay(10);
  }
  return deviceAddress != 0;
}

void loop()
{
  if (Serial.available() > 0)
  {
    char command = Serial.read();
    waitForDevice();
    if (command == 'i')
    {
      Serial.printf("DEVICE_INFO vid=%04x pid=%04x supported=%u interfaces=%u\n",
                    connectedVid,
                    connectedPid,
                    connectedSupported ? 1 : 0,
                    connectedInterfaces);
    }
    else if (command == 'n')
    {
      Serial.printf("MIDI_TX_NOTE_ON %u\n", usb.midiSendNoteOn(0, 60, 100) ? 1 : 0);
    }
    else if (command == 'f')
    {
      Serial.printf("MIDI_TX_NOTE_OFF %u\n", usb.midiSendNoteOff(0, 60, 0) ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("MIDI_TX_CC %u\n", usb.midiSendControlChange(0, 74, 64) ? 1 : 0);
    }
    else if (command == 'p')
    {
      Serial.printf("MIDI_TX_PROGRAM %u\n", usb.midiSendProgramChange(0, 10) ? 1 : 0);
    }
    else if (command == 'b')
    {
      Serial.printf("MIDI_TX_BEND %u\n", usb.midiSendPitchBend(0, 8192 + 1024) ? 1 : 0);
    }
    else if (command == 'a')
    {
      Serial.printf("MIDI_TX_PRESSURE %u\n", usb.midiSendChannelPressure(0, 77) ? 1 : 0);
    }
    else if (command == 'y')
    {
      Serial.printf("MIDI_TX_POLY_PRESSURE %u\n", usb.midiSendPolyPressure(0, 60, 80) ? 1 : 0);
    }
    else if (command == 's')
    {
      const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
      Serial.printf("MIDI_TX_SYSEX %u\n", usb.midiSendSysEx(sysex, sizeof(sysex)) ? 1 : 0);
    }
  }
  delay(1);
}
