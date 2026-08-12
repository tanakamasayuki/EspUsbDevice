#include "EspUsbDevice.h"
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <esp_heap_caps.h>

// Peer device for the CDC-NCM soak test. This is the side under investigation:
// a USB network device (CDC-NCM) with a DHCP server at 192.168.7.1, plus
//   port 9000: sink   - reads and discards everything the host sends
//   port 9001: source - writes as fast as the host reads
//
// The reported failure is a *permanent* stop of device->host traffic after a
// while of continuous streaming, so the interesting direction here is port
// 9001: every lwIP frame goes through espUsbDeviceNetTxFrame() ->
// tud_network_xmit() on the tcpip task, concurrently with the usbd task's
// tud_task(). If that race wedges the NCM transmit state machine, this sketch
// stops making progress and tud_network_can_xmit() stays false forever.
//
// tud_network_can_xmit() is TinyUSB's own symbol, declared here rather than
// pulled in via tusb.h so the sketch needs nothing from the library's private
// include path.
extern "C" bool tud_network_can_xmit(uint16_t size);

// Dumps the dwc2 IN endpoint registers directly, so it needs nothing from the
// library. This is what identified the original failure: with the driver in
// slave (FIFO) mode the transmitter ended up with EPENA=1 and packets still
// outstanding, an empty TxFIFO, and its DIEPEMPMSK refill bit already cleared -
// a transfer that can never be fed again. Keeping the dump makes a regression
// recognisable at a glance instead of just "throughput went to zero".
//
// The register base is ESP32-S3's; this test only runs on the S3 peer profiles.
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "usb_ncm_soak peer_device expects an ESP32-S3 (dwc2 register base)"
#endif
static const uint32_t USB_OTG_BASE = 0x60080000;
#define DWC2_REG(off) (*reinterpret_cast<volatile uint32_t *>(USB_OTG_BASE + (off)))
#define DWC2_DIEPCTL(n) DWC2_REG(0x900 + 0x20 * (n))
#define DWC2_DIEPINT(n) DWC2_REG(0x908 + 0x20 * (n))
#define DWC2_DIEPTSIZ(n) DWC2_REG(0x910 + 0x20 * (n))
#define DWC2_DTXFSTS(n) DWC2_REG(0x918 + 0x20 * (n))

static void reportDwc2()
{
  Serial.printf("DWC2_GLOBAL gintsts=%08lx gintmsk=%08lx daint=%08lx daintmsk=%08lx "
                "diepempmsk=%08lx\n",
                static_cast<unsigned long>(DWC2_REG(0x014)),
                static_cast<unsigned long>(DWC2_REG(0x018)),
                static_cast<unsigned long>(DWC2_REG(0x818)),
                static_cast<unsigned long>(DWC2_REG(0x81c)),
                static_cast<unsigned long>(DWC2_REG(0x834)));
  for (uint8_t n = 0; n < 5; n++)
  {
    const uint32_t ctl = DWC2_DIEPCTL(n);
    if ((ctl & (1u << 15)) == 0 && n != 0)
    {
      continue; // endpoint not active
    }
    const uint32_t tsiz = DWC2_DIEPTSIZ(n);
    Serial.printf("DWC2_EP%u ctl=%08lx epena=%lu naksts=%lu stall=%lu int=%08lx "
                  "tsiz=%08lx pktcnt=%lu xfrsiz=%lu dtxfsts=%08lx empmsk=%lu\n",
                  n,
                  static_cast<unsigned long>(ctl),
                  static_cast<unsigned long>((ctl >> 31) & 1),
                  static_cast<unsigned long>((ctl >> 17) & 1),
                  static_cast<unsigned long>((ctl >> 21) & 1),
                  static_cast<unsigned long>(DWC2_DIEPINT(n)),
                  static_cast<unsigned long>(tsiz),
                  static_cast<unsigned long>((tsiz >> 19) & 0x3ff),
                  static_cast<unsigned long>(tsiz & 0x7ffff),
                  static_cast<unsigned long>(DWC2_DTXFSTS(n)),
                  static_cast<unsigned long>((DWC2_REG(0x834) >> n) & 1));
  }
}

EspUsbDevice device;
EspUsbDeviceNet net(device);

NetworkServer sink(9000);
NetworkServer source(9001);
NetworkClient sinkClient;
NetworkClient sourceClient;

static uint8_t buffer[8192];
static uint32_t sinkBytes = 0;
static uint32_t sourceBytes = 0;
// Non-zero once source.write() starts refusing data, which is what a wedged
// transmit path looks like from the sketch's side.
static uint32_t sourceWriteFails = 0;
static uint32_t lastTickMs = 0;
static uint32_t lastTickBytes = 0;
static bool ticking = false;

