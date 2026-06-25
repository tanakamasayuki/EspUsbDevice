#include "EspUsbDevice.h"
#include "EspUsbHost.h"

EspUsbDevice device;
EspUsbDeviceMidi DeviceMIDI(device);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool hostMidiReceived = false;
static volatile bool deviceMidiReceived = false;
static volatile uint8_t hostCin = 0;
static volatile uint8_t hostStatus = 0;
static volatile uint8_t hostData1 = 0;
static volatile uint8_t hostData2 = 0;
static volatile uint8_t deviceCin = 0;
static volatile uint8_t deviceStatus = 0;
static volatile uint8_t deviceData1 = 0;
static volatile uint8_t deviceData2 = 0;
static volatile bool sysexFirstSeen = false;
static volatile bool sysexLastSeen = false;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    pollDeviceMidi();
    delay(10);
  }
  return flag;
}

static void pollDeviceMidi()
{
  EspUsbDeviceMidiPacket packet;
  while (DeviceMIDI.readPacket(packet))
  {
    deviceCin = packet.header & 0x0f;
    deviceStatus = packet.byte1;
    deviceData1 = packet.byte2;
    deviceData2 = packet.byte3;
    deviceMidiReceived = true;
    if (deviceCin == 0x04 && deviceStatus == 0xf0 && deviceData1 == 125 && deviceData2 == 1)
    {
      sysexFirstSeen = true;
    }
    if (deviceCin == 0x06 && deviceStatus == 0x02 && deviceData1 == 247 && deviceData2 == 0)
    {
      sysexLastSeen = true;
    }
    Serial.printf("DEVICE_RX cin=%02x status=%02x data1=%u data2=%u\n",
                  deviceCin,
                  deviceStatus,
                  deviceData1,
                  deviceData2);
  }
}

static bool waitHostMidi(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    if (hostMidiReceived && hostCin == cin && hostStatus == status && hostData1 == data1 && hostData2 == data2)
    {
      return true;
    }
    pollDeviceMidi();
    delay(10);
  }
  Serial.printf("MIDI_RX_TIMEOUT cin=%02x status=%02x data1=%u data2=%u\n",
                hostCin,
                hostStatus,
                hostData1,
                hostData2);
  return false;
}

static bool waitDeviceMidi(uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    pollDeviceMidi();
    if (deviceMidiReceived && deviceCin == cin && deviceStatus == status && deviceData1 == data1 && deviceData2 == data2)
    {
      return true;
    }
    delay(10);
  }
  Serial.printf("DEVICE_RX_TIMEOUT cin=%02x status=%02x data1=%u data2=%u\n",
                deviceCin,
                deviceStatus,
                deviceData1,
                deviceData2);
  return false;
}

static bool sendDeviceNoteOn()
{
  hostMidiReceived = false;
  const bool sent = DeviceMIDI.noteOn(0, 64, 110);
  Serial.printf("DEVICE_TX_NOTE_ON %u\n", sent ? 1 : 0);
  return sent && waitHostMidi(0x09, 0x90, 64, 110);
}

static bool sendDeviceChannelMessages()
{
  bool ok = true;
  hostMidiReceived = false;
  ok = ok && DeviceMIDI.programChange(0, 10);
  Serial.printf("DEVICE_TX_PROGRAM %u\n", ok ? 1 : 0);
  ok = ok && waitHostMidi(0x0c, 0xc0, 10, 0);

  hostMidiReceived = false;
  const bool bendSent = DeviceMIDI.pitchBend(0, 8192 + 1024);
  Serial.printf("DEVICE_TX_BEND %u\n", bendSent ? 1 : 0);
  ok = ok && bendSent && waitHostMidi(0x0e, 0xe0, 0, 72);

  hostMidiReceived = false;
  const bool pressureSent = DeviceMIDI.channelPressure(0, 77);
  Serial.printf("DEVICE_TX_PRESSURE %u\n", pressureSent ? 1 : 0);
  ok = ok && pressureSent && waitHostMidi(0x0d, 0xd0, 77, 0);

  hostMidiReceived = false;
  const bool polySent = DeviceMIDI.polyPressure(0, 60, 80);
  Serial.printf("DEVICE_TX_POLY_PRESSURE %u\n", polySent ? 1 : 0);
  ok = ok && polySent && waitHostMidi(0x0a, 0xa0, 60, 80);

  hostMidiReceived = false;
  const bool ccSent = DeviceMIDI.controlChange(0, 74, 64);
  Serial.printf("DEVICE_TX_CC %u\n", ccSent ? 1 : 0);
  ok = ok && ccSent && waitHostMidi(0x0b, 0xb0, 74, 64);

  return ok;
}

