#include <Arduino.h>
#include <EspUsbDevice.h>
#include <atomic>

EspUsbDevice device;
EspUsbDeviceCdcSerial serial0(device, "data");
EspUsbDeviceCdcSerial *serial1 = new EspUsbDeviceCdcSerial(device, "control");
EspUsbDeviceVendor *vendor = nullptr;
EspUsbDeviceHidKeyboard *keyboard = nullptr;
EspUsbDeviceBulkStream *channels[] = {&serial0, serial1, nullptr};
uint8_t buffers[3][129];
size_t wanted[3] = {}, received[3] = {}, sent[3] = {};
alignas(64) uint8_t direct[384];
EspUsbTransferResult rxResult, txResult;
std::atomic<unsigned> rxDone{0}, txDone{0};
unsigned rxTarget = 0, txTarget = 0;
alignas(64) uint8_t sourceStorage[2][256];
EspUsbTransferResult sourceResults[2];
std::atomic<unsigned> sourceDone[2];
unsigned sourceTargets[2] = {};
uint64_t sourceIds[2] = {};
size_t sourceLengths[2] = {};
bool directSource = false;
bool borrowed = false, txPosted = false;
std::atomic<unsigned> codingEvents[2], stateEvents[2];

void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  serial0.onLineCoding([](const EspUsbDeviceCdcLineCoding &) { ++codingEvents[0]; });
  serial1->onLineCoding([](const EspUsbDeviceCdcLineCoding &) { ++codingEvents[1]; });
  serial0.onLineState([](const EspUsbDeviceCdcLineState &) { ++stateEvents[0]; });
  serial1->onLineState([](const EspUsbDeviceCdcLineState &) { ++stateEvents[1]; });
  serial0.onDirectRxComplete([](const EspUsbTransferResult &result) { rxResult = result; rxDone.fetch_add(1, std::memory_order_release); });
  serial0.onDirectTxComplete([](const EspUsbTransferResult &result) { txResult = result; txDone.fetch_add(1, std::memory_order_release); });
}
void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command.startsWith("configure ")) {
      unsigned rx0 = 0, tx0 = 0, rx1 = 0, tx1 = 0;
      sscanf(command.c_str(), "configure %u %u %u %u", &rx0, &tx0, &rx1, &tx1);
      EspUsbBulkStreamConfig first, second;
      first.rxCapacity = rx0; first.txCapacity = tx0; first.transferBytes = 128;
      second.rxCapacity = rx1; second.txCapacity = tx1; second.transferBytes = 128;
      const auto a = serial0.configure(first), b = serial1->configure(second);
      if (a == ESP_OK && b == ESP_OK) borrowed = false;
      Serial.printf("CONFIGURED first=%d second=%d\r\n", int(a), int(b));
    } else if (command == "combined") {
      delete serial1; serial1 = nullptr;
      vendor = new EspUsbDeviceVendor(device);
      keyboard = new EspUsbDeviceHidKeyboard(device);
      channels[1] = nullptr; channels[2] = vendor;
      Serial.println("COMBINED");
    } else if (command == "key") {
      Serial.printf("KEY_SENT %u\r\n", unsigned(keyboard && keyboard->tapUsage(4)));
    } else if (command == "tx_only" || command == "tx_only_direct") {
      EspUsbBulkStreamConfig config;
      config.rxMode = EspUsbBulkRxMode::Disabled; config.rxCapacity = 0;
      config.txCapacity = command == "tx_only" ? 128 : 0;
      config.transferBytes = config.txCapacity;
      config.streamTermination = EspUsbStreamTermination::EndWithShortPacket;
      const bool configured = serial0.onDirectRxComplete(nullptr) == ESP_OK &&
          vendor && vendor->onDirectRxComplete(nullptr) == ESP_OK &&
          serial0.configure(config) == ESP_OK && vendor->configure(config) == ESP_OK;
      directSource = command == "tx_only_direct";
      serial0.onDirectTxComplete([](const EspUsbTransferResult &result) {
        sourceResults[0] = result; sourceDone[0].fetch_add(1, std::memory_order_release);
      });
      vendor->onDirectTxComplete([](const EspUsbTransferResult &result) {
        sourceResults[1] = result; sourceDone[1].fetch_add(1, std::memory_order_release);
      });
      Serial.printf("TX_ONLY %u\r\n", unsigned(configured));
    } else if (command == "rx_disabled") {
      uint8_t ignored = 0;
      const auto a = serial0.readDirect(sourceStorage[0], 64), b = vendor->readDirect(sourceStorage[1], 64);
      const bool disabled = !serial0.readCapacity() && !vendor->readCapacity() &&
          !serial0.maxDirectReceive() && !vendor->maxDirectReceive() &&
          !serial0.available() && !vendor->available() && !serial0.read(&ignored, 1) && !vendor->read(&ignored, 1) &&
          !a && a.error == EspUsbTransferError::NotSupported && !b && b.error == EspUsbTransferError::NotSupported;
      Serial.printf("RX_DISABLED %u\r\n", unsigned(disabled));
    } else if (command.startsWith("source ")) {
      unsigned port = 0, length = 0;
      sscanf(command.c_str(), "source %u %u", &port, &length);
      if ((port != 0 && port != 2) || !length || length > 129) { Serial.println("INVALID_SOURCE"); return; }
      const unsigned index = port / 2;
      auto &channel = *channels[port];
      memset(sourceStorage[index], 0xa5, sizeof(sourceStorage[index]));
      for (size_t i = 0; i < length; ++i) sourceStorage[index][64 + i] = uint8_t(i * 37 + length);
      sourceLengths[index] = length;
      sourceTargets[index] = sourceDone[index].load(std::memory_order_acquire) + 1;
      if (directSource) {
        const auto result = channel.writeDirect(sourceStorage[index] + 64, length, EspUsbTransferTermination::EndWithShortPacket);
        sourceIds[index] = result.id;
        Serial.printf("SOURCE_POSTED %u\r\n", unsigned(bool(result)));
      } else Serial.printf("SOURCE_COPIED %u\r\n", unsigned(channel.write(sourceStorage[index] + 64, length)));
    } else if (command.startsWith("source_result ")) {
      const unsigned index = command.substring(14).toInt() / 2;
      const uint32_t deadline = millis() + 3000;
      while (sourceDone[index].load(std::memory_order_acquire) < sourceTargets[index] && int32_t(deadline - millis()) > 0) delay(1);
      const auto &result = sourceResults[index];
      const bool valid = sourceDone[index].load(std::memory_order_acquire) == sourceTargets[index] &&
          result.id == sourceIds[index] && result.buffer == sourceStorage[index] + 64 &&
          result.status == EspUsbTransferStatus::Success && result.actualBytesKnown && result.actualBytes == sourceLengths[index];
      Serial.printf("SOURCE_RESULT %u\r\n", unsigned(valid));
    } else if (command == "borrowed") {
      EspUsbBulkStreamConfig config;
      config.rxMode = EspUsbBulkRxMode::Borrowed;
      config.rxCapacity = config.txCapacity = config.transferBytes = 0;
      const auto result = serial0.configure(config);
      borrowed = result == ESP_OK;
      Serial.printf("BORROWED %d\r\n", int(result));
    } else if (command == "start") {
      EspUsbDeviceConfig config; config.product = "v3 CDC stream probe";
      Serial.println(device.begin(config) ? "DEVICE_STARTED" : "DEVICE_START_FAILED");
    } else if (command == "capacities") {
      Serial.printf("CAPACITIES %u %u %u %u\r\n", unsigned(serial0.readCapacity()), unsigned(serial0.writeCapacity()),
          unsigned(serial1 ? serial1->readCapacity() : 0), unsigned(serial1 ? serial1->writeCapacity() : 0));
    } else if (command == "mutate") {
      const bool rejected = serial0.onLineCoding(nullptr) == ESP_ERR_INVALID_STATE &&
          serial0.onLineState(nullptr) == ESP_ERR_INVALID_STATE && serial0.onBreak(nullptr) == ESP_ERR_INVALID_STATE &&
          serial0.onRx(nullptr) == ESP_ERR_INVALID_STATE && serial0.setName("changed") == ESP_ERR_INVALID_STATE;
      Serial.printf("MUTATION_REJECTED %u\r\n", unsigned(rejected));
    } else if (command == "state") {
      for (unsigned i = 0; i < (serial1 ? 2u : 1u); ++i) {
        auto &port = i ? *serial1 : serial0;
        const auto coding = port.lineCoding(); const auto state = port.lineState();
        Serial.printf("PORT %u mounted=%u connected=%u baud=%u stop=%u parity=%u bits=%u dtr=%u rts=%u coding_events=%u state_events=%u\r\n",
            i, unsigned(port.mounted()), unsigned(port.connected()), unsigned(coding.baud), unsigned(coding.stopBits), unsigned(coding.parity),
            unsigned(coding.dataBits), unsigned(state.dtr), unsigned(state.rts), codingEvents[i].load(), stateEvents[i].load());
      }
    } else if (command.startsWith("expect ")) {
      unsigned port = 0, length = 0;
      sscanf(command.c_str(), "expect %u %u", &port, &length);
      if (port > 2 || !length || length > 129 || wanted[port]) { Serial.println("INVALID_EXPECT"); return; }
      wanted[port] = length; received[port] = sent[port] = 0;
      if (borrowed && port == 0) {
        rxTarget = rxDone.load(std::memory_order_acquire) + 1;
        txTarget = txDone.load(std::memory_order_acquire) + 1;
        txPosted = false;
        memset(direct, 0xa5, sizeof(direct));
        if (!serial0.readDirect(direct + 64, (length + 63) & ~size_t(63))) { Serial.println("RX_POST_FAILED"); return; }
      }
      Serial.printf("ARMED %u %u\r\n", port, length);
    } else if (command == "stop") {
      const auto result = device.end();
      for (auto &value : wanted) value = 0;
      Serial.println(result == ESP_OK ? "STOPPED" : "STOP_FAILED");
    }
  }
  for (unsigned port = 0; port < 3; ++port) {
    if (!channels[port] || !wanted[port]) continue;
    if (borrowed && port == 0) {
      if (!txPosted && rxDone.load(std::memory_order_acquire) >= rxTarget) {
        if (rxResult.status != EspUsbTransferStatus::Success || !rxResult.actualBytesKnown || rxResult.actualBytes != wanted[0] || rxResult.buffer != direct + 64) {
          Serial.println("DIRECT_RX_FAILED"); wanted[0] = 0; continue;
        }
        txPosted = true;
        if (!serial0.writeDirect(direct + 64, wanted[0], EspUsbTransferTermination::EndWithShortPacket)) {
          Serial.println("DIRECT_TX_FAILED"); wanted[0] = 0;
        }
      }
      if (txPosted && txDone.load(std::memory_order_acquire) >= txTarget) {
        bool guards = true;
        for (unsigned i = 0; i < 64; ++i) guards &= direct[i] == 0xa5;
        const size_t capacity = (wanted[0] + 63) & ~size_t(63);
        for (size_t i = 64 + capacity; i < sizeof(direct); ++i) guards &= direct[i] == 0xa5;
        if (txResult.status == EspUsbTransferStatus::Success && txResult.actualBytesKnown && txResult.actualBytes == wanted[0] &&
            txResult.buffer == direct + 64 && guards) Serial.printf("ECHOED 0 %u\r\n", unsigned(wanted[0]));
        else Serial.println("DIRECT_RESULT_FAILED");
        wanted[0] = 0;
      }
      continue;
    }
    auto &channel = *channels[port];
    received[port] += channel.read(buffers[port] + received[port], wanted[port] - received[port]);
    if (received[port] == wanted[port]) {
      sent[port] += channel.write(buffers[port] + sent[port], wanted[port] - sent[port]);
      if (sent[port] == wanted[port]) { Serial.printf("ECHOED %u %u\r\n", port, unsigned(wanted[port])); wanted[port] = 0; }
    }
  }
  delay(1);
}
