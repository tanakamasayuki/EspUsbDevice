#include <Arduino.h>
#include <EspUsbDevice.h>
#include <NetworkServer.h>
#include <NetworkClient.h>
#include <atomic>
#include <esp_heap_caps.h>
NetworkServer server(4567);
NetworkClient client;
bool serving=false;
EspUsbDevice device;
EspUsbDeviceNet *net = nullptr;
std::atomic<unsigned> received{0}, rejected{0};
#include "../../common/probe_link.h"
// Two 3200-byte NTBs; RX strides round up to the bulk MPS (3584 on HS).
constexpr size_t EXTERNAL_BYTES = 2*((3200+PROBE_BULK_MPS-1)/PROBE_BULK_MPS*PROBE_BULK_MPS);
alignas(64) uint8_t externalTx[EXTERNAL_BYTES], externalRx[EXTERNAL_BYTES];
bool metadata() {
  EspUsbDevice d; EspUsbDeviceNet f(d); bool ok = !f.ntbInCapacity() && !f.txBufferCount();
  EspUsbNetConfig c;
  for (size_t n : {size_t(0),size_t(2047),size_t(65536),SIZE_MAX}) {
    c.ntbInBytes=n; ok &= f.configure(c)==ESP_ERR_INVALID_SIZE && f.configuration().ntbInBytes==3200;
  }
  c={}; c.maxDatagrams=0; ok &= f.configure(c)==ESP_ERR_INVALID_SIZE;
  c={}; c.txBuffers=SIZE_MAX; ok &= f.configure(c)==ESP_ERR_INVALID_SIZE;
  const uint8_t bad[]={1,2,3,4,5,6}, zero[]={0,0,0,0,0,0};
  ok &= f.macAddress(bad)==ESP_ERR_INVALID_ARG && f.macAddress(zero)==ESP_ERR_INVALID_ARG;
  EspUsbDeviceConfig identity; identity.startMode = EspUsbDeviceStartMode::DescriptorsOnly;
  c={}; c.ntbInBytes=c.ntbOutBytes=2048; c.txBuffers=c.rxBuffers=2;
  c.txBuffer=externalTx+1; c.rxBuffer=externalRx; c.txBufferBytes=sizeof(externalTx)-1; c.rxBufferBytes=sizeof(externalRx);
  ok &= f.configure(c)==ESP_OK && !d.begin(identity) && d.lastError()==ESP_ERR_INVALID_ARG;
  c.txBuffer=externalTx; c.rxBuffer=externalTx;
  ok &= f.configure(c)==ESP_OK && !d.begin(identity) && d.lastError()==ESP_ERR_INVALID_ARG;
  c.rxBuffer=externalRx; c.txBufferBytes=1;
  ok &= f.configure(c)==ESP_OK && !d.begin(identity) && d.lastError()==ESP_ERR_INVALID_ARG;
  c.txBufferBytes=sizeof(externalTx);
  ok &= f.configure(c)==ESP_OK && d.begin(identity) && f.txBufferCount()==2 && f.rxBufferCount()==2 && d.end()==ESP_OK;
  c={}; c.txBuffers=c.rxBuffers=0;
  ok &= f.configure(c)==ESP_OK && d.begin(identity) && !f.txBufferCount() && !f.rxBufferCount() && !f.ntbInCapacity() && !f.ntbOutCapacity() && !f.beginNetwork() && d.end()==ESP_OK;
  return ok;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (serving) {
    if (!client || !client.connected()) client=server.accept();
    uint8_t data[512];
    if (client && client.available()) { int n=client.read(data,sizeof(data)); if (n>0) client.write(data,n); }
  }
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command=="metadata") {
    bool ok=metadata(); const size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (unsigned i=0;i<5 && ok;++i) ok &= metadata();
    Serial.printf("METADATA ok=%u before=%u after=%u\r\n",unsigned(ok),unsigned(before),unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
  } else if (command.startsWith("start ")) {
    unsigned capacity, buffers, external; sscanf(command.c_str(),"start %u %u %u",&capacity,&buffers,&external);
    delete net; net = new EspUsbDeviceNet(device); received = rejected = 0;
    EspUsbNetConfig c; c.ntbInBytes = c.ntbOutBytes = capacity; c.txBuffers = c.rxBuffers = buffers; c.name = "NCM probe";
    if (external) { c.txBuffer = externalTx; c.rxBuffer = externalRx; c.txBufferBytes = sizeof(externalTx); c.rxBufferBytes = sizeof(externalRx); }
    bool ok = net->configure(c) == ESP_OK;
    ok &= net->onFrame([](const uint8_t *p,size_t n) { ++received; if (!net->sendFrame(p,n)) ++rejected; }) == ESP_OK;
    const uint8_t mac[] = {2,2,0x84,0x6a,0x96,0}; ok &= net->macAddress(mac) == ESP_OK;
    ok &= device.begin() && net->ntbInCapacity() == capacity && net->ntbOutCapacity() == capacity && net->txBufferCount() == buffers && net->rxBufferCount() == buffers;
    ok &= net->configure(c) == ESP_ERR_INVALID_STATE && net->macAddress(mac) == ESP_ERR_INVALID_STATE && net->onFrame({}) == ESP_ERR_INVALID_STATE;
    Serial.printf("STARTED %u\r\n",unsigned(ok));
  } else if (command == "stats") {
    const auto s = net->stats();
    Serial.printf("STATS received=%u rejected=%u tx=%u rx=%u invalid=%u link=%u\r\n",unsigned(received.load()),unsigned(rejected.load()),unsigned(s.txFrames),unsigned(s.rxFrames),unsigned(s.invalidNtbs),unsigned(net->linkUp()));
  } else if (command.startsWith("burst ")) {
    unsigned count,n; sscanf(command.c_str(),"burst %u %u",&count,&n); uint8_t p[1514]; memset(p,0x5a,sizeof(p)); bool ok = n >= 14 && n <= sizeof(p);
    for (unsigned i = 0; i < count && ok; ++i) { p[0] = uint8_t(i); ok &= net->sendFrame(p,n); }
    Serial.printf("BURST %u\r\n",unsigned(ok));
  } else if (command=="ip_start") {
    bool ok=net->dhcpServer(true)==ESP_OK && net->beginNetwork() && net->networkUp();
    if (ok) { server.begin(); serving=true; }
    Serial.printf("IP_READY %u\r\n",unsigned(ok));
  } else if (command=="ip_stop") {
    client.stop(); server.end(); serving=false;
    Serial.printf("IP_STOPPED %u\r\n",unsigned(net->endNetwork() && net->dhcpServer(false)==ESP_OK));
  } else if (command == "network") {
    bool ok = net->ipConfig(IPAddress(192,168,7,1),IPAddress(192,168,7,1),IPAddress(255,255,255,0)) == ESP_OK;
    ok &= net->beginNetwork() && net->networkUp() && net->localIP() == IPAddress(192,168,7,1);
    ok &= net->ipConfig({}, {}, {}) == ESP_ERR_INVALID_STATE && net->endNetwork() && !net->networkUp();
    ok &= net->beginNetwork() && net->endNetwork();
    Serial.printf("NETWORK %u\r\n",unsigned(ok));
  } else if (command == "stop") { client.stop(); server.end(); serving=false; Serial.println(device.end() == ESP_OK ? "STOPPED" : "STOP_FAILED"); }
}
