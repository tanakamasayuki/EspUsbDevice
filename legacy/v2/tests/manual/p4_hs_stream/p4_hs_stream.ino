// One-way high-speed bulk IN through EspUsbDeviceVendor, shaped so its numbers
// can be put next to the E069-E071 measurements that asked for these knobs.
//
// Same transfer as those: 4 MiB per run out of a 64 KiB internal-RAM pattern,
// the sending task pinned to core 0 at priority 5, commands over the console so
// nothing shares the endpoint being measured. The host reads with a 1 MiB read
// size and checks the pattern, so a fast run that lost data cannot look good.
//
// Three things vary from build_opt.h, and none of them needs the library edited:
//
//   -DCFG_TUD_VENDOR_TX_BUFSIZE=<n>   transmit FIFO depth       (default 8192 on P4)
//   -DCFG_TUD_VENDOR_TX_EPSIZE=<n>    bytes per armed transfer  (default 512 = one packet)
//   -DESP_USB_STREAM_WAIT_WRITABLE=1  block instead of spinning when the FIFO is full
//
// The middle one is the interesting one. TinyUSB's vendor class submits one
// transfer per endpoint and re-arms it from the completion callback, so at one
// packet per transfer every 512 bytes costs a completion interrupt, an event
// queue hop and a usbd task turn. Raising it has DWC2 send several packets per
// armed transfer, which buys the same thing as having two transfers in flight
// without touching TinyUSB.
//
// Commands and results go over the bulk endpoint itself rather than the console,
// which is not a stylistic choice: opening the USB-Serial-JTAG port resets the
// chip, and on a bench where the OTG port reaches the PC through usbip that
// reset drops the attachment mid-measurement. Nothing is sent in band while the
// timed section is running.
//
// Host side: uv run --with pyusb python manual/p4_hs_stream/p4_hs_stream.py
#include "EspUsbDevice.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "p4_hs_stream requires ESP32-P4"
#endif

#ifndef ESP_USB_STREAM_WAIT_WRITABLE
#define ESP_USB_STREAM_WAIT_WRITABLE 0
#endif

// build_opt.h reaches the sketch as well as the library, so when the transfer
// size is overridden its value is known here; otherwise it is TinyUSB's own
// default of one high-speed bulk packet. The FIFO depth does not need the same
// treatment - EspUsbDeviceVendor::writeCapacity() reports it.
#ifdef CFG_TUD_VENDOR_TX_EPSIZE
#define STREAM_TX_EPSIZE CFG_TUD_VENDOR_TX_EPSIZE
#else
#define STREAM_TX_EPSIZE 512
#endif

EspUsbDevice device;
EspUsbDeviceVendor vendor(device, 512);

static constexpr size_t PATTERN_BYTES = 64u * 1024u;
static constexpr size_t TRANSFER_BYTES = 4u * 1024u * 1024u;
static constexpr BaseType_t SENDER_CORE = 0;
static constexpr UBaseType_t SENDER_PRIORITY = 5;

static uint8_t *pattern = nullptr;
static SemaphoreHandle_t senderDone = nullptr;
static volatile uint64_t elapsedUs = 0;
static volatile size_t writtenBytes = 0;
static volatile uint32_t stallCount = 0;
static volatile uint32_t waitCount = 0;

static volatile bool streamRequested = false;

static void senderTask(void *)
{
  size_t sent = 0;
  size_t offset = 0;
  uint32_t stalls = 0;
  // How often the FIFO was too full to take the next chunk. On the spin path
  // that is the same event as a refused write(); with waitWritable() it is the
  // number of times the task actually blocked, which is the figure to compare
  // against the spin count.
  uint32_t waits = 0;
  const uint64_t startedUs = esp_timer_get_time();

  while (sent < TRANSFER_BYTES)
  {
    const size_t room = PATTERN_BYTES - offset;
    const size_t remaining = TRANSFER_BYTES - sent;
    const size_t want = remaining < room ? remaining : room;

#if ESP_USB_STREAM_WAIT_WRITABLE
    // Wait for room to make progress, not for room for the whole chunk. Asking
    // for the entire FIFO means asking for it to be completely empty, which is a
    // different and much worse thing to wait for.
    const size_t waitFor = ESP_USB_STREAM_WAIT_WRITABLE;
    if (vendor.writeAvailable() < waitFor)
    {
      ++waits;
      if (!vendor.waitWritable(waitFor, 1000))
      {
        ++stalls;
        if (!vendor.mounted())
        {
          break;
        }
        continue;
      }
    }
#endif

    const size_t written = vendor.write(pattern + offset, want);
    if (written == 0)
    {
      ++stalls;
      vendor.flush();
      taskYIELD();
      continue;
    }
    sent += written;
    offset = (offset + written) % PATTERN_BYTES;
  }
  vendor.flush();

  elapsedUs = esp_timer_get_time() - startedUs;
  writtenBytes = sent;
  stallCount = stalls;
  waitCount = waits;
  xSemaphoreGive(senderDone);
  vTaskDelete(nullptr);
}

