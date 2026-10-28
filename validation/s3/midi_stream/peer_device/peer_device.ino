#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>
#include "esp_heap_caps.h"
#include "../packets.h"
EspUsbDevice device;
EspUsbDeviceMidi midi(device);
EspUsbDeviceMidi *second = nullptr;
EspUsbDeviceCdcSerial *serialPort = nullptr;
EspUsbDeviceHidKeyboard *keyboard = nullptr;
EspUsbDeviceMidiPacket externalRx[5], externalTx[7];
unsigned wanted = 0, echoed = 0, cables = 1;
std::atomic<unsigned> callbackPackets{0};
std::atomic<bool> payloadOk{true};
bool echoing = false, receiveOnly = false;
std::atomic<bool> producersRun{false};
std::atomic<unsigned> producersActive{0}, closedAttempts{0}, staleWrites{0};
std::atomic<bool> transportClosed{false};
void endProducer(void *) {
  while (producersRun.load()) {
    const bool closed = transportClosed.load();
    const bool accepted = midi.noteOn(0, 60, 100);
    midi.noteOn(0, 61, 100, 1);
    if (closed) { ++closedAttempts; if (accepted) ++staleWrites; }
    delay(1);
  }
  --producersActive; vTaskDelete(nullptr);
}
unsigned producerIds[] = {0, 1};
void producer(void *context) {
  const auto id = *static_cast<unsigned *>(context);
  for (unsigned seq = 0; seq < 100 && producersRun.load();) {
    const EspUsbDeviceMidiPacket packet{9, uint8_t(0x90 | id), uint8_t(seq), uint8_t(seq ^ 0x55)};
    if (midi.writePacket(packet)) ++seq;
    else delay(1);
  }
  --producersActive; vTaskDelete(nullptr);
}
EspUsbDeviceMidiPacket pending; bool havePending = false;

