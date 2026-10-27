#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client = nullptr;
usb_device_handle_t handle = nullptr;
usb_transfer_t *control = nullptr, *input = nullptr, *output = nullptr;
std::atomic<bool> running{false}, exited{true}, allFree{false}, controlDone{true}, bulkDone{true};
bool claimed[4] = {};
uint8_t in[] = {0x82,0x84}, out[] = {2,4};
unsigned selected = 0;
uint32_t actual = 0;
usb_transfer_status_t result = USB_TRANSFER_STATUS_ERROR;
uint16_t le16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
uint32_t le32(const uint8_t *p) { return le16(p) | uint32_t(le16(p+2)) << 16; }
void put16(uint8_t *p,unsigned n) { p[0]=n; p[1]=n>>8; }
void put32(uint8_t *p,uint32_t n) { put16(p,n); put16(p+2,n>>16); }
void daemon(void *) {
  while (running.load()) { uint32_t flags=0; usb_host_lib_handle_events(pdMS_TO_TICKS(10),&flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
  }
  exited = true; vTaskDelete(nullptr);
}
void event(const usb_host_client_event_msg_t *e,void *) {
  if (e->event == USB_HOST_CLIENT_EVENT_NEW_DEV && !handle) usb_host_device_open(client,e->new_dev.address,&handle);
}
void events(unsigned ms) {
  const uint32_t until=millis()+ms;
  do { usb_host_client_handle_events(client,pdMS_TO_TICKS(1)); } while (int32_t(until-millis())>0);
}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t bytes,uint16_t index) {
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *s={}; s->bmRequestType=type; s->bRequest=code; s->wValue=value; s->wIndex=index; s->wLength=bytes;
  control->device_handle=handle; control->bEndpointAddress=0; control->num_bytes=8+bytes;
  control->callback=[](usb_transfer_t *) { controlDone=true; }; controlDone=false;
  if (usb_host_transfer_submit_control(client,control)!=ESP_OK) { controlDone=true; return false; }
  const uint32_t until=millis()+3000;
  while (!controlDone.load() && int32_t(until-millis())>0) events(1);
  return controlDone.load() && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8+bytes;
}
bool bulk(bool read,unsigned bytes) {
  auto *t=read ? input : output;
  t->device_handle=handle; t->bEndpointAddress=read ? in[selected] : out[selected]; t->num_bytes=bytes;
  t->callback=[](usb_transfer_t *) { bulkDone=true; }; bulkDone=false;
  if (usb_host_transfer_submit(t)!=ESP_OK) { bulkDone=true; return false; }
  const uint32_t until=millis()+3000;
  while (!bulkDone.load() && int32_t(until-millis())>0) events(1);
  if (!bulkDone.load()) {
    usb_host_endpoint_halt(handle,t->bEndpointAddress); usb_host_endpoint_flush(handle,t->bEndpointAddress);
    const uint32_t drain=millis()+1000; while (!bulkDone.load() && int32_t(drain-millis())>0) events(1);
    return false;
  }
  actual=t->actual_num_bytes; result=t->status;
  return result==USB_TRANSFER_STATUS_COMPLETED && (read || actual==bytes);
}
unsigned ntb(bool two=true,bool tableLast=false,bool reverse=false) {
  auto *p=output->data_buffer; memset(p,0,16384);
  const unsigned table=tableLast ? 144 : 12, first=tableLast ? 12 : 32, second=tableLast ? 76 : 96;
  const unsigned bytes=tableLast ? 164 : two ? 161 : 96;
  put32(p,0x484d434e); put16(p+4,12); put16(p+8,bytes); put16(p+10,table);
  put32(p+table,0x304d434e); put16(p+table+4,two ? 20 : 16);
  put16(p+table+8,reverse ? second : first); put16(p+table+10,reverse ? 65 : 64);
  if (two) { put16(p+table+12,reverse ? first : second); put16(p+table+14,reverse ? 64 : 65); }
  for (unsigned i=0;i<64;++i) p[first+i]=uint8_t((0x40+i)%251);
  if (two) for (unsigned i=0;i<65;++i) p[second+i]=uint8_t((0x41+i)%251);
  return bytes;
}
bool response(unsigned count,const unsigned *sizes,const unsigned *tags) {
  if (!bulk(true,3200)) return false;
  auto *p=input->data_buffer;
  if (actual<12 || le32(p)!=0x484d434e || le16(p+8)!=actual) return false;
  const unsigned table=le16(p+10);
  if (table+12>actual || le32(p+table)!=0x304d434e) return false;
  const unsigned n=le16(p+table+4); unsigned got=0;
  for (unsigned i=8;i+4<=n;i+=4) {
    const unsigned start=le16(p+table+i),bytes=le16(p+table+i+2);
    if (!start && !bytes) break;
    if (got>=count || bytes!=sizes[got] || start+bytes>actual) return false;
    for (unsigned j=0;j<bytes;++j) if (p[start+j]!=uint8_t((tags[got]+j)%251)) return false;
    ++got;
  }
  return got==count;
}
bool marker() {
  unsigned bytes=ntb(false); const unsigned sizes[]={64},tags[]={0x40};
  return bulk(false,bytes) && response(1,sizes,tags);
}
bool matrix() {
  bool ok=true;
  for (unsigned v=0;v<22 && ok;++v) {
    unsigned bytes=ntb(); auto *p=output->data_buffer;
    switch (v) {
    case 0: bytes=11; break; case 1: put32(p,0); break; case 2: put16(p+4,11); break;
    case 3: put16(p+8,bytes+1); break; case 4: put16(p+8,40); break;
    case 5: put16(p+10,0); break; case 6: put16(p+10,13); break; case 7: put16(p+10,160); break;
    case 8: put32(p+12,0x314d434e); break; case 9: put16(p+16,8); break; case 10: put16(p+16,13); break;
    case 11: put16(p+16,160); break; case 12: put16(p+18,12); break;
    case 13: put16(p+28,32); put16(p+30,64); break;
    case 14: put16(p+20,4); break; case 15: put16(p+20,16); break;
    case 16: put16(p+20,160); break; case 17: put16(p+22,13); break;
    case 18: put16(p+22,1515); break; case 19: put16(p+20,33); break;
    case 20: put16(p+24,32); break; case 21: put32(p+20,0); break;
    }
    ok=bulk(false,bytes) && marker();
    if (!ok) Serial.printf("CASE_FAILED %u\r\n",v);
  }
  const unsigned sizes[]={64,65},tags[]={0x40,0x41};
  for (unsigned v=0;v<4 && ok;++v) {
    unsigned bytes=ntb(true,v==2,v==3);
    if (v==1) put16(output->data_buffer+8,0);
    const unsigned reverseSizes[]={65,64},reverseTags[]={0x41,0x40};
    ok=bulk(false,bytes) && response(2,v==3 ? reverseSizes : sizes,v==3 ? reverseTags : tags);
    if (!ok) Serial.printf("VALID_FAILED %u\r\n",v);
  }
  // A datagram after the S3's first 8128-byte hardware arm.
  auto *p=output->data_buffer; memset(p,0,9001); put32(p,0x484d434e); put16(p+4,12); put16(p+8,9001); put16(p+10,12);
  put32(p+12,0x304d434e); put16(p+16,16); put16(p+20,8704); put16(p+22,65);
  for (unsigned i=0;i<65;++i) p[8704+i]=(0x55+i)%251;
  const unsigned largeSize[]={65},largeTag[]={0x55};
  return ok && bulk(false,9001) && response(1,largeSize,largeTag);
}
bool toggle() {
  bool ok=true;
  for (unsigned i=0;i<12 && ok;++i) {
    ok=request(1,11,0,0,1) && usb_host_interface_release(client,handle,1)==ESP_OK;
    claimed[1]=false;
    ok &= request(1,11,1,0,1) && usb_host_interface_claim(client,handle,1,1)==ESP_OK;
    claimed[1]=ok; ok &= marker();
  }
  return ok;
}
bool stop() {
  if (!bulkDone.load() || !controlDone.load()) return false;
  bool ok=true;
  for (auto **t : {&input,&output,&control}) if (*t) { ok &= usb_host_transfer_free(*t)==ESP_OK; *t=nullptr; }
  for (unsigned i=0;i<4;++i) { if (claimed[i]) ok &= usb_host_interface_release(client,handle,i)==ESP_OK; claimed[i]=false; }
  if (handle) { ok &= usb_host_device_close(client,handle)==ESP_OK; handle=nullptr; }
  if (client) { ok &= usb_host_client_deregister(client)==ESP_OK; client=nullptr; }
  const uint32_t until=millis()+3000; while (!allFree.load() && int32_t(until-millis())>0) delay(1);
  running=false; while (!exited.load()) delay(1);
  return ok && allFree.load() && usb_host_uninstall()==ESP_OK;
}
void setup() { Serial.begin(115200); Serial.setTimeout(100); }
void loop() {
  if (!Serial.available()) { delay(1); return; }
  String command=Serial.readStringUntil('\n'); command.trim();
  if (command=="start") {
    usb_host_config_t cfg={}; cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *) { return d->idVendor==0x303a && d->idProduct==0x4000; };
#endif
    bool ok=usb_host_install(&cfg)==ESP_OK;
    running=true; exited=false; allFree=false;
    ok &= xTaskCreate(daemon,"ncm-host",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t c={}; c.max_num_event_msg=8; c.async.client_event_callback=event;
    ok &= usb_host_client_register(&c,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(72,0,&control)==ESP_OK && usb_host_transfer_alloc(16384,0,&input)==ESP_OK && usb_host_transfer_alloc(16384,0,&output)==ESP_OK;
    const uint32_t until=millis()+10000; while (ok && !handle && int32_t(until-millis())>0) events(1);
    ok &= handle!=nullptr;
    if (ok) {
      const usb_config_desc_t *config=nullptr; ok &= usb_host_get_active_config_descriptor(handle,&config)==ESP_OK;
      unsigned found=0;
      if (ok) { const auto *d=reinterpret_cast<const uint8_t *>(config);
        for (size_t i=0;i+2<=config->wTotalLength && d[i]>=2;i+=d[i]) {
          if (i+d[i]>config->wTotalLength) { ok=false; break; }
          if (d[i]==13 && d[i+1]==0x24 && d[i+2]==0x0f) { ok &= d[i+3]==7+found; ++found; }
        }
      }
      ok &= found==2;
      uint8_t mac[26];
      ok &= request(0x80,6,0x0307,26,0x409); memcpy(mac,control->data_buffer+8,26);
      ok &= request(0x80,6,0x0308,26,0x409) && memcmp(mac,control->data_buffer+8,26)!=0;
    }
    for (unsigned i=0;i<4 && ok;++i) {
      if (i&1) ok &= request(1,11,1,0,i);
      ok &= usb_host_interface_claim(client,handle,i,i&1)==ESP_OK; claimed[i]=ok;
    }
    Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if (command=="matrix") { Serial.printf("MATRIX %u\r\n",unsigned(matrix()));
  } else if (command=="multi") { selected=1; bool ok=marker(); selected=0; Serial.printf("MULTI %u\r\n",unsigned(ok));
  } else if (command=="toggle") { Serial.printf("TOGGLE %u\r\n",unsigned(toggle()));
  } else if (command=="stop") { Serial.println(!client || stop() ? "STOPPED" : "STOP_FAILED"); }
}
