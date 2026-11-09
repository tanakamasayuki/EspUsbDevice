#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
EspUsbDevice device;
EspUsbDeviceHidVendor small(device, 7), large(device, 129);
std::atomic<bool> stress{false}, taskActive{false};
std::atomic<unsigned> outputs{0}, features{0}, featureWrites{0};
uint8_t smallPayload[7], largePayload[129];
void featureTask(void *) {
  uint8_t data[129]; bool next = false;
  while (stress.load()) {
    memset(data, next ? 0x31 : 0x32, sizeof(data)); next = !next;
    if (large.setFeatureReport(data, sizeof(data)) != ESP_OK) break;
    ++featureWrites; vTaskDelay(1);
  }
  taskActive.store(false); vTaskDelete(nullptr);
}
void stopTask() {
  stress.store(false);
  while (taskActive.load()) delay(1);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  small.onOutputReport([](const EspUsbDeviceHidReport &) { ++outputs; });
  large.onOutputReport([](const EspUsbDeviceHidReport &) { ++outputs; });
  small.onFeatureReport([](const EspUsbDeviceHidReport &) { ++features; });
  large.onFeatureReport([](const EspUsbDeviceHidReport &) { ++features; });
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    EspUsbDeviceConfig config; config.product = "v3 HID report state";
    bool ok = device.begin(config);
    memset(smallPayload, 0x16, sizeof(smallPayload)); memset(largePayload, 0x21, sizeof(largePayload));
    ok &= small.setFeatureReport(smallPayload, sizeof(smallPayload)) == ESP_OK;
    ok &= large.setFeatureReport(largePayload, sizeof(largePayload)) == ESP_OK;
    // Callback replacement and cross-Function Feature updates are rejected.
    ok &= small.onOutputReport(nullptr) == ESP_ERR_INVALID_STATE && large.onFeatureReport(nullptr) == ESP_ERR_INVALID_STATE;
    ok &= small.setFeatureReport(smallPayload, sizeof(smallPayload), large.compositeReportId()) == ESP_ERR_INVALID_ARG;
    ok &= large.setFeatureReport(largePayload, sizeof(largePayload) + 1) == ESP_ERR_INVALID_SIZE;
    Serial.printf("STARTED %u small=%u large=%u\r\n", unsigned(ok), unsigned(small.compositeReportId()), unsigned(large.compositeReportId()));
  } else if (command == "publish") {
    memset(smallPayload, 0x46, sizeof(smallPayload)); memset(largePayload, 0x41, sizeof(largePayload));
    Serial.printf("PUBLISHED %u\r\n", unsigned(small.sendInput(smallPayload, sizeof(smallPayload)) && large.sendInput(largePayload, sizeof(largePayload))));
  } else if (command == "feature") {
    memset(smallPayload, 0x76, sizeof(smallPayload)); memset(largePayload, 0x71, sizeof(largePayload));
    Serial.printf("FEATURE_UPDATED %u\r\n", unsigned(small.setFeatureReport(smallPayload, sizeof(smallPayload)) == ESP_OK && large.setFeatureReport(largePayload, sizeof(largePayload)) == ESP_OK));
  } else if (command == "events") {
    Serial.printf("EVENTS output=%u feature=%u\r\n", outputs.load(), features.load());
  } else if (command == "stress") {
    featureWrites.store(0); stress.store(true); taskActive.store(true);
    bool ok = xTaskCreate(featureTask, "feature probe", 3072, nullptr, 1, nullptr) == pdPASS;
    if (!ok) { stress.store(false); taskActive.store(false); }
    while (ok && taskActive.load() && !featureWrites.load()) delay(1);
    Serial.printf("STRESS %u\r\n", unsigned(ok && featureWrites.load()));
  } else if (command == "stress_stop") {
    stopTask(); Serial.println("STRESS_STOPPED");
  } else if (command == "suspended") {
    const uint32_t until = millis() + 3000;
    while (!device.suspended() && int32_t(until - millis()) > 0) delay(1);
    memset(smallPayload, 0x46, sizeof(smallPayload)); memset(largePayload, 0x41, sizeof(largePayload));
    const bool ok = device.started() && device.ready() && device.suspended()
      && !small.sendInput(smallPayload, sizeof(smallPayload)) && !large.sendInput(largePayload, sizeof(largePayload));
    Serial.printf("SUSPEND_REJECTED %u\r\n", unsigned(ok));
  } else if (command == "resumed") {
    const uint32_t until = millis() + 3000;
    while (device.suspended() && int32_t(until - millis()) > 0) delay(1);
    Serial.printf("RESUMED %u\r\n", unsigned(device.started() && device.ready() && !device.suspended()));
  } else if (command == "stop") {
    stopTask();
    const bool ok = device.end() == ESP_OK && small.setFeatureReport(smallPayload, sizeof(smallPayload)) == ESP_ERR_INVALID_STATE;
    Serial.println(ok ? "STOPPED" : "STOP_FAILED");
  }
}
