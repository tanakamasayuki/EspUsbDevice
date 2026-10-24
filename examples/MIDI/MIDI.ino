// en: Generate USB MIDI notes and inspect received event packets.
// ja: USB MIDIの音符を生成し、受信event packetを調べる。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"

// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceMidi MIDI(device);

static uint32_t lastNoteMs = 0;
static uint8_t stepIndex = 0;
static const uint8_t NOTES[] = {60, 64, 67, 72};

static void sendStep()
{
  const uint8_t previous = NOTES[(stepIndex + 3) % 4];
  const uint8_t current = NOTES[stepIndex];

  MIDI.noteOff(0, previous, 0);
  MIDI.noteOn(0, current, 96);
  MIDI.controlChange(0, 74, 32 + (stepIndex * 24));

  Serial.printf("MIDI_TX note=%u\n", current);
  stepIndex = (stepIndex + 1) % 4;
}

static void logPacket(const EspUsbDeviceMidiPacket &packet)
{
  Serial.printf("MIDI_RX header=0x%02x cin=0x%02x b1=0x%02x b2=%u b3=%u\n",
                packet.header,
                packet.header & 0x0f,
                packet.byte1,
                packet.byte2,
                packet.byte3);
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4017;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice MIDI";
  config.serialNumber = "espusb-midi";

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }

  Serial.println("USB MIDI ready");
}

void loop()
{
  const uint32_t now = millis();
  if (now - lastNoteMs >= 1000)
  {
    lastNoteMs = now;
    sendStep();
  }

  EspUsbDeviceMidiPacket packet;
  // en: Decode MIDI packet fields; USB cable and MIDI channel are different selectors.
  // ja: MIDI packetのfieldを解釈する。USB cableとMIDI channelは別の指定。
  while (MIDI.readPacket(packet))
  {
    logPacket(packet);
  }

  delay(1);
}