static void runTransfer()
{
  elapsedUs = 0;
  writtenBytes = 0;
  stallCount = 0;
  waitCount = 0;
  xTaskCreatePinnedToCore(senderTask, "hs_stream_tx", 4096, nullptr,
                          SENDER_PRIORITY, nullptr, SENDER_CORE);
  xSemaphoreTake(senderDone, portMAX_DELAY);

  // Sent after the timed section, as one short packet the host reads once it has
  // counted the whole transfer.
  char stats[160];
  const int length = snprintf(
      stats, sizeof(stats),
      "SEND bytes=%lu written=%lu stalls=%lu waits=%lu elapsed_us=%llu "
      "tx_bufsize=%u tx_epsize=%d wait_writable=%d sender_core=%d\n",
      static_cast<unsigned long>(TRANSFER_BYTES),
      static_cast<unsigned long>(writtenBytes),
      static_cast<unsigned long>(stallCount),
      static_cast<unsigned long>(waitCount),
      static_cast<unsigned long long>(elapsedUs),
      static_cast<unsigned>(EspUsbDeviceVendor::writeCapacity()),
      STREAM_TX_EPSIZE, ESP_USB_STREAM_WAIT_WRITABLE,
      static_cast<int>(SENDER_CORE));
  Serial.print(stats);
  Serial.flush();

  size_t sent = 0;
  const uint32_t deadline = millis() + 2000;
  while (sent < static_cast<size_t>(length) && millis() < deadline)
  {
    const size_t written =
        vendor.write(reinterpret_cast<const uint8_t *>(stats) + sent,
                     static_cast<size_t>(length) - sent);
    sent += written;
    vendor.flush();
    if (written == 0)
    {
      taskYIELD();
    }
  }
}

// Runs on the usbd task. Only sets the flag; loop() starts the sender, the same
// shape the experiments this is compared against use.
static void handleRx()
{
  while (vendor.available() > 0)
  {
    uint8_t buffer[64];
    const size_t read = vendor.read(buffer, sizeof(buffer));
    if (read == 0)
    {
      return;
    }
    for (size_t i = 0; i < read; i++)
    {
      if (buffer[i] == 'S')
      {
        streamRequested = true;
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1500);

  pattern = static_cast<uint8_t *>(
      heap_caps_malloc(PATTERN_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (pattern)
  {
    uint32_t *words = reinterpret_cast<uint32_t *>(pattern);
    for (size_t i = 0; i < PATTERN_BYTES / sizeof(uint32_t); i++)
    {
      words[i] = static_cast<uint32_t>(i);
    }
  }
  senderDone = xSemaphoreCreateBinary();

  vendor.onRx([](size_t) { handleRx(); });

  EspUsbDeviceConfig config;
  // Deliberately the same VID / PID / serial as the E069-E078 experiments this
  // harness is compared against. Windows keys a device instance on exactly those
  // three, so reusing them keeps the usbipd share that was already granted to
  // that instance - otherwise every reflash needs another administrator `usbipd
  // bind`. pid.codes 1209:0008 is a test PID and belongs to nothing shipped.
  config.vid = 0x1209;
  config.pid = 0x0008;
  config.manufacturer = "Open Embedded Probe (TEST ONLY)";
  config.product = "EspUsbDevice P4 HS Stream";
  config.serialNumber = "E069-A";
  config.controller = EspUsbController::HighSpeed;
  config.webusbEnabled = true;

  if (!device.begin(config))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
}

void loop()
{
  if (streamRequested)
  {
    streamRequested = false;
    runTransfer();
  }
  delay(1);
}
