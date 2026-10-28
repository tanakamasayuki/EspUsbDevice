#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceHidKeyboard keyboard(device);
void setup() {
  Serial.begin(115200);
  if (!device.begin()) Serial.println(device.lastErrorName());
}
void loop() {
  // Type into the separate debug serial port to send keys over the data USB.
  // Select a text editor on the USB Host before entering text.
  if (device.ready() && Serial.available()) keyboard.tapKey(char(Serial.read()));
  delay(1);
}
