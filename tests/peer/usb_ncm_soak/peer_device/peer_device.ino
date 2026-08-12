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

// TEMPORARY INVESTIGATION HOOK, matching the one in src/class/net/ncm_device.c.
// v[0..3] = xmit free / ready / tinyusb / glue NTB pointers, v[4] = datagram
// index into the glue NTB, v[5] = itf_data_alt, v[6] = ep_in busy,
// v[7] = NTBs dropped by the free list, v[8] = NTBs dropped by the ready list,
// v[9] = times no free NTB was available, v[10] = ep_in, v[11] = ep_size,
// v[12] = times the usbd task ran netd_xfer_cb() while another task was inside
// tud_network_xmit(), v[13] = total netd_xfer_cb() calls.
extern "C" void espusb_ncm_dbg_state(uint32_t *v);
extern "C" const char *espusb_ncm_dbg_tx_task_name(void);

// The stall leaves TinyUSB believing an IN transfer is still in flight. Reading
// the dwc2 IN endpoint registers says who is actually waiting for whom:
// DIEPCTL.EPENA == 1 means the device armed the endpoint and is waiting for an
// IN token, i.e. the host stopped polling. EPENA == 0 while TinyUSB still
// thinks the endpoint is busy means the transfer was lost on the device side.
static const uint32_t USB_OTG_BASE = 0x60080000;
#define DWC2_REG(off) (*reinterpret_cast<volatile uint32_t *>(USB_OTG_BASE + (off)))
#define DWC2_DIEPCTL(n) DWC2_REG(0x900 + 0x20 * (n))
#define DWC2_DIEPINT(n) DWC2_REG(0x908 + 0x20 * (n))
#define DWC2_DIEPTSIZ(n) DWC2_REG(0x910 + 0x20 * (n))

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

static void reportDwc2(const char *tag, uint8_t epIn)
{
  const uint8_t n = epIn & 0x0f;
  const uint32_t ctl = DWC2_DIEPCTL(n);
  Serial.printf("%s ep=%u diepctl=%08lx epena=%lu naksts=%lu stall=%lu "
                "diepint=%08lx dieptsiz=%08lx gintsts=%08lx gintmsk=%08lx daint=%08lx\n",
                tag, n,
                static_cast<unsigned long>(ctl),
                static_cast<unsigned long>((ctl >> 31) & 1),
                static_cast<unsigned long>((ctl >> 17) & 1),
                static_cast<unsigned long>((ctl >> 21) & 1),
                static_cast<unsigned long>(DWC2_DIEPINT(n)),
                static_cast<unsigned long>(DWC2_DIEPTSIZ(n)),
                static_cast<unsigned long>(DWC2_REG(0x014)),
                static_cast<unsigned long>(DWC2_REG(0x018)),
                static_cast<unsigned long>(DWC2_REG(0x818)));
}

static void reportNcmState(const char *tag)
{
  uint32_t v[14] = {};
  espusb_ncm_dbg_state(v);
  Serial.printf("%s free=%08lx ready=%08lx tinyusb=%08lx glue=%08lx ndx=%lu "
                "alt=%lu epBusy=%lu freeDrops=%lu readyDrops=%lu noFree=%lu\n",
                tag,
                static_cast<unsigned long>(v[0]), static_cast<unsigned long>(v[1]),
                static_cast<unsigned long>(v[2]), static_cast<unsigned long>(v[3]),
                static_cast<unsigned long>(v[4]), static_cast<unsigned long>(v[5]),
                static_cast<unsigned long>(v[6]), static_cast<unsigned long>(v[7]),
                static_cast<unsigned long>(v[8]), static_cast<unsigned long>(v[9]));
  Serial.printf("DEVICE_RACE txTask=%s overlaps=%lu xferCbs=%lu\n",
                espusb_ncm_dbg_tx_task_name(),
                static_cast<unsigned long>(v[12]),
                static_cast<unsigned long>(v[13]));
  reportDwc2("DEVICE_DWC2", static_cast<uint8_t>(v[10]));
}

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
    uint32_t v[14] = {};
    espusb_ncm_dbg_state(v);
    // Deliberately no tud_network_can_xmit() here: it is not a pure query, it
    // also tries to start a transfer, which would add another foreign-task
    // caller to the very race under investigation. espusb_ncm_dbg_state() only
    // reads.
    Serial.printf("DEVICE_TICK bytes=%lu delta=%lu writeFails=%lu "
                  "free=%08lx ready=%08lx tinyusb=%08lx glue=%08lx epBusy=%lu "
                  "freeDrops=%lu readyDrops=%lu noFree=%lu heap=%lu\n",
                  static_cast<unsigned long>(sourceBytes),
                  static_cast<unsigned long>(delta),
                  static_cast<unsigned long>(sourceWriteFails),
                  static_cast<unsigned long>(v[0]),
                  static_cast<unsigned long>(v[1]),
                  static_cast<unsigned long>(v[2]),
                  static_cast<unsigned long>(v[3]),
                  static_cast<unsigned long>(v[6]),
                  static_cast<unsigned long>(v[7]),
                  static_cast<unsigned long>(v[8]),
                  static_cast<unsigned long>(v[9]),
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
      reportNcmState("DEVICE_NCM");
    }
  }
}
