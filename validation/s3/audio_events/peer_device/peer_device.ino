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
    EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
    ok &= local.begin(identity) && function.eventCapacity() == cap;
    ok &= function.configure(config) == ESP_ERR_INVALID_STATE && !stream.addFormat({44100,2,2,16});
    ok &= !function.hasCaptureStream(); function.addCaptureStream(); ok &= !function.hasCaptureStream();
    ok &= local.end() == ESP_OK && !function.eventCapacity();
    ok &= function.configure(config) == ESP_OK && local.begin(identity) && local.end() == ESP_OK;
  }
  {
    EspUsbDevice local; EspUsbAudioFunction function(local, EspUsbAudioProtocol::Uac2);
    auto &input = function.addPlaybackStream(); auto &output = function.addCaptureStream();
    ok &= input.addFormat({48000,2,2,16}) && output.addFormat({48000,2,2,16});
    uint8_t buffers[65] = {};
    EspUsbAudioConfig config; config.eventCapacity = 0;
    config.playbackBufferFrames = config.captureBufferFrames = 8;
    config.playbackBuffer = buffers + 1; config.captureBuffer = buffers + 17;
    config.playbackBufferBytes = config.captureBufferBytes = 32;
    EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
    ok &= function.configure(config) == ESP_OK && !local.begin(identity) && local.lastError() == ESP_ERR_INVALID_ARG;
    config.captureBuffer = buffers + 33;
    ok &= function.configure(config) == ESP_OK && local.begin(identity);
    ok &= input.capacity() == 32 && output.capacity() == 32;
    ok &= local.end() == ESP_OK;
    config.playbackBufferFrames = SIZE_MAX;
    ok &= function.configure(config) == ESP_ERR_INVALID_SIZE;
  }
  return ok && parallelQueue();
}
// Reject incomplete capabilities, then repair the same stopped Function.
bool capabilities() {
  bool all = true;
  for (auto protocol : {EspUsbAudioProtocol::Uac1, EspUsbAudioProtocol::Uac2}) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
      EspUsbDevice local; EspUsbAudioFunction function(local, protocol);
      auto &input = function.addPlaybackStream();
      EspUsbAudioCaptureStream *output = nullptr;
      bool ok = input.addFormat({24000,2,2,16}) && input.addFormat({16000,2,2,16});
      if (scenario == 0 || scenario == 1) {
        ok &= input.addFormat({24000,2,3,24});
        if (scenario == 1) ok &= input.addFormat({12000,2,3,24});
      } else if (scenario == 2) {
        output = &function.addCaptureStream();
        ok &= output->addFormat({24000,2,2,16}) && output->addFormat({12000,2,2,16});
      } else if (scenario == 3) {
        ok &= input.addFormat({24000,1,4,32});
      } else {
        // Identical rate sets in different insertion orders are valid.
        ok &= input.addFormat({16000,2,3,24}) && input.addFormat({24000,2,3,24});
        output = &function.addCaptureStream();
        ok &= output->addFormat({16000,2,2,16}) && output->addFormat({24000,2,2,16});
      }
      EspUsbAudioConfig config; config.eventCapacity = 0;
      config.playbackBufferFrames = config.captureBufferFrames = 0;
      EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
      ok &= function.configure(config) == ESP_OK;
      if (scenario != 4) {
        // Settings that disagree are an argument error, not a size.
        ok &= !local.begin(identity) && local.lastError() == ESP_ERR_INVALID_ARG;
        ok &= !local.started() && !local.ready() && !input.capacity() && !function.eventCapacity();
        if (scenario == 0) ok &= input.addFormat({16000,2,3,24});
        else if (scenario == 2) {
          ok &= output->clearFormats() == ESP_OK;
          ok &= output->addFormat({16000,2,2,16}) && output->addFormat({24000,2,2,16});
        } else {
          ok &= input.clearFormats() == ESP_OK && input.addFormat({24000,2,2,16});
        }
      }
      ok &= local.begin(identity) && local.started() && !local.ready() && function.currentSampleRate() == 24000;
      ok &= local.end() == ESP_OK && !local.started() && !input.capacity();
      Serial.printf("CAPABILITY protocol=%u scenario=%u ok=%u\r\n", unsigned(protocol), scenario, unsigned(ok));
      all &= ok;
    }
    {
      EspUsbDevice local; EspUsbAudioFunction function(local, protocol);
      auto &input = function.addPlaybackStream(); auto &output = function.addCaptureStream();
      bool ok = input.addFormat({24000,2,2,16}) && input.addFormat({24000,2,4,32});
      ok &= output.addFormat({24000,2,2,16}) && output.addFormat({24000,2,4,32});
      uint8_t buffers[130], data[80]; memset(buffers,0xc3,sizeof(buffers)); memset(data,0x5a,sizeof(data));
      EspUsbAudioConfig config; config.eventCapacity = 0;
      config.playbackBufferFrames = config.captureBufferFrames = 8;
      config.playbackBuffer = buffers + 1; config.captureBuffer = buffers + 33;
      config.playbackBufferBytes = config.captureBufferBytes = 64;
      EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
      ok &= function.configure(config) == ESP_OK;
      for (unsigned invalidPair = 0; invalidPair < 4; ++invalidPair) {
        auto invalid = config;
        if (invalidPair == 0) invalid.playbackBufferBytes = 0;
        if (invalidPair == 1) invalid.captureBufferBytes = 0;
        if (invalidPair == 2) invalid.playbackBuffer = nullptr;
        if (invalidPair == 3) invalid.captureBuffer = nullptr;
        ok &= function.configure(invalid) == ESP_ERR_INVALID_ARG;
        ok &= function.configuration().playbackBufferBytes == 64 && function.configuration().captureBufferBytes == 64;
      }
      // Both the first-format budget and a one-byte shortage must fail without writes.
      for (size_t supplied : {32u,63u}) for (unsigned direction = 0; direction < 2; ++direction) {
        auto shortBuffer = config;
        if (direction == 0) shortBuffer.playbackBufferBytes = supplied;
        else shortBuffer.captureBufferBytes = supplied;
        ok &= function.configure(shortBuffer) == ESP_OK && !local.begin(identity) && local.lastError() == ESP_ERR_INVALID_SIZE;
        ok &= !local.started() && !input.capacity() && !output.capacity();
        for (uint8_t byte : buffers) ok &= byte == 0xc3;
      }
      Serial.printf("EXTENTS protocol=%u rejected=4 badPairs=4 guards=%u\r\n", unsigned(protocol), unsigned(ok));
      // First-format 32-byte sizing would incorrectly accept this overlap.
      ok &= function.configure(config) == ESP_OK && !local.begin(identity) && local.lastError() == ESP_ERR_INVALID_ARG;
      ok &= !local.started() && !input.capacity() && !output.capacity();
      config.captureBuffer = buffers + 65;
      ok &= function.configure(config) == ESP_OK && local.begin(identity);
      ok &= input.capacity() == 64 && output.capacity() == 64 && output.write(data,sizeof(data)) == 64;
      ok &= output.availableForWrite() == 0 && output.write(data,sizeof(data)) == 0;
      ok &= local.end() == ESP_OK && buffers[0] == 0xc3 && buffers[129] == 0xc3;
      for (unsigned i = 1; i < 65; ++i) ok &= buffers[i] == 0xc3;
      for (unsigned i = 65; i < 129; ++i) ok &= buffers[i] == 0x5a;
      Serial.printf("CAPABILITY protocol=%u scenario=5 ok=%u\r\n", unsigned(protocol), unsigned(ok));
      all &= ok;
    }
  }
  return all;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "capabilities") {
    bool ok = capabilities(); const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ok &= capabilities(); const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    Serial.printf("CAPABILITIES ok=%u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(after));
  } else if (command == "metadata") {
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
    bool ok = device.lastError() == ESP_ERR_INVALID_STATE && !stream.available() && !stream.clearBuffer();
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
