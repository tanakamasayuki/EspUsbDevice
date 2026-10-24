// en: Bridge basic messages between USB MIDI and a MIDI UART.
// ja: USB MIDIとMIDI用UARTの間で基本メッセージを中継する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceMidi MIDI(device);

static constexpr int MIDI_RX_PIN = 39;
static constexpr int MIDI_TX_PIN = 40;

static const int8_t CIN_TO_SERIAL_SIZE[16] = {
    -1, -1, 2, 3,
    3, 1, 2, 3,
    3, 3, 3, 3,
    2, 2, 3, 1,
};

static bool readSerialMidiMessage(EspUsbDeviceMidiPacket &packet)
{
  if (!Serial1.available())
  {
    return false;
  }

  const uint8_t status = Serial1.read();
  if ((status & 0x80) == 0)
  {
    return false;
  }

  uint8_t cin = 0;
  uint8_t size = 0;
  switch (status & 0xf0)
  {
  case 0x80:
    cin = ESP_USB_DEVICE_MIDI_CIN_NOTE_OFF;
    size = 3;
    break;
  case 0x90:
    cin = ESP_USB_DEVICE_MIDI_CIN_NOTE_ON;
    size = 3;
    break;
  case 0xa0:
    cin = ESP_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS;
    size = 3;
    break;
  case 0xb0:
    cin = ESP_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE;
    size = 3;
    break;
  case 0xc0:
    cin = ESP_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE;
    size = 2;
    break;
  case 0xd0:
    cin = ESP_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE;
    size = 2;
    break;
  case 0xe0:
    cin = ESP_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE;
    size = 3;
    break;
  default:
    return false;
  }

  const uint32_t start = millis();
  // en: Read only bytes already buffered; application message boundaries must be tracked separately.
  // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
  while (Serial1.available() < size - 1)
  {
    if (millis() - start > 10)
    {
      return false;
    }
    delay(1);
  }

  packet.header = cin;
  packet.byte1 = status;
  packet.byte2 = size > 1 ? Serial1.read() : 0;
  packet.byte3 = size > 2 ? Serial1.read() : 0;
  return true;
}

static void writeSerialMidiMessage(const EspUsbDeviceMidiPacket &packet)
{
  const uint8_t cin = packet.header & 0x0f;
  const int8_t size = CIN_TO_SERIAL_SIZE[cin];
  if (size <= 0)
  {
    return;
  }

  Serial1.write(packet.byte1);
  if (size > 1)
  {
    Serial1.write(packet.byte2);
  }
  if (size > 2)
  {
    Serial1.write(packet.byte3);
  }
  Serial1.flush();
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  Serial1.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4019;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MIDI Interface";
  config.serialNumber = "espusb-midi-interface";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI interface ready");
}

void loop()
{
  EspUsbDeviceMidiPacket packet;

  if (readSerialMidiMessage(packet))
  {
    if (MIDI.writePacket(packet))
    {
      Serial.printf("DIN_TO_USB status=0x%02x\n", packet.byte1);
    }
  }

  // en: Decode MIDI packet fields; USB cable and MIDI channel are different selectors.
  // ja: MIDI packetのfieldを解釈する。USB cableとMIDI channelは別の指定。
  while (MIDI.readPacket(packet))
  {
    writeSerialMidiMessage(packet);
    Serial.printf("USB_TO_DIN cin=0x%02x status=0x%02x\n", packet.header & 0x0f, packet.byte1);
  }

  delay(1);
}
