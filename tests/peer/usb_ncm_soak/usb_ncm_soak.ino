#include "EspUsbHost.h"
#include <NetworkClient.h>

// Host side (DUT) of the CDC-NCM soak test. Pairs with the EspUsbDevice NCM
// device in peer_device/, which runs a DHCP server at 192.168.7.1, a TCP sink
// on port 9000 and a TCP source on port 9001.
//
// This test is about *duration*, not enumeration: the reported failure is that
// device->host traffic stops permanently "after a while" of continuous
// streaming and only an ESP32 restart brings it back. So the soak runs for tens
// of seconds to minutes and prints a per-second progress line, which is what
// pins down whether the link died and at what point.

EspUsbHost usb;

static volatile bool connected = false;
static uint8_t deviceAddress = 0;
static bool attached = false;
static uint32_t lastReportedIp = 0;

static const uint16_t SINK_PORT = 9000;
static const uint16_t SOURCE_PORT = 9001;
static const IPAddress PEER_IP(192, 168, 7, 1);
static uint8_t buffer[1460];

static void reportStats(const char *tag)
{
  EspUsbHostNetworkStats st;
  usb.networkStats(st, deviceAddress);
  Serial.printf("%s ready=%u link=%u netif=%u rxNtb=%lu rxFrames=%lu tx=%lu txFail=%lu heap=%lu block=%lu\n",
                tag,
                st.ready ? 1 : 0, st.linkUp ? 1 : 0, st.netifAttached ? 1 : 0,
                static_cast<unsigned long>(st.rxNtb),
                static_cast<unsigned long>(st.rxFrames),
                static_cast<unsigned long>(st.txFrames),
                static_cast<unsigned long>(st.txFails),
                static_cast<unsigned long>(esp_get_free_heap_size()),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

// Pull data from the peer's TCP source for durationMs. This is the direction
// the reported WebSocket workload stresses: every frame the device sends goes
// through espUsbDeviceNetTxFrame() on the device's tcpip task.
static void rxSoak(uint32_t durationMs)
{
  NetworkClient client;
  if (!client.connect(PEER_IP, SOURCE_PORT, 5000))
  {
    Serial.println("SOAK connect=0");
    return;
  }

  const uint32_t startMs = millis();
  uint32_t bytes = 0;
  uint32_t maxIdleMs = 0;
  uint32_t lastDataMs = startMs;
  uint32_t lastTickMs = startMs;
  uint32_t lastTickBytes = 0;
  uint32_t stalledSeconds = 0;
  bool disconnected = false;

  Serial.printf("SOAK start durationMs=%lu\n", static_cast<unsigned long>(durationMs));

  while (millis() - startMs < durationMs)
  {
    if (client.available() > 0)
    {
      const int read = client.read(buffer, sizeof(buffer));
      if (read > 0)
      {
        bytes += static_cast<uint32_t>(read);
        lastDataMs = millis();
      }
    }
    else
    {
      if (!client.connected())
      {
        disconnected = true;
        break;
      }
      const uint32_t idleMs = millis() - lastDataMs;
      if (idleMs > maxIdleMs)
      {
        maxIdleMs = idleMs;
      }
      delay(1);
    }

    // One line per second: a run of delta=0 is the failure signature, and its
    // position in the log gives the time-to-failure the report is vague about.
    if (millis() - lastTickMs >= 1000)
    {
      const uint32_t delta = bytes - lastTickBytes;
      lastTickBytes = bytes;
      lastTickMs = millis();
      if (delta == 0)
      {
        stalledSeconds++;
      }
      else
      {
        stalledSeconds = 0;
      }
      EspUsbHostNetworkStats st;
      usb.networkStats(st, deviceAddress);
      Serial.printf("SOAK_TICK t=%lu bytes=%lu delta=%lu stalled=%lu rxFrames=%lu tx=%lu txFail=%lu heap=%lu\n",
                    static_cast<unsigned long>((millis() - startMs) / 1000),
                    static_cast<unsigned long>(bytes),
                    static_cast<unsigned long>(delta),
                    static_cast<unsigned long>(stalledSeconds),
                    static_cast<unsigned long>(st.rxFrames),
                    static_cast<unsigned long>(st.txFrames),
                    static_cast<unsigned long>(st.txFails),
                    static_cast<unsigned long>(esp_get_free_heap_size()));
    }
  }

  const uint32_t elapsed = millis() - startMs;
  client.stop();

  Serial.printf("SOAK connect=1 bytes=%lu ms=%lu kbps=%lu maxIdleMs=%lu stalled=%lu disconnected=%u\n",
                static_cast<unsigned long>(bytes),
                static_cast<unsigned long>(elapsed),
                static_cast<unsigned long>(elapsed ? (bytes * 8ULL / elapsed) : 0),
                static_cast<unsigned long>(maxIdleMs),
                static_cast<unsigned long>(stalledSeconds),
                disconnected ? 1 : 0);
  reportStats("SOAK_STATS");
}

// After a soak, check whether the link is merely idle or permanently dead: a
// fresh connection to the peer's source port either works (link alive) or does
// not (the reported "requires esp32 restart" state).
static void probeRecovery()
{
  NetworkClient client;
  const bool ok = client.connect(PEER_IP, SOURCE_PORT, 5000);
  uint32_t bytes = 0;
  if (ok)
  {
    const uint32_t startMs = millis();
    while (millis() - startMs < 2000 && bytes < 4096)
    {
      const int read = client.read(buffer, sizeof(buffer));
      if (read > 0)
      {
        bytes += static_cast<uint32_t>(read);
      }
      else
      {
        delay(1);
      }
    }
    client.stop();
  }
  Serial.printf("RECOVER connect=%u bytes=%lu\n", ok ? 1 : 0,
                static_cast<unsigned long>(bytes));
  reportStats("RECOVER_STATS");
}

void setup()
{
  Serial.setTxBufferSize(4096);
  Serial.begin(115200);
  delay(500);

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device)
                        {
                          deviceAddress = device.address;
                          connected = true;
                          Serial.printf("HOST_CONNECTED address=%u vid=%04x pid=%04x\n",
                                        device.address, device.vid, device.pid);
                        });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device)
                           {
                             (void)device;
                             connected = false;
                             attached = false;
                             lastReportedIp = 0;
                           });

  if (!usb.begin())
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
  }
}

void loop()
{
  if (attached)
  {
    const uint32_t ip = static_cast<uint32_t>(usb.networkLocalIP(deviceAddress));
    if (ip != 0 && ip != lastReportedIp)
    {
      lastReportedIp = ip;
      Serial.printf("NETWORK_IP ip=%s\n", IPAddress(ip).toString().c_str());
    }
  }

  if (Serial.available() > 0)
  {
    const char command = Serial.read();
    if (command == 'a')
    {
      EspUsbHostNetworkConfig netConfig; // dhcpClient = true
      attached = usb.networkAttachNetif(netConfig, deviceAddress);
      Serial.printf("NETWORK_ATTACH ok=%u\n", attached ? 1 : 0);
    }
    else if (command == 'd')
    {
      reportStats("NETWORK_STATS");
    }
    else if (command == '1')
    {
      rxSoak(30000);
    }
    else if (command == '2')
    {
      rxSoak(300000);
    }
    else if (command == 'v')
    {
      probeRecovery();
    }
  }
  delay(1);
}
