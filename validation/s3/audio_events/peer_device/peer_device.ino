#include <Arduino.h>
#include <EspUsbDevice.h>
#include "../../../../src/internal/EspUsbAudioEvent.h"
#include <esp_heap_caps.h>
#include <atomic>
EspUsbDevice device;
EspUsbAudioFunction *audio = nullptr;
EspUsbAudioPlaybackStream *playback = nullptr;
espusb::internal::AudioEventQueue queue;
std::atomic<unsigned> finished{0};
void producer(void *context) {
  const bool capture = uintptr_t(context);
  for (unsigned i = 1; i <= 100; ++i) {
    espusb::internal::AudioRuntimeEvent event;
    event.target = capture ? espusb::internal::AudioRuntimeEventTarget::Capture : espusb::internal::AudioRuntimeEventTarget::Playback;
    event.value = i;
    queue.push(event);
  }
  ++finished; vTaskDelete(nullptr);
}
bool parallelQueue() {
  if (!queue.configure(257)) return false;
  finished.store(0);
  bool ok = xTaskCreatePinnedToCore(producer, "event-a", 2048, nullptr, 3, nullptr, 0) == pdPASS;
  ok &= xTaskCreatePinnedToCore(producer, "event-b", 2048, reinterpret_cast<void *>(1), 3, nullptr, 1) == pdPASS;
  const uint32_t until = millis() + 3000;
  while (finished.load() < 2 && int32_t(until - millis()) > 0) delay(1);
  unsigned counts[2] = {};
  espusb::internal::AudioRuntimeEvent event;
  while (queue.pop(event)) {
    const unsigned slot = event.target == espusb::internal::AudioRuntimeEventTarget::Capture;
    ok &= event.value == int32_t(++counts[slot]);
  }
  ok &= counts[0] == 100 && counts[1] == 100 && !queue.dropped();
  queue.release(); delay(20); return ok;
}
bool metadata() {
  bool ok = true;
  for (unsigned cap : {0u,1u,17u,257u}) {
    EspUsbDevice local; EspUsbAudioFunction function(local, EspUsbAudioProtocol::Uac2);
    auto &stream = function.addPlaybackStream();
    ok &= stream.addFormat({48000,2,2,16});
    EspUsbAudioConfig config; config.eventCapacity = cap;
    ok &= function.configure(config) == ESP_OK;
    auto invalid = config; invalid.eventCapacity = SIZE_MAX;
    ok &= function.configure(invalid) == ESP_ERR_INVALID_SIZE && function.configuration().eventCapacity == cap;
    EspUsbDeviceConfig identity; identity.startTinyUsb = false;
    ok &= local.begin(identity) && function.eventCapacity() == cap;
    ok &= function.configure(config) == ESP_ERR_INVALID_STATE && !stream.addFormat({44100,2,2,16});
    ok &= !function.hasCaptureStream(); function.addCaptureStream(); ok &= !function.hasCaptureStream();
    ok &= local.end() == ESP_OK && !function.eventCapacity();
    ok &= function.configure(config) == ESP_OK && local.begin(identity) && local.end() == ESP_OK;
  }
  return ok && parallelQueue();
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "metadata") {
    bool ok = metadata(); const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (unsigned i = 0; i < 5 && ok; ++i) ok &= metadata();
    const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(after));
  } else if (command.startsWith("start ")) {
    delete audio; audio = new EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2);
    playback = &audio->addPlaybackStream();
    bool ok = playback->addFormat({48000,2,2,16});
    EspUsbAudioConfig config; config.eventCapacity = command.substring(6).toInt();
    ok &= audio->configure(config) == ESP_OK;
    EspUsbDeviceConfig identity; identity.product = "v3 Audio events";
    ok &= device.begin(identity);
    Serial.printf("STARTED %u capacity=%u\r\n", unsigned(ok), unsigned(audio->eventCapacity()));
  } else if (command == "intruder") {
    auto *rejected = new EspUsbAudioFunction(device, EspUsbAudioProtocol::Uac2);
    auto &stream = rejected->addPlaybackStream();
    bool ok = !rejected->begin() && rejected->startError() == ESP_ERR_INVALID_STATE && !stream.available() && !stream.clearBuffer();
    delete rejected;
    ok &= device.ready(); Serial.printf("INTRUDER_REJECTED %u\r\n", unsigned(ok));
  } else if (command.startsWith("drain ")) {
    const unsigned expected = command.substring(6).toInt();
    unsigned count = 0; EspUsbAudioEvent event; bool ok = true;
    while (audio->pollEvent(event)) {
      ok &= event.type == EspUsbAudioEventType::MuteChanged && event.target == EspUsbAudioEventTarget::Playback && !event.channel && event.muted == bool((count + 1) & 1);
      ++count;
    }
    Serial.printf("EVENTS count=%u dropped=%u payload=%u\r\n", count, unsigned(audio->droppedEvents()), unsigned(ok && count == expected));
    audio->clearEvents();
  } else if (command == "drop") {
    delete audio; audio = nullptr;
    Serial.printf("DROPPED %u\r\n", unsigned(!device.ready()));
  } else if (command == "stop") {
    Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
  }
}
