#include <EspUsbDevice.h>
#include <atomic>
#include <string.h>

EspUsbDevice device;
EspUsbDeviceCcid reader(device);

// PC/SC synthetic ATR for a contactless storage card: the historical bytes carry
// RID A0 00 00 03 06, standard 03 (ISO 14443 A part 3) and card name 00 01
// (MIFARE Classic 1K). Example data, not read from a real card.
const uint8_t cardAtr[] = {
    0x3b, 0x8f, 0x80, 0x01, 0x80, 0x4f, 0x0c, 0xa0, 0x00, 0x00,
    0x03, 0x06, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x6a,
};
const uint8_t cardUid[] = {0x04, 0x11, 0x22, 0x33};

// The callbacks run on the USB worker and must return at once, so they only
// copy what loop() prints.
portMUX_TYPE apduLock = portMUX_INITIALIZER_UNLOCKED;
uint8_t lastApdu[16];
size_t lastApduLength = 0;
uint32_t printedApdus = 0, lastStatus = 0;
std::atomic<int> powerChange{-1};

size_t answerApdu(const uint8_t *apdu, size_t length, uint8_t *reply, size_t capacity) {
  taskENTER_CRITICAL(&apduLock);
  lastApduLength = length;
  memcpy(lastApdu, apdu, length < sizeof(lastApdu) ? length : sizeof(lastApdu));
  taskEXIT_CRITICAL(&apduLock);

  // Get UID (PC/SC pseudo APDU): FF CA 00 00 00.
  if (length >= 4 && apdu[0] == 0xff && apdu[1] == 0xca) {
    if (capacity < sizeof(cardUid) + 2) return 0; // 0 fails the command.
    memcpy(reply, cardUid, sizeof(cardUid));
    reply[sizeof(cardUid)] = 0x90;
    reply[sizeof(cardUid) + 1] = 0x00;
    return sizeof(cardUid) + 2;
  }
  // Echo: 80 01 00 00 Lc <data> returns <data> 90 00.
  if (length >= 5 && apdu[0] == 0x80 && apdu[1] == 0x01) {
    const size_t dataLength = apdu[4];
    if (length < 5 + dataLength || capacity < dataLength + 2) return 0;
    memcpy(reply, apdu + 5, dataLength);
    reply[dataLength] = 0x90;
    reply[dataLength + 1] = 0x00;
    return dataLength + 2;
  }
  if (capacity < 2) return 0;
  reply[0] = 0x6d; // 6D 00: instruction not supported.
  reply[1] = 0x00;
  return 2;
}

void setup() {
  Serial.begin(115200); // Separate debug connection; also inserts and removes the card.
  EspUsbCcidConfig config; // messageBytes 0: 271 at full speed, 512 at high speed.
  config.name = "EspUsbDevice CCID Reader";
  esp_err_t error = reader.configure(config);
  if (error == ESP_OK) error = reader.onApdu(answerApdu);
  if (error == ESP_OK) error = reader.onPower([](bool on) { powerChange = on ? 1 : 0; });
  if (error != ESP_OK) {
    Serial.printf("CCID setup failed: %d\n", int(error));
    return;
  }
  // Start with a card in the slot. A removable reader accepts this before or
  // after begin(); the host is told once it configures the reader.
  reader.insertCard(cardAtr, sizeof(cardAtr));

  EspUsbDeviceConfig identity;
  identity.pid = 0x4021;
  identity.manufacturer = "EspUsbDevice";
  identity.product = "EspUsbDevice CCID Reader";
  identity.serialNumber = "espusb-ccid";
  if (!device.begin(identity)) {
    Serial.printf("USB start failed: %s\n", device.lastErrorName());
    return;
  }
  Serial.printf("CCID reader ready (%u-byte messages). 'i' inserts a card, 'r' removes it.\n",
      unsigned(reader.messageCapacity()));
}

void loop() {
  if (Serial.available() > 0) {
    const char command = char(Serial.read());
    if (command == 'i') Serial.printf("insert %u\n", unsigned(reader.insertCard(cardAtr, sizeof(cardAtr))));
    else if (command == 'r') Serial.printf("remove %s\n", esp_err_to_name(reader.removeCard()));
  }
  const int power = powerChange.exchange(-1);
  if (power >= 0) Serial.printf("card %s\n", power ? "activated" : "deactivated");

  const uint32_t apdus = reader.apduCount();
  if (apdus != printedApdus) {
    printedApdus = apdus;
    uint8_t copy[sizeof(lastApdu)];
    taskENTER_CRITICAL(&apduLock);
    const size_t length = lastApduLength;
    memcpy(copy, lastApdu, sizeof(copy));
    taskEXIT_CRITICAL(&apduLock);
    Serial.printf("APDU #%lu, %u bytes:", static_cast<unsigned long>(apdus), unsigned(length));
    for (size_t i = 0; i < length && i < sizeof(copy); i++) Serial.printf(" %02x", copy[i]);
    Serial.println(length > sizeof(copy) ? " ..." : "");
  }
  if (millis() - lastStatus >= 2000) {
    lastStatus = millis();
    Serial.printf("mounted=%u card=%u powered=%u commands=%lu apdus=%lu\n",
        unsigned(reader.mounted()), unsigned(reader.cardPresent()), unsigned(reader.cardPowered()),
        static_cast<unsigned long>(reader.commandCount()), static_cast<unsigned long>(apdus));
  }
  delay(10);
}