static bool sendHostChannelMessages()
{
  bool ok = true;
  deviceMidiReceived = false;
  bool sent = usb.midiSendNoteOn(0, 60, 100);
  Serial.printf("MIDI_TX_NOTE_ON %u\n", sent ? 1 : 0);
  ok = ok && sent && waitDeviceMidi(0x09, 0x90, 60, 100);

  deviceMidiReceived = false;
  sent = usb.midiSendProgramChange(0, 10);
  Serial.printf("MIDI_TX_PROGRAM %u\n", sent ? 1 : 0);
  ok = ok && sent && waitDeviceMidi(0x0c, 0xc0, 10, 0);

  deviceMidiReceived = false;
  sent = usb.midiSendPitchBend(0, 8192 + 1024);
  Serial.printf("MIDI_TX_BEND %u\n", sent ? 1 : 0);
  ok = ok && sent && waitDeviceMidi(0x0e, 0xe0, 0, 72);

  deviceMidiReceived = false;
  sent = usb.midiSendChannelPressure(0, 77);
  Serial.printf("MIDI_TX_PRESSURE %u\n", sent ? 1 : 0);
  ok = ok && sent && waitDeviceMidi(0x0d, 0xd0, 77, 0);

  deviceMidiReceived = false;
  sent = usb.midiSendPolyPressure(0, 60, 80);
  Serial.printf("MIDI_TX_POLY_PRESSURE %u\n", sent ? 1 : 0);
  ok = ok && sent && waitDeviceMidi(0x0a, 0xa0, 60, 80);

  deviceMidiReceived = false;
  sent = usb.midiSendControlChange(0, 74, 64);
  Serial.printf("MIDI_TX_CC %u\n", sent ? 1 : 0);
  ok = ok && sent && waitDeviceMidi(0x0b, 0xb0, 74, 64);

  return ok;
}

static bool sendHostSysEx()
{
  const uint8_t sysex[] = {0xf0, 0x7d, 0x01, 0x02, 0xf7};
  deviceMidiReceived = false;
  sysexFirstSeen = false;
  sysexLastSeen = false;
  const bool sent = usb.midiSendSysEx(sysex, sizeof(sysex));
  Serial.printf("MIDI_TX_SYSEX %u\n", sent ? 1 : 0);
  const uint32_t start = millis();
  while (millis() - start < 3000)
  {
    pollDeviceMidi();
    if (sysexFirstSeen && sysexLastSeen)
    {
      return sent;
    }
    delay(10);
  }
  Serial.printf("DEVICE_SYSEX_TIMEOUT first=%u last=%u\n", sysexFirstSeen ? 1 : 0, sysexLastSeen ? 1 : 0);
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_midi");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    {
                      hostCin = message.codeIndex;
                      hostStatus = message.status;
                      hostData1 = message.data1;
                      hostData2 = message.data2;
                      hostMidiReceived = true;
                      Serial.printf("MIDI_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                                    message.cable,
                                    message.codeIndex,
                                    message.status,
                                    message.data1,
                                    message.data2);
                    });

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(hostConfig))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY fs");

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.port = ESP_USB_DEVICE_PORT_FULL_SPEED;
  deviceConfig.speed = ESP_USB_DEVICE_SPEED_FULL;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4017;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback MIDI";
  deviceConfig.serialNumber = "espusb-loopback-midi";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY fs");

  if (!waitFor(deviceConnected, 30000))
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n", usb.lastErrorName(), device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);

  bool ok = true;
  ok = ok && sendDeviceNoteOn();
  ok = ok && sendHostChannelMessages();
  ok = ok && sendDeviceChannelMessages();
  ok = ok && sendHostSysEx();

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  pollDeviceMidi();
  delay(1);
}
