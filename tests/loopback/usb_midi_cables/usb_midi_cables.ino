// Multi-cable USB MIDI loopback: a 4-cable device talking to the host stack on
// the same board.
//
// tests/loopback/usb_midi covers the default single-cable device; this one covers
// what changes when a MIDI function exposes several cables over the one pair of
// bulk endpoints. The descriptor bytes are checked by tests/unit/midi_descriptor,
// so what is left to prove on real hardware is that the cable number in each
// packet header survives the wire in both directions - that a note sent on cable 3
// does not arrive as cable 0.

#include "EspUsbDevice.h"
#include "EspUsbHost.h"

static constexpr uint8_t CABLE_COUNT = 4;

EspUsbDevice device;
EspUsbDeviceMidi DeviceMIDI(device, CABLE_COUNT);
EspUsbHost usb;

static volatile bool deviceConnected = false;
static volatile bool hostMidiReceived = false;
static volatile bool deviceMidiReceived = false;
static volatile uint8_t hostCable = 0;
static volatile uint8_t hostCin = 0;
static volatile uint8_t hostStatus = 0;
static volatile uint8_t hostData1 = 0;
static volatile uint8_t hostData2 = 0;
static volatile uint8_t deviceCable = 0;
static volatile uint8_t deviceCin = 0;
static volatile uint8_t deviceStatus = 0;
static volatile uint8_t deviceData1 = 0;
static volatile uint8_t deviceData2 = 0;

static void pollDeviceMidi()
{
  EspUsbDeviceMidiPacket packet;
  while (DeviceMIDI.readPacket(packet))
  {
    deviceCable = packet.header >> 4;
    deviceCin = packet.header & 0x0f;
    deviceStatus = packet.byte1;
    deviceData1 = packet.byte2;
    deviceData2 = packet.byte3;
    deviceMidiReceived = true;
    Serial.printf("DEVICE_RX cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                  deviceCable,
                  deviceCin,
                  deviceStatus,
                  deviceData1,
                  deviceData2);
  }
}

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

static bool waitHostMidi(uint8_t cable, uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    if (hostMidiReceived && hostCable == cable && hostCin == cin &&
        hostStatus == status && hostData1 == data1 && hostData2 == data2)
    {
      return true;
    }
    pollDeviceMidi();
    delay(10);
  }
  Serial.printf("MIDI_RX_TIMEOUT cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                hostCable,
                hostCin,
                hostStatus,
                hostData1,
                hostData2);
  return false;
}

static bool waitDeviceMidi(uint8_t cable, uint8_t cin, uint8_t status, uint8_t data1, uint8_t data2, uint32_t timeoutMs = 3000)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    pollDeviceMidi();
    if (deviceMidiReceived && deviceCable == cable && deviceCin == cin &&
        deviceStatus == status && deviceData1 == data1 && deviceData2 == data2)
    {
      return true;
    }
    delay(10);
  }
  Serial.printf("DEVICE_RX_TIMEOUT cable=%u cin=%02x status=%02x data1=%u data2=%u\n",
                deviceCable,
                deviceCin,
                deviceStatus,
                deviceData1,
                deviceData2);
  return false;
}

// Note number varies with the cable so a packet that lands on the wrong cable is
// distinguishable from one that lands on the right cable with the wrong payload.
static bool sendDeviceNoteOnPerCable()
{
  bool ok = true;
  for (uint8_t cable = 0; cable < CABLE_COUNT; cable++)
  {
    hostMidiReceived = false;
    const uint8_t note = static_cast<uint8_t>(60 + cable);
    const bool sent = DeviceMIDI.noteOn(0, note, 100, cable);
    Serial.printf("DEVICE_TX_CABLE %u %u\n", cable, sent ? 1 : 0);
    ok = ok && sent && waitHostMidi(cable, 0x09, 0x90, note, 100);
  }
  return ok;
}

// A cable the host was never told about has no port to arrive on, so the helpers
// refuse it rather than letting it land on some other cable.
static bool rejectUnknownCable()
{
  const bool sent = DeviceMIDI.noteOn(0, 60, 100, CABLE_COUNT);
  Serial.printf("DEVICE_TX_UNKNOWN_CABLE %u\n", sent ? 1 : 0);
  return !sent;
}

// midiSendNoteOn() has no cable argument, so the packet is assembled here: the
// cable number is the high nibble of the header byte.
static bool sendHostNoteOnPerCable()
{
  bool ok = true;
  for (uint8_t cable = 0; cable < CABLE_COUNT; cable++)
  {
    deviceMidiReceived = false;
    const uint8_t note = static_cast<uint8_t>(70 + cable);
    const uint8_t packet[] = {static_cast<uint8_t>((cable << 4) | 0x09), 0x90, note, 90};
    const bool sent = usb.midiSend(packet, sizeof(packet));
    Serial.printf("MIDI_TX_CABLE %u %u\n", cable, sent ? 1 : 0);
    ok = ok && sent && waitDeviceMidi(cable, 0x09, 0x90, note, 90);
  }
  return ok;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_midi_cables");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  usb.onMidiMessage([](const EspUsbHostMidiMessage &message)
                    {
                      hostCable = message.cable;
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
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4017;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback MIDI Cables";
  deviceConfig.serialNumber = "espusb-loopback-midi-cables";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.printf("DEVICE_READY fs cables=%u/%u bytes=%u\n",
                DeviceMIDI.inCableCount(),
                DeviceMIDI.outCableCount(),
                DeviceMIDI.descriptorLength());

  if (!waitFor(deviceConnected, 30000))
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n", usb.lastErrorName(), device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }

  delay(500);

  bool ok = true;
  ok = ok && sendDeviceNoteOnPerCable();
  ok = ok && rejectUnknownCable();
  ok = ok && sendHostNoteOnPerCable();

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  pollDeviceMidi();
  delay(1);
}
