// TEMPORARY - prototype device for CR-10 / F1. Delete with the prototype.
//
// Streams fixed blocks with EspUsbDeviceVendor::writeDirect(), arming the next
// one from inside onTxComplete(). Non-buffered build (build_opt.h) so the
// vendor class has no TX FIFO and no ZLP logic of its own.
//
// Each block is stamped "D<seq:04u>:" so the host can tell a dropped or
// duplicated block from a merely slow one, and the rest is a position-dependent
// pattern so a torn transfer is visible.
#include "EspUsbDevice.h"

EspUsbDevice device;
EspUsbDeviceVendor Vendor(device);

// 21 bytes, because the host reads 63 at a time: three whole blocks per read,
// no partial block at either end, so a torn or reordered transfer is
// unambiguous rather than a boundary artifact. Throughput is the P4's job, not
// this rig's - full speed cannot show it.
static constexpr size_t kBlock = 21;
static uint8_t __attribute__((aligned(64))) bufA[kBlock];
static uint8_t __attribute__((aligned(64))) bufB[kBlock];

static volatile uint32_t g_blocks = 0;
static volatile uint32_t g_bytes = 0;
static volatile uint32_t g_armFail = 0;
static volatile uint32_t g_zeroLen = 0;
static volatile bool g_running = false;
static uint8_t g_which = 0;

static void fill(uint8_t *dst, uint32_t seq)
{
  // snprintf writes its own NUL at dst[6]; the loop below overwrites it.
  snprintf(reinterpret_cast<char *>(dst), 7, "D%04u:", static_cast<unsigned>(seq % 10000));
  for (size_t i = 6; i < kBlock; i++)
  {
    dst[i] = static_cast<uint8_t>('a' + ((i + seq) % 26));
  }
}

static bool armNext()
{
  uint8_t *buf = g_which ? bufB : bufA;
  g_which ^= 1;
  fill(buf, g_blocks);
  if (!Vendor.writeDirect(buf, kBlock))
  {
    g_armFail++;
    Serial.printf("DEVICE_DIRECT_ARMFAIL %s\n", Vendor.lastDirectErrorName());
    return false;
  }
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  // Arm from inside the completion callback: that is what wins the endpoint
  // claim against the class's own refill.
  Vendor.onTxComplete([](size_t sent)
                      {
                        g_blocks++;
                        g_bytes += static_cast<uint32_t>(sent);
                        if (sent == 0)
                        {
                          g_zeroLen++;
                        }
                        if (g_running)
                        {
                          armNext();
                        }
                      });

  EspUsbDeviceConfig config;
  config.pid = 0x4074;
  config.manufacturer = "EspUsbDevice";
  config.product = "Vendor direct write";
  device.begin(config);
}

void loop()
{
  static bool started = false;
  if (!started && Vendor.mounted())
  {
    delay(100);
    started = true;
    g_running = true;
    // directWriteSupported() is the *library's* view. The sketch can see
    // CFG_TUD_VENDOR_TXRX_BUFFERED from its own command line while the library
    // was built without it - that is what a stale build (no --clean) looks
    // like - so asserting the sketch's macro would pass for the wrong reason.
    Serial.printf("DEVICE_DIRECT_START ok=%d direct=%d\n", armNext() ? 1 : 0,
                  EspUsbDeviceVendor::directWriteSupported() ? 1 : 0);
  }
  if (started && !Vendor.mounted())
  {
    started = false;
    g_running = false;
    Serial.println("DEVICE_DIRECT_STOP");
  }

  while (Serial.available() > 0)
  {
    switch (Serial.read())
    {
    case 's':
      Serial.printf("DEVICE_DIRECT_STAT blocks=%lu bytes=%lu armfail=%lu zerolen=%lu\n",
                    (unsigned long)g_blocks, (unsigned long)g_bytes,
                    (unsigned long)g_armFail, (unsigned long)g_zeroLen);
      break;
    default:
      break;
    }
  }
  delay(5);
}
