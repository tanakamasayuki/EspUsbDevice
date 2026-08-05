#include "EspUsbHost.h"

// DUT for the CCID peer test: an EspUsbHost host driving the EspUsbDevice CCID
// reader in peer_device/. Every command prints one line so the pytest side can
// assert on it.

EspUsbHost usb;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static volatile uint32_t insertedCount = 0;
static volatile uint32_t removedCount = 0;
static volatile bool lastEventPresent = false;

static void printHex(const char *prefix, const uint8_t *data, size_t length)
{
  Serial.print(prefix);
  for (size_t i = 0; i < length; i++)
  {
    Serial.printf("%02x", data[i]);
  }
  Serial.println();
}

static const char *iccStatusName(EspUsbHostCcidIccStatus status)
{
  switch (status)
  {
  case ESP_USB_HOST_CCID_ICC_ACTIVE:
    return "active";
  case ESP_USB_HOST_CCID_ICC_INACTIVE:
    return "inactive";
  case ESP_USB_HOST_CCID_ICC_ABSENT:
    return "absent";
  default:
    return "unknown";
  }
}

static void printEnumeration()
{
  EspUsbHostInterfaceInfo interfaces[ESP_USB_HOST_MAX_INTERFACES];
  EspUsbHostEndpointInfo endpoints[ESP_USB_HOST_MAX_ENDPOINTS];

  const size_t interfaceCount = usb.getInterfaces(deviceAddress, interfaces, ESP_USB_HOST_MAX_INTERFACES);
  const size_t endpointCount = usb.getEndpoints(deviceAddress, endpoints, ESP_USB_HOST_MAX_ENDPOINTS);

  uint8_t ccidInterface = 0xff;
  for (size_t i = 0; i < interfaceCount; i++)
  {
    const EspUsbHostInterfaceInfo &itf = interfaces[i];
    Serial.printf("INTERFACE number=%u class=0x%02x subclass=0x%02x protocol=0x%02x endpoints=%u\n",
                  itf.number,
                  itf.interfaceClass,
                  itf.interfaceSubClass,
                  itf.interfaceProtocol,
                  itf.endpointCount);
    if (itf.interfaceClass == 0x0b)
    {
      ccidInterface = itf.number;
    }
  }

  bool bulkIn = false;
  bool bulkOut = false;
  bool interruptIn = false;
  for (size_t i = 0; i < endpointCount; i++)
  {
    const EspUsbHostEndpointInfo &ep = endpoints[i];
    Serial.printf("ENDPOINT iface=%u ep=0x%02x attrs=0x%02x mps=%u interval=%u\n",
                  ep.interfaceNumber,
                  ep.address,
                  ep.attributes,
                  ep.maxPacketSize,
                  ep.interval);
    if (ep.interfaceNumber != ccidInterface)
    {
      continue;
    }
    const uint8_t type = ep.attributes & 0x03;
    if (type == 0x02 && (ep.address & 0x80))
    {
      bulkIn = true;
    }
    else if (type == 0x02)
    {
      bulkOut = true;
    }
    else if (type == 0x03 && (ep.address & 0x80))
    {
      interruptIn = true;
    }
  }

  Serial.printf("CCID_ENUM interface=%u bulk_in=%u bulk_out=%u interrupt_in=%u interfaces=%u\n",
                ccidInterface == 0xff ? 0 : 1,
                bulkIn ? 1 : 0,
                bulkOut ? 1 : 0,
                interruptIn ? 1 : 0,
                static_cast<unsigned>(interfaceCount));
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onCcidCardInserted([](const EspUsbHostCcidSlotEvent &event)
                         {
                           insertedCount++;
                           lastEventPresent = event.present;
                         });
  usb.onCcidCardRemoved([](const EspUsbHostCcidSlotEvent &event)
                        {
                          removedCount++;
                          lastEventPresent = event.present;
                        });

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.printf("HOST_CONNECTED address=%u vid=%04x pid=%04x interfaces=%u\n",
                                        device.address,
                                        device.vid,
                                        device.pid,
                                        device.configurationInterfaceCount);
                        });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'i')
    {
      if (connected)
      {
        printEnumeration();
      }
      else
      {
        Serial.println("CCID_ENUM interface=0 bulk_in=0 bulk_out=0 interrupt_in=0 interfaces=0");
      }
    }
    else if (command == 'o')
    {
      Serial.printf("CCID_OPEN %u\n", connected && usb.ccidOpen(deviceAddress) ? 1 : 0);
    }
    else if (command == 'd')
    {
      EspUsbHostCcidInterface info;
      if (!usb.ccidGetInterface(info, deviceAddress))
      {
        Serial.println("CCID_INTERFACE unavailable");
      }
      else
      {
        Serial.printf("CCID_INTERFACE iface=%u in=0x%02x out=0x%02x interrupt=0x%02x classDesc=%u bcd=%04x slots=%u voltage=0x%02x protocols=0x%08lx features=0x%08lx maxMessage=%lu exchange=%u\n",
                      info.interfaceNumber,
                      info.inEndpoint,
                      info.outEndpoint,
                      info.interruptEndpoint,
                      info.hasClassDescriptor ? 1 : 0,
                      info.bcdCCID,
                      info.slotCount,
                      info.voltageSupport,
                      (unsigned long)info.protocols,
                      (unsigned long)info.features,
                      (unsigned long)info.maxMessageLength,
                      (unsigned)info.exchangeLevel);
      }
    }
    else if (command == 's')
    {
      EspUsbHostCcidStatus status;
      const bool ok = connected && usb.ccidGetStatus(status, 0, deviceAddress);
      Serial.printf("CCID_STATUS ok=%u icc=%s present=%u active=%u command=%u error=0x%02x\n",
                    ok ? 1 : 0,
                    ok ? iccStatusName(status.iccStatus) : "unknown",
                    status.present ? 1 : 0,
                    status.active ? 1 : 0,
                    (unsigned)status.commandStatus,
                    status.error);
    }
    else if (command == 'p')
    {
      uint8_t atr[ESP_USB_HOST_CCID_MAX_ATR] = {};
      size_t atrLength = 0;
      const bool ok = connected && usb.ccidPowerOn(atr, sizeof(atr), &atrLength,
                                                   ESP_USB_HOST_CCID_VOLTAGE_AUTO, 0, deviceAddress);
      Serial.printf("CCID_POWER_ON ok=%u len=%u error=0x%02x\n",
                    ok ? 1 : 0,
                    static_cast<unsigned>(atrLength),
                    usb.ccidLastError(deviceAddress));
      printHex("CCID_ATR data=", atr, atrLength);
    }
    else if (command == 'f')
    {
      Serial.printf("CCID_POWER_OFF %u\n", connected && usb.ccidPowerOff(0, deviceAddress) ? 1 : 0);
    }
    else if (command == 'g')
    {
      EspUsbHostCcidCardInfo card;
      const bool ok = connected && usb.ccidGetCardInfo(card, 0, deviceAddress);
      Serial.printf("CCID_CARD ok=%u standard=\"%s\" code=0x%02x level=%u name=\"%s\" nameCode=0x%04x pcsc=%u\n",
                    ok ? 1 : 0,
                    card.standardText,
                    card.standardCode,
                    card.level,
                    card.cardNameText,
                    card.cardName,
                    card.pcscStorageAtr ? 1 : 0);
    }
    else if (command == 'a')
    {
      static const uint8_t getUid[] = {0xff, 0xca, 0x00, 0x00, 0x00};
      uint8_t response[64] = {};
      size_t responseLength = 0;
      uint16_t statusWord = 0;
      const bool ok = connected && usb.ccidApdu(getUid, sizeof(getUid), response, sizeof(response),
                                                &responseLength, &statusWord, 0, deviceAddress);
      Serial.printf("CCID_APDU ok=%u sw=%04x len=%u\n",
                    ok ? 1 : 0, statusWord, static_cast<unsigned>(responseLength));
      printHex("CCID_APDU data=", response, responseLength);
    }
    else if (command == 'e')
    {
      // The device's echo instruction: CLA 80 INS 01, four data bytes back.
      static const uint8_t echo[] = {0x80, 0x01, 0x00, 0x00, 0x04, 0xde, 0xad, 0xbe, 0xef};
      uint8_t response[64] = {};
      size_t responseLength = 0;
      uint16_t statusWord = 0;
      const bool ok = connected && usb.ccidApdu(echo, sizeof(echo), response, sizeof(response),
                                                &responseLength, &statusWord, 0, deviceAddress);
      Serial.printf("CCID_ECHO ok=%u sw=%04x len=%u\n",
                    ok ? 1 : 0, statusWord, static_cast<unsigned>(responseLength));
      printHex("CCID_ECHO data=", response, responseLength);
    }
    else if (command == 'x')
    {
      // An instruction the emulated card does not implement: the exchange must
      // still succeed, with the card's own 6D00 as the status word.
      static const uint8_t unknown[] = {0x00, 0xb0, 0x00, 0x00, 0x00};
      uint8_t response[64] = {};
      size_t responseLength = 0;
      uint16_t statusWord = 0;
      const bool ok = connected && usb.ccidApdu(unknown, sizeof(unknown), response, sizeof(response),
                                                &responseLength, &statusWord, 0, deviceAddress);
      Serial.printf("CCID_UNKNOWN ok=%u sw=%04x len=%u\n",
                    ok ? 1 : 0, statusWord, static_cast<unsigned>(responseLength));
    }
    else if (command == 'c')
    {
      static const uint8_t payload[] = {0x01, 0x02, 0x03};
      uint8_t response[64] = {};
      size_t responseLength = 0;
      const bool ok = connected && usb.ccidEscape(payload, sizeof(payload), response, sizeof(response),
                                                  &responseLength, 0, deviceAddress);
      Serial.printf("CCID_ESCAPE ok=%u len=%u\n", ok ? 1 : 0, static_cast<unsigned>(responseLength));
      printHex("CCID_ESCAPE data=", response, responseLength);
    }
    else if (command == 'm')
    {
      // Raw GetSlotStatus: checks that bSeq stays in step after the exchanges
      // above and that the device answers the message type it should.
      EspUsbHostCcidResponse raw;
      const bool ok = connected && usb.ccidMessage(0x65, nullptr, nullptr, 0, raw, 0, deviceAddress);
      Serial.printf("CCID_MESSAGE ok=%u type=0x%02x status=0x%02x error=0x%02x len=%u\n",
                    ok ? 1 : 0,
                    raw.messageType,
                    raw.status,
                    raw.error,
                    static_cast<unsigned>(raw.length));
    }
    else if (command == 'P')
    {
      // GetParameters (0x6c): the device answers RDR_to_PC_Parameters with the
      // T=1 parameter block.
      EspUsbHostCcidResponse raw;
      const bool ok = connected && usb.ccidMessage(0x6c, nullptr, nullptr, 0, raw, 0, deviceAddress);
      Serial.printf("CCID_PARAMETERS ok=%u type=0x%02x protocol=%u len=%u\n",
                    ok ? 1 : 0,
                    raw.messageType,
                    raw.chainParameter,
                    static_cast<unsigned>(raw.length));
    }
    else if (command == 'A')
    {
      Serial.printf("CCID_ABORT %u\n", connected && usb.ccidAbort(0, deviceAddress) ? 1 : 0);
    }
    else if (command == 'n')
    {
      Serial.printf("CCID_EVENTS inserted=%lu removed=%lu present=%u\n",
                    static_cast<unsigned long>(insertedCount),
                    static_cast<unsigned long>(removedCount),
                    lastEventPresent ? 1 : 0);
    }
    else if (command == 'z')
    {
      insertedCount = 0;
      removedCount = 0;
      Serial.println("CCID_EVENTS_RESET");
    }
  }
  delay(1);
}