bool metadata() {
  bool ok = true;
  {
    EspUsbDevice automatic; EspUsbDeviceMidi function(automatic);
    EspUsbDeviceConfig config; config.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
    ok &= !function.transferCapacity() && automatic.begin(config) && function.transferCapacity() == 16;
    ok &= automatic.end() == ESP_OK && !function.transferCapacity();
  }
  EspUsbDevice local; EspUsbDeviceMidi function(local, 16, 1);
  auto config = function.configuration(); config.rxPackets = config.txPackets = config.transferPackets = 1;
  config.name = "MIDI 計測";
  ok &= function.configure(config) == ESP_OK;
  EspUsbDeviceConfig detached; detached.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
  ok &= local.begin(detached) && function.receiveCapacity() == 1 && function.sendCapacity() == 1;
  ok &= function.onPacket(nullptr) == ESP_ERR_INVALID_STATE && function.configure(config) == ESP_ERR_INVALID_STATE;
  ok &= local.end() == ESP_OK && !function.receiveCapacity() && !function.sendCapacity();
  auto invalid = config; invalid.inCables = 0;
  ok &= function.configure(invalid) == ESP_ERR_INVALID_ARG && function.inCableCount() == 16;
  invalid = config; invalid.outCables = 17;
  ok &= function.configure(invalid) == ESP_ERR_INVALID_ARG;
  invalid = config; invalid.transferPackets = 16384;
  ok &= function.configure(invalid) == ESP_ERR_INVALID_SIZE;
  config.inCables = config.outCables = 1; config.rxMode = EspUsbMidiRxMode::Callback; config.rxPackets = 0;
  ok &= function.configure(config) == ESP_OK && !local.begin(detached) && local.lastError() == ESP_ERR_INVALID_ARG;
  config.rxMode = EspUsbMidiRxMode::Disabled; config.txPackets = config.transferPackets = 0;
  ok &= function.configure(config) == ESP_OK && local.begin(detached) && !function.receiveCapacity() && !function.sendCapacity();
  ok &= local.end() == ESP_OK;
  {
    EspUsbDevice invalidDevice; EspUsbDeviceMidi invalidFunction(invalidDevice, 0);
    ok &= !invalidDevice.begin(detached);
    EspUsbMidiConfig fixed; ok &= invalidFunction.configure(fixed) == ESP_OK && invalidDevice.begin(detached);
    ok &= invalidDevice.end() == ESP_OK;
  }
  return ok && heap_caps_check_integrity_all(true);
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command == "metadata") {
      bool ok = metadata(); const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
      for (unsigned i = 0; i < 20 && ok; ++i) ok &= metadata();
      Serial.printf("METADATA ok=%u before=%u after=%u\r\n", unsigned(ok), unsigned(before), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    } else if (command.startsWith("start ")) {
      delete second; second = nullptr; delete serialPort; serialPort = nullptr; delete keyboard; keyboard = nullptr;
      const unsigned mode = command.substring(6).toInt();
      EspUsbMidiConfig config; config.rxPackets = config.txPackets = config.transferPackets = 1;
      cables = 1; config.name = "MIDI primary"; receiveOnly = false;
      if (mode == 1) { config.rxPackets = 37; config.txPackets = 43; config.transferPackets = 17; config.inCables = config.outCables = cables = 16; }
      if (mode == 2) {
        memset(externalRx, 0x5a, sizeof(externalRx)); memset(externalTx, 0x5a, sizeof(externalTx));
        config.rxPackets = 3; config.txPackets = 5; config.rxBuffer = externalRx + 1; config.txBuffer = externalTx + 1;
      }
      if (mode == 3) { config.rxMode = EspUsbMidiRxMode::Callback; config.rxPackets = 0; config.txPackets = 128; }
      if (mode == 4) { config.rxMode = EspUsbMidiRxMode::Disabled; config.rxPackets = 0; }
      if (mode == 5) { config.txPackets = 0; receiveOnly = true; }
      if (mode == 6) { second = new EspUsbDeviceMidi(device); auto next = second->configuration(); next.rxPackets = 3; next.txPackets = 5; next.transferPackets = 1; second->configure(next); }
      if (mode == 8) config.autoFlush = false;
      if (mode == 9) config.inCables = 16;
      // Receive all 16 OUT cables, then exercise the sole IN cable separately.
      // Echoing OUT cables 1..15 would correctly be refused by the IN contract.
      if (mode == 10) { config.outCables = cables = 16; receiveOnly = true; }
      if (mode == 7) { serialPort = new EspUsbDeviceCdcSerial(device); keyboard = new EspUsbDeviceHidKeyboard(device); }
      callbackPackets.store(0); payloadOk.store(true);
      midi.onPacket([](const EspUsbDeviceMidiPacket &packet) {
        uint8_t expected[4]; midiPattern(expected, callbackPackets.load());
        payloadOk.store(payloadOk.load() && memcmp(&packet, expected, 4) == 0 && midi.writePacket(packet)); ++callbackPackets;
      });
      bool ok = midi.configure(config) == ESP_OK;
      EspUsbDeviceConfig identity; identity.product = "v3 MIDI stream";
      ok &= device.begin(identity);
      Serial.printf("STARTED %u rx=%u tx=%u in=%u out=%u\r\n", unsigned(ok), unsigned(midi.receiveCapacity()), unsigned(midi.sendCapacity()), midi.inCableCount(), midi.outCableCount());
    } else if (command == "rejected") {
      EspUsbDeviceMidiPacket bad{0xf9, 0x90, 1, 2};
      bool ok = midi.mounted() && !midi.writePacket(bad);
      bad.header = 0; ok &= !midi.writePacket(bad);
      ok &= static_cast<Print &>(midi).write(uint8_t(0xff)) == 0;
      Serial.printf("TX_REJECTED %u\r\n", unsigned(ok));
    } else if (command == "batch") {
      EspUsbDeviceMidiPacket first, next;
      midiPattern(reinterpret_cast<uint8_t *>(&first), 0); midiPattern(reinterpret_cast<uint8_t *>(&next), 1);
      const bool ok = midi.writePacket(first) && !midi.availableForWritePackets() && !midi.writePacket(next);
      Serial.printf("BATCH %u\r\n", unsigned(ok));
    } else if (command == "flush") {
      midi.flush(); Serial.println("FLUSHED");
    } else if (command == "pending") {
      const uint32_t deadline = millis() + 3000;
      while (midi.availablePackets() < 16 && int32_t(deadline - millis()) > 0) delay(1);
      Serial.printf("STAGED %u\r\n", unsigned(midi.receiveCapacity() == 1 && midi.availablePackets() == 16));
    } else if (command == "composite") {
      const uint32_t deadline = millis() + 3000;
      while (serialPort->available() < 3 && int32_t(deadline - millis()) > 0) delay(1);
      uint8_t data[3];
      const bool ok = serialPort->read(data, sizeof(data)) == 3 && memcmp(data, "mix", 3) == 0 && serialPort->write(data, sizeof(data)) == 3 && keyboard->tapUsage(4);
      Serial.printf("COMPOSITE %u\r\n", unsigned(ok));
    } else if (command.startsWith("echo ")) {
      wanted = command.substring(5).toInt(); echoed = 0; havePending = false; echoing = true; payloadOk.store(true);
      Serial.printf("ARMED %u\r\n", wanted);
    } else if (command == "callback_result") {
      Serial.printf("CALLBACK %u payload=%u\r\n", callbackPackets.load(), unsigned(payloadOk.load()));
    } else if (command.startsWith("send ")) {
      wanted = command.substring(5).toInt(); echoed = 0; havePending = false; echoing = false;
    } else if (command == "second") {
      uint8_t expected[4]; midiPattern(expected, 0);
      EspUsbDeviceMidiPacket packet;
      const uint32_t deadline = millis() + 3000;
      while (!second->availablePackets() && int32_t(deadline - millis()) > 0) delay(1);
      const bool ok = second->readPacket(packet) && memcmp(&packet, expected, 4) == 0 && second->writePacket(packet) && !midi.availablePackets();
      Serial.printf("SECOND %u\r\n", unsigned(ok));
    } else if (command == "parallel") {
      producersRun.store(true); producersActive.store(2); bool ok = true;
      for (auto &id : producerIds) if (xTaskCreate(producer, "MIDI producer", 3072, &id, 1, nullptr) != pdPASS) { --producersActive; ok = false; }
      Serial.printf("PARALLEL_STARTED %u\r\n", unsigned(ok));
    } else if (command == "parallel_result") {
      const uint32_t deadline = millis() + 3000;
      while (producersActive.load() && int32_t(deadline - millis()) > 0) delay(1);
      Serial.printf("PARALLEL_DONE %u\r\n", unsigned(!producersActive.load()));
    } else if (command == "semantics") {
      const uint8_t cable = midi.inCableCount() - 1;
      bool ok = true;
      for (unsigned which = 0; which < midiHelperCount && ok; ++which) {
        const uint32_t deadline = millis() + 3000; bool accepted = false;
        do {
          switch (which) {
          case 0: accepted = midi.noteOn(2,60,100,cable); break;
          case 1: accepted = midi.noteOff(3,61,0,cable); break;
          case 2: accepted = midi.controlChange(4,7,127,cable); break;
          case 3: accepted = midi.programChange(5,10,cable); break;
          case 4: accepted = midi.polyPressure(6,62,80,cable); break;
          case 5: accepted = midi.channelPressure(7,81,cable); break;
          case 6: accepted = midi.pitchBend(8,8192,cable); break;
          case 7: accepted = midi.noteOn(31,200,255,cable); break;
          default: {
            EspUsbDeviceMidiPacket event; memcpy(&event, midiHelperExpected[which], 4); event.header |= uint8_t(cable << 4);
            accepted = midi.writePacket(event); break;
          }
          }
          if (!accepted) delay(1);
        } while (!accepted && int32_t(deadline - millis()) > 0);
        ok &= accepted;
      }
      Serial.printf("SEMANTICS %u\r\n", unsigned(ok));
    } else if (command == "end_race") {
      transportClosed.store(false); closedAttempts.store(0); staleWrites.store(0);
      producersRun.store(true); producersActive.store(1);
      bool ok = xTaskCreate(endProducer, "MIDI end probe", 3072, nullptr, 1, nullptr) == pdPASS;
      if (!ok) producersActive.store(0);
      delay(10);
      ok &= device.end() == ESP_OK; transportClosed.store(true);
      auto config = midi.configuration();
      for (unsigned i = 0; i < 32; ++i) {
        config.inCables = i % 2 ? 16 : 1;
        ok &= midi.configure(config) == ESP_OK; delay(1);
      }
      producersRun.store(false); while (producersActive.load()) delay(1);
      Serial.printf("END_RACE %u\r\n", unsigned(ok && closedAttempts.load() >= 10 && !staleWrites.load()));
    } else if (command == "helpers") {
      bool ok = midi.noteOn(2, 60, 100) && !midi.noteOn(0, 1, 1, 16);
      Serial.printf("HELPERS %u\r\n", unsigned(ok));
    } else if (command == "errors") {
      Serial.printf("ERRORS %u\r\n", unsigned(midi.receiveErrors()));
    } else if (command == "capacities") {
      bool guards = true;
      for (unsigned i : {0u, 4u}) for (auto byte : reinterpret_cast<const uint8_t (&)[4]>(externalRx[i])) guards &= byte == 0x5a;
      for (unsigned i : {0u, 6u}) for (auto byte : reinterpret_cast<const uint8_t (&)[4]>(externalTx[i])) guards &= byte == 0x5a;
      Serial.printf("CAPACITY rx=%u tx=%u guards=%u\r\n", unsigned(midi.receiveCapacity()), unsigned(midi.sendCapacity()), unsigned(guards));
    } else if (command == "stop") {
      producersRun.store(false); while (producersActive.load()) delay(1);
      wanted = 0; havePending = false;
      Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED");
    }
  }
  if (wanted) {
    if (!havePending) {
      if (echoing) {
        if (midi.readPacket(pending)) {
          uint8_t expected[4]; midiPattern(expected, echoed, cables);
          payloadOk.store(payloadOk.load() && memcmp(&pending, expected, 4) == 0); havePending = true;
        }
      } else { midiPattern(reinterpret_cast<uint8_t *>(&pending), echoed, cables); havePending = true; }
    }
    if (havePending && (receiveOnly || midi.writePacket(pending))) {
      havePending = false;
      if (++echoed == wanted) { Serial.printf("DONE %u payload=%u\r\n", wanted, unsigned(payloadOk.load())); wanted = 0; }
    }
  }
  delay(1);
}
