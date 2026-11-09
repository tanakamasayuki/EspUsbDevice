#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_chip_info.h"
// PSRAM present on this P4 board: initialized size, the external heap, and a
// pattern written and read back across one large allocation.
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command != "psram") return;
  esp_chip_info_t chip; esp_chip_info(&chip);
  const size_t size = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
  const size_t heap = heap_caps_get_total_size(MALLOC_CAP_SPIRAM), largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  bool ok = size > 0;
  const size_t n = largest / 4 * 4;
  auto *p = static_cast<uint32_t *>(n ? heap_caps_malloc(n, MALLOC_CAP_SPIRAM) : nullptr);
  ok &= p != nullptr;
  for (size_t i = 0; ok && i < n / 4; ++i) p[i] = uint32_t(i * 2654435761u);
  for (size_t i = 0; ok && i < n / 4; ++i) ok &= p[i] == uint32_t(i * 2654435761u);
  heap_caps_free(p);
  Serial.printf("PSRAM ok=%u size=%u heap=%u tested=%u revision=%u\r\n", unsigned(ok), unsigned(size), unsigned(heap), unsigned(n), unsigned(chip.revision));
}
