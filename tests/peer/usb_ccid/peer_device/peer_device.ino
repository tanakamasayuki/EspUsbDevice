#include "EspUsbDevice.h"

// Peer device for the CCID test: a USB smart card reader with one slot and an
// emulated card. Card presence is driven over the serial console so the host
// side can assert on both states deterministically.

EspUsbDevice device;
EspUsbDeviceCcid Ccid(device);

// PC/SC synthetic ATR for a contactless storage card: historical bytes carry
// the PC/SC RID A0 00 00 03 06, standard 03 (ISO 14443 A part 3) and card name
// 00 01 (MIFARE Classic 1K). The host identifies the card from exactly this.
static const uint8_t CARD_ATR[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6a,
};

static const uint8_t CARD_UID[] = {0x04, 0x11, 0x22, 0x33};

static volatile uint32_t powerOnCount = 0;
static volatile uint32_t powerOffCount = 0;
static volatile uint32_t escapeCount = 0;

static size_t answerApdu(const uint8_t *apdu, size_t length, uint8_t *response, size_t capacity)
{
  // Get UID (PC/SC pseudo APDU FF CA 00 00 00).
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

  // Echo: CLA 80, INS 01, Lc bytes of data back plus 9000.
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

  // Instruction not supported.
  response[0] = 0x6d;
  response[1] = 0x00;
  return 2;
}

static size_t answerEscape(const uint8_t *data, size_t length, uint8_t *response, size_t capacity)
{
  escapeCount++;
  // Vendor answer: the payload with every byte incremented, so the host can tell
  // it came from the escape handler and not from an APDU.
  if (capacity < length)
  {
    return 0;
  }
  for (size_t i = 0; i < length; i++)
  {
    response[i] = static_cast<uint8_t>(data[i] + 1);
  }
  return length;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  Ccid.onApdu(answerApdu);
  Ccid.onEscape(answerEscape);
  Ccid.onPower([](bool on)
               {
                 if (on)
                 {
                   powerOnCount++;
                 }
                 else
                 {
                   powerOffCount++;
                 }
               });

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4021;
  config.manufacturer = "EspUsbDevice";
  config.product = "EspUsbDevice CCID Reader";
  config.serialNumber = "espusb-usb-ccid";
  Serial.printf("DEVICE_BEGIN %u\n", device.begin(config) ? 1 : 0);
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.println("DEVICE_READY");
    }
    else if (command == 'i')
    {
      const bool ok = Ccid.insertCard(CARD_ATR, sizeof(CARD_ATR));
      Serial.printf("DEVICE_CARD inserted=%u present=%u\n", ok ? 1 : 0, Ccid.cardPresent() ? 1 : 0);
    }
    else if (command == 'r')
    {
      Ccid.removeCard();
      Serial.printf("DEVICE_CARD inserted=0 present=%u\n", Ccid.cardPresent() ? 1 : 0);
    }
    else if (command == 's')
    {
      Serial.printf("DEVICE_STATUS mounted=%u present=%u powered=%u commands=%lu apdus=%lu last=0x%02x\n",
                    Ccid.mounted() ? 1 : 0,
                    Ccid.cardPresent() ? 1 : 0,
                    Ccid.cardPowered() ? 1 : 0,
                    static_cast<unsigned long>(Ccid.commandCount()),
                    static_cast<unsigned long>(Ccid.apduCount()),
                    Ccid.lastMessageType());
    }
    else if (command == 'p')
    {
      Serial.printf("DEVICE_POWER on=%lu off=%lu escape=%lu\n",
                    static_cast<unsigned long>(powerOnCount),
                    static_cast<unsigned long>(powerOffCount),
                    static_cast<unsigned long>(escapeCount));
    }
  }
  delay(1);
}
