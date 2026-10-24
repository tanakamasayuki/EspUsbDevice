// en: Emulate a CCID reader and respond to selected APDUs.
// ja: CCID readerを模擬し、一部のAPDUへ応答する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceCcid Ccid(device);

// en: PC/SC synthetic ATR for a contactless storage card: the historical bytes carry the PC/SC RID A0 00 00 03 06, standard 03 (ISO 14443 A part 3) and card name 00 01 (MIFARE Classic 1K), which is how a host names the card.
// ja: このATRはPC/SC用の模擬データ。実カードから取得した値ではない。
static const uint8_t CARD_ATR[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6a,
};

static const uint8_t CARD_UID[] = {0x04, 0x11, 0x22, 0x33};

static uint32_t lastStatusMs = 0;

static size_t answerApdu(const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity)
{
  Serial.printf("APDU len=%u:", static_cast<unsigned>(length));
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf(" %02x", apdu[i]);
  }
  Serial.println();

  // Get UID (PC/SC pseudo APDU): CLA FF, INS CA.
  if (length >= 4 && apdu[0] == 0xff && apdu[1] == 0xca)
  {
    if (capacity < sizeof(CARD_UID) + 2)
    {
      return 0;
    }
    memcpy(response, CARD_UID, sizeof(CARD_UID));
    response[sizeof(CARD_UID)] = 0x90;
    response[sizeof(CARD_UID) + 1] = 0x00;
    return sizeof(CARD_UID) + 2;
  }

  // Echo: CLA 80, INS 01, Lc bytes of data.
  if (length >= 5 && apdu[0] == 0x80 && apdu[1] == 0x01)
  {
    const size_t dataLength = apdu[4];
    if (length < 5 + dataLength || capacity < dataLength + 2)
    {
      return 0;
    }
    memcpy(response, &apdu[5], dataLength);
    response[dataLength] = 0x90;
    response[dataLength + 1] = 0x00;
    return dataLength + 2;
  }

  // 6D00: instruction not supported.
  response[0] = 0x6d;
  response[1] = 0x00;
  return 2;
}

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: The handler must respect both APDU length and response capacity before writing a reply.
  // ja: 返信を書く前にAPDUの長さと返信領域の容量を確認する。
  Ccid.onApdu(answerApdu);
  Ccid.onPower([](bool on)
               { Serial.printf("CARD %s\n", on ? "activated" : "deactivated"); });

  EspUsbDeviceConfig config;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice CCID Reader";
  config.serialNumber = "espusb-ccid";
  config.pid = 0x4021;
  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin(config))
  {
    Serial.printf("begin failed: %s\n", device.lastErrorName());
    return;
  }

  // Start with a card in the slot. Remove it with 'r' and put it back with 'i'.
  Ccid.insertCard(CARD_ATR, sizeof(CARD_ATR));
  Serial.println("CCID reader ready. 'i' inserts a card, 'r' removes it.");
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      Serial.printf("insert %u\n", Ccid.insertCard(CARD_ATR, sizeof(CARD_ATR)) ? 1 : 0);
    }
    else if (command == 'r')
    {
      Ccid.removeCard();
      Serial.println("removed");
    }
  }

  if (millis() - lastStatusMs >= 2000)
  {
    lastStatusMs = millis();
    Serial.printf("mounted=%u card=%u powered=%u commands=%lu apdus=%lu\n",
                  Ccid.mounted() ? 1 : 0,
                  Ccid.cardPresent() ? 1 : 0,
                  Ccid.cardPowered() ? 1 : 0,
                  static_cast<unsigned long>(Ccid.commandCount()),
                  static_cast<unsigned long>(Ccid.apduCount()));
  }
  delay(10);
}
