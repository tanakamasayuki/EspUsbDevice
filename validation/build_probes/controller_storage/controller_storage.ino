#include <EspUsbDevice.h>
#include "../../../src/internal/EspUsbDwc2Runtime.h"
EspUsbDevice device;
EspUsbDeviceVendor vendor(device);
EspUsbDeviceCcid reader(device);
void setup() {
  Serial.begin(115200);
  EspUsbCcidConfig card;card.messageBytes=512;reader.configure(card);
  EspUsbDeviceConfig config;
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  config.controller=EspUsbController::HighSpeed;
#else
  config.controller=EspUsbController::FullSpeed;
#endif
  bool ok=device.begin(config);
  Serial.printf("CONTROLLER_STORAGE ok=%u bytes=%u records=%u\r\n",unsigned(ok),unsigned(espusb_dwc2_runtime_bytes()),unsigned(espusb_dwc2_runtime_transfers()));
  device.end();
}
void loop(){delay(1);}
