#include "EspUsbHost.h"
#include <string.h>

// Host side for the HID keyboard + bulk Vendor composite test. Verifies (1) the
// device enumerates with both interfaces claimed, no duplicate endpoint address,
// and distinct interface numbers (the descriptor-duplication regression would
// break exactly this), and (2) both classes actually function.

EspUsbHost usb;

static const uint16_t DEVICE_PID = 0x4024;
static uint8_t deviceAddress = 0;
static uint16_t devicePid = 0;

static void reportEnumeration()
{
  if (deviceAddress == 0)
  {
    Serial.println("HOST_ENUM pid=0000 ifcount=0 eps=0 dup=1 hid=0 vendor=0 claimok=0");
    return;
  }

  EspUsbHostInterfaceInfo interfaces[12];
  const size_t ifCount = usb.getInterfaces(deviceAddress, interfaces, 12);

  uint8_t hidCount = 0, vendorCount = 0, claimOk = 1, ifNumDup = 0;
  for (size_t i = 0; i < ifCount; i++)
  {
    switch (interfaces[i].interfaceClass)
    {
    case 0x03:
      hidCount++;
      break;
    case 0xff:
      vendorCount++;
      break;
    }
    if (interfaces[i].claimAttempted && interfaces[i].claimResult != ESP_OK)
    {
      claimOk = 0;
    }
    for (size_t j = i + 1; j < ifCount; j++)
    {
      if (interfaces[i].number == interfaces[j].number)
      {
        ifNumDup = 1;
      }
    }
    Serial.printf("HOST_IF num=%u class=0x%02x claimed=%u result=0x%x\n",
                  interfaces[i].number, interfaces[i].interfaceClass,
                  interfaces[i].claimed ? 1 : 0, interfaces[i].claimResult);
  }

  EspUsbHostEndpointInfo endpoints[16];
  const size_t epCount = usb.getEndpoints(deviceAddress, endpoints, 16);

  uint8_t dup = 0;
  for (size_t i = 0; i < epCount; i++)
  {
    Serial.printf("HOST_EP addr=0x%02x itf=%u attr=0x%02x mps=%u\n",
                  endpoints[i].address, endpoints[i].interfaceNumber,
                  endpoints[i].attributes, endpoints[i].maxPacketSize);
    for (size_t j = i + 1; j < epCount; j++)
    {
      if (endpoints[i].address == endpoints[j].address)
      {
        dup = 1;
      }
    }
  }

  Serial.printf("HOST_ENUM pid=%04x ifcount=%u eps=%u dup=%u hid=%u vendor=%u ifnumdup=%u claimok=%u\n",
                devicePid, static_cast<unsigned>(ifCount), static_cast<unsigned>(epCount),
                dup, hidCount, vendorCount, ifNumDup, claimOk);
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("HOST_SEEN vid=%04x pid=%04x supported=%u ifcount=%u\n",
                                        device.vid, device.pid, device.supported ? 1 : 0,
                                        device.configurationInterfaceCount);
                          if (device.pid != DEVICE_PID)
                          {
                            return; // ignore the transient USB-Serial/JTAG.
                          }
                          deviceAddress = device.address;
                          devicePid = device.pid;
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x ifcount=%u\n",
                                        device.vid, device.pid, device.configurationInterfaceCount);
                        });

  usb.onKeyboard([](const EspUsbHostKeyboardEvent &event)
                 {
                   if (event.pressed && event.ascii)
                   {
                     Serial.printf("KEY %c\n", static_cast<char>(event.ascii));
                   }
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
    char command = Serial.read();
    if (command == 'e')
    {
      reportEnumeration();
    }
    else if (command == 'v')
    {
      const bool opened = deviceAddress && usb.vendorOpen(deviceAddress);
      Serial.printf("VENDOR_OPEN ok=%u err=%s\n", opened ? 1 : 0, usb.lastErrorName());
      const uint8_t payload[] = "ping";
      const bool wrote = opened && usb.vendorWrite(payload, sizeof(payload) - 1, deviceAddress);
      Serial.printf("VENDOR_WRITE ok=%u err=%s\n", wrote ? 1 : 0, usb.lastErrorName());
      uint8_t buffer[64] = {};
      size_t length = 0;
      const uint32_t started = millis();
      while (wrote && length == 0 && millis() - started < 1000)
      {
        length = usb.vendorRead(buffer, sizeof(buffer) - 1, deviceAddress);
        if (length == 0)
        {
          delay(5);
        }
      }
      buffer[length] = '\0';
      Serial.printf("VENDOR_READ len=%u\n", static_cast<unsigned>(length));
      Serial.printf("VENDOR_ECHO ok=%u data=%s\n", (wrote && length > 0) ? 1 : 0,
                    reinterpret_cast<const char *>(buffer));
    }
  }
  delay(1);
}