static void reportState(const char *tag)
{
  Serial.printf("%s link=%u net=%u ip=%s sink=%lu source=%lu writeFails=%lu "
                "canXmit=%u canXmitSmall=%u heap=%lu block=%lu\n",
                tag,
                net.linkUp() ? 1 : 0,
                net.networkUp() ? 1 : 0,
                net.localIP().toString().c_str(),
                static_cast<unsigned long>(sinkBytes),
                static_cast<unsigned long>(sourceBytes),
                static_cast<unsigned long>(sourceWriteFails),
                tud_network_can_xmit(1514) ? 1 : 0,
                tud_network_can_xmit(64) ? 1 : 0,
                static_cast<unsigned long>(esp_get_free_heap_size()),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

void setup()
{
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);

  EspUsbDeviceConfig config;
  config.vid = 0x303a;
  config.pid = 0x4032;
  config.manufacturer = "EspUsb";
  config.product = "EspUsbDevice NCM";
  config.serialNumber = "espusb-ncm-peer";

  if (!device.begin(config))
  {
    Serial.printf("DEVICE_BEGIN 0 %s\n", device.lastErrorName());
    return;
  }
  if (!net.beginNetwork())
  {
    Serial.println("NET_BEGIN 0");
    return;
  }

  memset(buffer, 0xa5, sizeof(buffer));
  sink.begin();
  sink.setNoDelay(true);
  source.begin();
  source.setNoDelay(true);

  Serial.printf("DEVICE_BEGIN 1 ip=%s\n", net.localIP().toString().c_str());
}

void loop()
{
  if (!sinkClient || !sinkClient.connected())
  {
    NetworkClient incoming = sink.accept();
    if (incoming)
    {
      sinkClient = incoming;
      sinkBytes = 0;
    }
  }
  if (sinkClient && sinkClient.connected())
  {
    while (sinkClient.available() > 0)
    {
      const int read = sinkClient.read(buffer, 1460);
      if (read <= 0)
      {
        break;
      }
      sinkBytes += static_cast<uint32_t>(read);
    }
  }

  if (!sourceClient || !sourceClient.connected())
  {
    NetworkClient incoming = source.accept();
    if (incoming)
    {
      sourceClient = incoming;
      sourceClient.setNoDelay(true);
      sourceBytes = 0;
      sourceWriteFails = 0;
      lastTickMs = millis();
      lastTickBytes = 0;
      ticking = true;
    }
  }
  if (sourceClient && sourceClient.connected())
  {
    // Write several MSS worth at once so lwIP hands the NCM driver a burst of
    // datagrams and it batches them into one large NTB, the way a real USB NIC
    // does under load.
    const int written = sourceClient.write(buffer, sizeof(buffer));
    if (written > 0)
    {
      sourceBytes += static_cast<uint32_t>(written);
    }
    else
    {
      sourceWriteFails++;
    }
  }

  // Once per second, print how much this side actually pushed. If the host
  // reports a stall while these ticks keep climbing, the problem is on the host
  // side; if both go to zero and canXmit sticks at 0, that is the reported
  // device-side wedge.
  if (ticking && millis() - lastTickMs >= 1000)
  {
    const uint32_t delta = sourceBytes - lastTickBytes;
    lastTickBytes = sourceBytes;
    lastTickMs = millis();
    // Deliberately no tud_network_can_xmit() here: it is not a pure query, it
    // also tries to start a transfer, and calling it every second from the
    // Arduino task would be exactly the foreign-task entry into TinyUSB this
    // test exists to catch. It is only sampled on demand, after the soak.
    Serial.printf("DEVICE_TICK bytes=%lu delta=%lu writeFails=%lu heap=%lu\n",
                  static_cast<unsigned long>(sourceBytes),
                  static_cast<unsigned long>(delta),
                  static_cast<unsigned long>(sourceWriteFails),
                  static_cast<unsigned long>(esp_get_free_heap_size()));
    if (!sourceClient.connected())
    {
      ticking = false;
    }
  }

  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == '?')
    {
      Serial.printf("DEVICE_READY ip=%s link=%u\n",
                    net.localIP().toString().c_str(),
                    net.linkUp() ? 1 : 0);
    }
    else if (command == 'c')
    {
      Serial.printf("DEVICE_COUNTS sink=%lu source=%lu\n",
                    static_cast<unsigned long>(sinkBytes),
                    static_cast<unsigned long>(sourceBytes));
    }
    else if (command == 's')
    {
      reportState("DEVICE_STATE");
    }
    else if (command == 'x')
    {
      reportDwc2();
    }
  }
}
