#include "EspUsbDevice.h"
#include "EspUsbHost.h"
#include <string.h>

// One-board CDC-NCM soak: EspUsbDevice drives the NCM function on one port and
// EspUsbHost consumes it on the other. This is the only rig that can exercise
// the P4's high-speed controller, and it exists because the device-to-host bulk
// IN path could stall permanently under sustained traffic - the endpoint ending
// up enabled with packets outstanding but nothing able to feed it again, which
// killed the link until reboot.
//
// Deliberately raw frames on both sides: no beginNetwork(), no netif. Two lwIP
// interfaces on one chip would sit on the same stack, so TCP between them would
// be routed internally and never cross the USB bus at all - the test would pass
// without touching the code under test. sendFrame()/networkReadFrame() go
// straight to the bulk endpoints.

// TinyUSB's own symbol. A transmitter that has wedged reports false here
// forever, which distinguishes the failure from a merely busy link.
extern "C" bool tud_network_can_xmit(uint16_t size);

EspUsbDevice device;
EspUsbDeviceNet net(device);
EspUsbHost usb;

static constexpr uint16_t FRAME_LEN = 1514;
static constexpr uint32_t SOAK_MS = 30000;
// A second without a single frame accepted is already far outside normal
// backpressure; three consecutive ones is the reported permanent stop.
static constexpr uint32_t STALL_SECONDS_FAIL = 3;

static uint8_t txFrame[FRAME_LEN];
static uint8_t rxFrame[2048];
static volatile bool deviceConnected = false;
static uint8_t deviceAddress = 0;

static bool waitFor(volatile bool &flag, uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (!flag && millis() - start < timeoutMs)
  {
    delay(10);
  }
  return flag;
}

static bool waitNetworkReady(uint32_t timeoutMs)
{
  const uint32_t start = millis();
  while (millis() - start < timeoutMs)
  {
    if (usb.networkReady(deviceAddress) && usb.networkLinkUp(deviceAddress) && net.linkUp())
    {
      return true;
    }
    delay(10);
  }
  return false;
}

