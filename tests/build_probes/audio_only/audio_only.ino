#include <EspUsbDevice.h>
EspUsbDevice device;
EspUsbAudioFunction audio(device, EspUsbAudioProtocol::Uac2);
void setup() {
  audio.addCaptureStream().addFormat({48000, 2, 2, 16});
  EspUsbAudioConfig config; config.eventCapacity = 0; config.captureBufferFrames = 0;
  audio.configure(config); device.begin();
}
void loop() { delay(1); }
