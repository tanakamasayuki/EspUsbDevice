#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbDeviceVideo video(device);
void setup() {
  EspUsbVideoConfig config; config.transport=EspUsbVideoTransport::Bulk;
  config.payloadBytes=65; video.configure(config); device.begin();
}
void loop() { delay(1); }