// Streams frames device -> host for SOAK_MS, draining the host side in the same
// loop, and reports whether the link kept making progress.
static bool soak()
{
  memset(txFrame, 0xa5, sizeof(txFrame));
  // Broadcast destination, locally-administered source, experimental ethertype:
  // nothing parses this, but a plausible header keeps the frame from looking
  // malformed to anything that might.
  memset(txFrame, 0xff, 6);
  txFrame[6] = 0x02;
  txFrame[11] = 0x01;
  txFrame[12] = 0x88;
  txFrame[13] = 0xb5;

  const uint32_t startMs = millis();
  uint32_t txBytes = 0;
  uint32_t rxBytes = 0;
  uint32_t txFrames = 0;
  uint32_t rxFrames = 0;
  uint32_t lastTickMs = startMs;
  uint32_t lastTickTx = 0;
  uint32_t stalledSeconds = 0;
  uint32_t worstStalledSeconds = 0;

  while (millis() - startMs < SOAK_MS)
  {
    // sendFrame() returning false is ordinary backpressure at line rate - the
    // transmit queue is full - so it is not counted as a failure. Only a second
    // with no frame accepted at all counts.
    if (net.sendFrame(txFrame, FRAME_LEN))
    {
      txBytes += FRAME_LEN;
      txFrames++;
    }

    const size_t read = usb.networkReadFrame(rxFrame, sizeof(rxFrame), deviceAddress);
    if (read > 0)
    {
      rxBytes += static_cast<uint32_t>(read);
      rxFrames++;
    }

    if (millis() - lastTickMs >= 1000)
    {
      const uint32_t delta = txBytes - lastTickTx;
      lastTickTx = txBytes;
      lastTickMs = millis();
      stalledSeconds = (delta == 0) ? stalledSeconds + 1 : 0;
      if (stalledSeconds > worstStalledSeconds)
      {
        worstStalledSeconds = stalledSeconds;
      }
      Serial.printf("NCM_TICK t=%lu tx=%lu rx=%lu delta=%lu stalled=%lu\n",
                    static_cast<unsigned long>((millis() - startMs) / 1000),
                    static_cast<unsigned long>(txBytes),
                    static_cast<unsigned long>(rxBytes),
                    static_cast<unsigned long>(delta),
                    static_cast<unsigned long>(stalledSeconds));
    }
  }

  const uint32_t elapsed = millis() - startMs;

  // Let the in-flight NTBs finish before asking whether the transmitter is
  // healthy. Straight after a full-rate stream every buffer is in flight, so
  // can_xmit is legitimately false for a moment; a wedged transmitter is the
  // one that never comes back. Keep draining the host meanwhile, since the
  // transfers only complete if something is reading them.
  const uint32_t drainStart = millis();
  while (millis() - drainStart < 2000 && !tud_network_can_xmit(FRAME_LEN))
  {
    usb.networkReadFrame(rxFrame, sizeof(rxFrame), deviceAddress);
    delay(1);
  }
  const uint32_t drainMs = millis() - drainStart;

  Serial.printf("NCM_SOAK txBytes=%lu rxBytes=%lu txFrames=%lu rxFrames=%lu ms=%lu "
                "kbps=%lu stalled=%lu drainMs=%lu canXmit=%u\n",
                static_cast<unsigned long>(txBytes),
                static_cast<unsigned long>(rxBytes),
                static_cast<unsigned long>(txFrames),
                static_cast<unsigned long>(rxFrames),
                static_cast<unsigned long>(elapsed),
                static_cast<unsigned long>(elapsed ? (txBytes * 8ULL / elapsed) : 0),
                static_cast<unsigned long>(worstStalledSeconds),
                static_cast<unsigned long>(drainMs),
                tud_network_can_xmit(FRAME_LEN) ? 1 : 0);

  // The host must have actually received the traffic: a device that queues
  // frames nobody reads would otherwise look healthy.
  const bool moved = txBytes > 1000000 && rxBytes > 1000000;
  const bool alive = worstStalledSeconds < STALL_SECONDS_FAIL && tud_network_can_xmit(FRAME_LEN);
  if (!moved)
  {
    Serial.println("NCM_FAIL throughput");
  }
  if (!alive)
  {
    Serial.println("NCM_FAIL stalled");
  }
  return moved && alive;
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("TEST_BEGIN loopback_usb_ncm");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &deviceInfo)
                        {
                          deviceAddress = deviceInfo.address;
                          Serial.printf("HOST_DEVICE vid=0x%04x pid=0x%04x\n", deviceInfo.vid, deviceInfo.pid);
                          deviceConnected = true;
                        });

  EspUsbHostConfig hostConfig;
  hostConfig.port = ESP_USB_HOST_PORT_FULL_SPEED;
  if (!usb.begin(hostConfig))
  {
    Serial.printf("HOST_BEGIN_FAILED %s\n", usb.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("HOST_READY fs");

  EspUsbDeviceConfig deviceConfig;
  deviceConfig.vid = 0x303a;
  deviceConfig.pid = 0x4032;
  deviceConfig.manufacturer = "EspUsb";
  deviceConfig.product = "EspUsbDevice Loopback NCM";
  deviceConfig.serialNumber = "espusb-loopback-ncm";

  if (!device.begin(deviceConfig))
  {
    Serial.printf("DEVICE_BEGIN_FAILED %s\n", device.lastErrorName());
    Serial.println("TEST_END fail");
    Serial.println("NG");
    return;
  }
  Serial.println("DEVICE_READY");

  bool ok = waitFor(deviceConnected, 30000);
  if (!ok)
  {
    Serial.printf("DEVICE_TIMEOUT host_error=%s device_error=%s\n",
                  usb.lastErrorName(), device.lastErrorName());
  }

  if (ok)
  {
    ok = usb.networkOpen(deviceAddress);
    Serial.printf("NCM_OPEN ok=%u\n", ok ? 1 : 0);
  }
  if (ok)
  {
    ok = waitNetworkReady(15000);
    Serial.printf("NCM_LINK ok=%u\n", ok ? 1 : 0);
  }
  if (ok)
  {
    ok = soak();
  }

  Serial.println(ok ? "TEST_END ok" : "TEST_END fail");
  Serial.println(ok ? "OK" : "NG");
}

void loop()
{
  delay(1);
}
