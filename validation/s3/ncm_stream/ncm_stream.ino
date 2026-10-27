#include <Arduino.h>
#include <EspUsbHost.h>
#include <NetworkClient.h>
#include <atomic>
EspUsbHost host;
std::atomic<uint8_t> address{0};
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  host.onDeviceConnected([](const EspUsbHostDeviceInfo &d) { if (d.vid == 0x303a && d.pid == 0x4000) address = d.address; });
}
bool read(uint8_t *p,size_t n) {
  const uint32_t until = millis()+3000;
  while (int32_t(until-millis())>0) { size_t actual = host.networkReadFrame(p,n,address.load()); if (actual) return actual == n; delay(1); }
  return false;
}
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command == "start") {
    address = 0; bool ok = host.begin(); const uint32_t until = millis()+10000;
    while (ok && !address.load() && int32_t(until-millis())>0) delay(1);
    ok &= address.load() && host.networkOpen(address.load());
    EspUsbHostNetworkInterfaceInfo info; ok &= host.getNetworkInterfaces(address.load(),&info,1) == 1 && info.macAddressStringIndex == 6;
    uint8_t mac[26]; size_t n = 0;
    ok &= host.vendorControlTransfer(0x80,6,0x0306,0x409,mac,sizeof(mac),&n,address.load(),1000) && n == 26;
    const char *expected = "0202846A9600";
    for (unsigned i = 0; i < 12; ++i) ok &= mac[2+2*i] == expected[i] && !mac[3+2*i];
    Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if (command.startsWith("exchange ")) {
    unsigned n = command.substring(9).toInt(); uint8_t sent[1514], received[1514]; bool ok = n >= 14 && n <= sizeof(sent);
    for (unsigned i = 0; i < n && ok; ++i) sent[i] = uint8_t(i*37+n);
    while (host.networkReadFrame(received,sizeof(received),address.load())) {}
    ok &= host.networkWriteFrame(sent,n,address.load()) && read(received,n) && !memcmp(sent,received,n);
    Serial.printf("EXCHANGE %u %u\r\n",n,unsigned(ok));
  } else if (command.startsWith("burst ")) {
    unsigned count,n; sscanf(command.c_str(),"burst %u %u",&count,&n); uint8_t p[1514]; bool ok = n >= 14 && n <= sizeof(p);
    for (unsigned i = 0; i < count && ok; ++i) { ok &= read(p,n) && p[0] == i; for (unsigned j = 1; j < n; ++j) ok &= p[j] == 0x5a; }
    Serial.printf("RECEIVED %u\r\n",unsigned(ok));
  } else if (command == "limits") {
    uint8_t input[28] = {}, params[28] = {}; size_t actual = 0;
    bool ok = host.vendorControlTransfer(0xa1,0x80,0,0,params,28,&actual,address.load(),1000) && actual == 28 && params[0] == 28;
    input[0] = 0; input[1] = 8;
    ok &= host.vendorControlTransfer(0x21,0x86,0,0,input,8,&actual,address.load(),1000);
    ok &= host.vendorControlTransfer(0xa1,0x85,0,0,input,8,&actual,address.load(),1000) && actual == 8 && input[0] == 0 && input[1] == 8;
    input[0] = 0xff; input[1] = 7;
    ok &= !host.vendorControlTransfer(0x21,0x86,0,0,input,4,&actual,address.load(),1000);
    ok &= host.vendorControlTransfer(0xa1,0x85,0,0,input,8,&actual,address.load(),1000) && input[0] == 0 && input[1] == 8;
    Serial.printf("LIMITS %u\r\n",unsigned(ok));
  } else if (command=="tcp") {
    bool ok=host.networkAttachNetif(EspUsbHostNetworkConfig{},address.load());
    const uint32_t until=millis()+10000;
    while (ok && host.networkLocalIP(address.load())==IPAddress() && int32_t(until-millis())>0) delay(1);
    const IPAddress ip=host.networkLocalIP(address.load());
    ok &= ip[0]==192 && ip[1]==168 && ip[2]==7 && ip[3]!=0 && ip[3]!=1;
    NetworkClient client; ok &= client.connect(IPAddress(192,168,7,1),4567,3000);
    const size_t total=8193; uint8_t data[512]; size_t sent=0, received=0;
    while (sent<total && ok) {
      size_t n=std::min(sizeof(data),total-sent);
      for (size_t i=0;i<n;++i) data[i]=uint8_t((sent+i)*37+13);
      ok &= client.write(data,n)==n; sent+=n;
    }
    const uint32_t readUntil=millis()+10000;
    while (received<total && ok && int32_t(readUntil-millis())>0) {
      int n=client.read(data,std::min(sizeof(data),total-received));
      if (n<=0) { delay(1); continue; }
      for (int i=0;i<n;++i) ok &= data[i]==uint8_t((received+i)*37+13);
      received+=n;
    }
    client.stop(); ok &= received==total && host.networkDetachNetif(address.load());
    Serial.printf("TCP %u bytes=%u ip=%u.%u.%u.%u\r\n",unsigned(ok),unsigned(received),ip[0],ip[1],ip[2],ip[3]);
  } else if (command == "stop") { host.end(); Serial.println(!host.ready() ? "STOPPED" : "STOP_FAILED"); }
}
