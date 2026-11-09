#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "../../../src/internal/EspUsbDeviceRestart.h"
// Runtime DFU on the P4X HS controller, driven by the Linux PC as Host.
// Measures what the DETACH spec has to decide: the restart delay after the
// status stage, the appDETACH timer, and end()/reset racing a scheduled restart.
EspUsbDevice device;
EspUsbDeviceDfu *dfu = nullptr;
std::atomic<unsigned> detaches{0}, asked{0};
unsigned refuse = 0;
std::atomic<uint32_t> detachedAt{0};
unsigned mode = 0, restartDelay = 0, reported = 0;
int endAfter = -1;
// Survives esp_restart(), so the PC can tell a new boot from a missed log line.
__NOINIT_ATTR uint32_t bootMagic, bootCount;
void setup() {
  if (bootMagic != 0x5a17c0de) { bootMagic = 0x5a17c0de; bootCount = 0; }
  ++bootCount;
  Serial.begin(115200); Serial.setTimeout(100);
}
void loop() {
  const unsigned n = detaches.load();
  if (n != reported) {
    reported = n;
    Serial.printf("DETACHED n=%u at=%u\r\n", n, unsigned(detachedAt.load()));
    if (endAfter >= 0) {
      while (int32_t(millis() - detachedAt.load() - unsigned(endAfter)) < 0) delay(1);
      const uint32_t begun = millis(); const esp_err_t r = device.end();
      Serial.printf("ENDED err=%d after=%u took=%u\r\n", int(r), unsigned(begun - detachedAt.load()), unsigned(millis() - begun));
    }
  }
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "boot") {
    Serial.printf("BOOT count=%u reason=%d\r\n", unsigned(bootCount), int(esp_reset_reason()));
  } else if (command.startsWith("start ")) {
    unsigned timeout = 1000; refuse = 0; asked = 0; detaches = 0; reported = 0;
    sscanf(command.c_str(), "start %u %u %u %d %u", &mode, &restartDelay, &timeout, &endAfter, &refuse);
    delete dfu; dfu = new EspUsbDeviceDfu(device, EspUsbDeviceDfuMode::Runtime);
    EspUsbDfuConfig config; config.detachTimeoutMs = timeout;
    bool ok = dfu->configure(config) == ESP_OK;
    // The first `refuse` DETACH requests are refused at SETUP.
    ok &= dfu->onDetach([]() { return ++asked > refuse; }) == ESP_OK;
    // mode 0: restart into this app after restartDelay (the default path's
    // mechanism without leaving for the ROM loader). mode 1: do nothing.
    ok &= dfu->setDetachHandler([]() {
      detachedAt = millis(); ++detaches;
      if (mode == 0) espusb::internal::scheduleDeviceRestart(&device, restartDelay, false);
    }) == ESP_OK;
    EspUsbDeviceConfig identity; identity.vid = 0x1209; identity.pid = 0x0001;
    identity.product = "EspUsbDevice DFU detach"; identity.serialNumber = "espusb-dfu-detach";
    ok &= device.begin(identity);
    ok &= dfu->onDetach({}) == ESP_ERR_INVALID_STATE && dfu->setDetachHandler({}) == ESP_ERR_INVALID_STATE;
    EspUsbDevice other; EspUsbDeviceDfu download(other);
    ok &= download.onDetach([]() { return true; }) == ESP_ERR_INVALID_ARG && download.setDetachHandler([]() {}) == ESP_ERR_INVALID_ARG;
    Serial.printf("STARTED %u\r\n", unsigned(ok));
  } else if (command == "counts") {
    Serial.printf("COUNTS asked=%u detached=%u\r\n", unsigned(asked.load()), unsigned(detaches.load()));
  } else if (command == "stop") {
    Serial.println(!device.started() || device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
