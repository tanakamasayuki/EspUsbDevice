#include "EspUsbHost.h"

EspUsbHost usb;

static const uint8_t OUTPUT_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'o', 'u', 't', 'p', 'u', 't'};
static const uint8_t FEATURE_REPORT[63] = {
    'h', 'o', 's', 't', ' ', 'f', 'e', 'a', 't', 'u', 'r', 'e'};

void setup()
{
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          Serial.printf("HOST_CONNECTED vid=%04x pid=%04x\n", device.vid, device.pid);
                        });

  usb.onHIDVendorInput([](const EspUsbHostHIDVendorInput &input)
                       {
                         Serial.print("VENDOR ");
                         for (size_t i = 0; i < input.reportLength && input.reportData[i] != 0; i++)
                         {
                           Serial.write(input.reportData[i]);
                         }
                         Serial.println();
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
    const char command = static_cast<char>(Serial.read());
    if (command == 'o')
    {
      Serial.printf("SEND_OUTPUT %u\n", usb.sendHIDVendorOutput(OUTPUT_REPORT, sizeof(OUTPUT_REPORT)) ? 1 : 0);
    }
    else if (command == 'f')
    {
      Serial.printf("SEND_FEATURE %u\n", usb.sendHIDVendorFeature(FEATURE_REPORT, sizeof(FEATURE_REPORT)) ? 1 : 0);
    }
  }
  delay(1);
}
