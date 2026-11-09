#include <Arduino.h>
#include "esp_private/usb_phy.h"
#include "soc/usb_wrap_struct.h"
// FS pad link check without either USB stack: one board drives or pulls its
// own D+/D- through the USB_WRAP test and pull override registers, the other
// reads its pads. Shows whether the two native ports are electrically joined.
usb_phy_handle_t phy = nullptr;
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void pulls(int dpUp, int dpDown, int dmUp, int dmDown) {
  usb_wrap_otg_conf_reg_t c = {}; c.val = USB_WRAP.otg_conf.val;
  c.pad_pull_override = 1; c.dp_pullup = dpUp; c.dp_pulldown = dpDown; c.dm_pullup = dmUp; c.dm_pulldown = dmDown;
  USB_WRAP.otg_conf.val = c.val;
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  int a, b, c, d;
  if (command == "init") {
    usb_phy_config_t config = {};
    config.controller = USB_PHY_CTRL_OTG; config.target = USB_PHY_TARGET_INT;
    config.otg_mode = USB_OTG_MODE_DEVICE; config.otg_speed = USB_PHY_SPEED_FULL;
    const esp_err_t error = phy ? ESP_OK : usb_new_phy(&config, &phy);
    USB_WRAP.test_conf.test_usb_wrap_oe = 0;
    USB_WRAP.test_conf.test_enable = 1;
    pulls(0, 1, 0, 1);
    Serial.printf("INIT %d\r\n", int(error));
  } else if (sscanf(command.c_str(), "drive %d %d", &a, &b) == 2) {
    pulls(0, 0, 0, 0);
    USB_WRAP.test_conf.test_tx_dp = a; USB_WRAP.test_conf.test_tx_dm = b;
    USB_WRAP.test_conf.test_usb_wrap_oe = 1;
    Serial.printf("DRIVEN %d %d\r\n", a, b);
  } else if (sscanf(command.c_str(), "pulls %d %d %d %d", &a, &b, &c, &d) == 4) {
    USB_WRAP.test_conf.test_usb_wrap_oe = 0;
    pulls(a, b, c, d);
    Serial.printf("PULLS %d %d %d %d\r\n", a, b, c, d);
  } else if (command == "read") {
    delay(2);
    Serial.printf("PADS dp=%u dm=%u\r\n", unsigned(USB_WRAP.test_conf.test_rx_dp), unsigned(USB_WRAP.test_conf.test_rx_dm));
  }
}
