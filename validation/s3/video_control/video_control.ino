#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client=nullptr;
usb_device_handle_t handle=nullptr;
usb_transfer_t *control=nullptr,*input[2]={};
bool submitted=false;
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed[2]={};uint8_t endpoints[2]={};
void daemon(void *) {
  while (running.load()) {
    uint32_t flags = 0; usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
  }
  exited = true; vTaskDelete(nullptr);
}

void event(const usb_host_client_event_msg_t *e,void *) {
  Serial.printf("EVENT %u\r\n",unsigned(e->event));
  if (e->event!=USB_HOST_CLIENT_EVENT_NEW_DEV || handle) return;
  esp_err_t opened=usb_host_device_open(client,e->new_dev.address,&handle);Serial.printf("OPEN %d\r\n",int(opened));
  if (opened!=ESP_OK) return;
  const usb_config_desc_t *config=nullptr;
  if (usb_host_get_active_config_descriptor(handle,&config)!=ESP_OK) return;
  const uint8_t *d=reinterpret_cast<const uint8_t *>(config);int stream=-1;
  for (size_t p=0;p+2<=config->wTotalLength && d[p]>=2 && p+d[p]<=config->wTotalLength;p+=d[p]) {
    if (d[p+1]==4 && d[p]==9) {
      stream=-1;
      if (d[p+5]==14 && d[p+6]==2 && d[p+3]==0 && (d[p+2]==1 || d[p+2]==3)) stream=d[p+2]/2;
    } else if (stream>=0 && d[p+1]==5 && d[p]==7 && (d[p+3]&3)==2) endpoints[stream]=d[p+2];
  }
  for (unsigned i=0;i<2;++i) {
    esp_err_t e=endpoints[i] ? usb_host_interface_claim(client,handle,1+2*i,0) : ESP_ERR_NOT_FOUND;
    claimed[i]=e==ESP_OK;Serial.printf("CLAIM %u ep=%u error=%d\r\n",i,endpoints[i],int(e));
  }
}
void events(unsigned ms) {
  uint32_t until=millis()+ms;
  do {usb_host_client_handle_events(client,pdMS_TO_TICKS(1));} while (int32_t(until-millis())>0);
}
void completed(usb_transfer_t *) {done=true;}
bool wait() {
  uint32_t until=millis()+3000;
  while (!done.load() && int32_t(until-millis())>0) events(1);
  return done.load();
}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t index,uint16_t n,const uint8_t *data=nullptr) {
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};
  s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=index;s->wLength=n;
  if (!(type&128) && n) {memset(control->data_buffer+8,0,n);if(data) memcpy(control->data_buffer+8,data,n);}
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+n;control->callback=completed;
  submitted=false;done=false;if (usb_host_transfer_submit_control(client,control)!=ESP_OK) {done=true;return false;}
  submitted=true;
  bool ok=wait() && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8u+n;
  if (!ok) Serial.printf("REQUEST type=%u code=%u value=%u index=%u length=%u status=%u actual=%u\r\n",type,code,value,index,n,unsigned(control->status),unsigned(control->actual_num_bytes));
  return ok;
}
bool stall(uint8_t type,uint8_t code,uint16_t value,uint16_t index,uint16_t n,const uint8_t *data=nullptr) {
  request(type,code,value,index,n,data);return submitted && done.load() && control->status==USB_TRANSFER_STATUS_STALL;
}
uint32_t u32(const uint8_t *p) {return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void p32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
bool probe(unsigned i,unsigned length,unsigned payload) {
  uint8_t data[48]={};data[2]=data[3]=1;p32(data+4,10000000/15);p32(data+22,payload);
  if (!request(0x21,1,0x100,1+2*i,length,data) || !request(0xa1,0x81,0x100,1+2*i,48)) return false;
  memcpy(data,control->data_buffer+8,48);
  bool ok=data[2]==1 && data[3]==1 && u32(data+18)==(i ? 512u : 65536u) && u32(data+22)==payload;
  return ok && request(0x21,1,0x200,1+2*i,length,data);
}
bool readFrame(unsigned i,unsigned expected) {
  unsigned offset=0;bool eof=false;uint8_t fid=0;
  while (!eof && offset<=expected) {
    auto *t=input[i];t->device_handle=handle;t->bEndpointAddress=endpoints[i];t->num_bytes=128;t->callback=completed;
    done=false;if(usb_host_transfer_submit(t)!=ESP_OK){done=true;return false;}
    if(!wait() || t->status!=USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes<2)return false;
    const auto *d=t->data_buffer;unsigned n=t->actual_num_bytes;
    if(n>(i ? 128u : 65u) || d[0]!=2 || !(d[1]&128) || (offset && (d[1]&1)!=fid) || offset+n-2>expected)return false;
    fid=d[1]&1;eof=d[1]&2;
    for(unsigned j=2;j<n;++j,++offset) if(d[j]!=uint8_t(i ? offset*17+7 : offset*37+13))return false;
  }
  return eof && offset==expected;
}
bool stop() {
  if(!done.load())return false;bool ok=true;
  if(control){ok &= usb_host_transfer_free(control)==ESP_OK;control=nullptr;}
  for(unsigned i=0;i<2;++i){
    if(input[i]){ok &= usb_host_transfer_free(input[i])==ESP_OK;input[i]=nullptr;}
    if(claimed[i])ok &= usb_host_interface_release(client,handle,1+2*i)==ESP_OK;claimed[i]=false;
  }
  if(handle){ok &= usb_host_device_close(client,handle)==ESP_OK;handle=nullptr;}
  if(client){ok &= usb_host_client_deregister(client)==ESP_OK;client=nullptr;}
  uint32_t until=millis()+3000;while(!allFree.load() && int32_t(until-millis())>0)delay(1);
  running=false;while(!exited.load())delay(1);
  return ok && allFree.load() && usb_host_uninstall()==ESP_OK;
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  if(!Serial.available()){delay(1);return;}String c=Serial.readStringUntil('\n');c.trim();
  if(c=="start"){
    usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *) {return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    esp_err_t error=usb_host_install(&cfg);Serial.printf("INSTALL %d\r\n",int(error));
    bool ok=error==ESP_OK;running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"video-host",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t cc={};cc.max_num_event_msg=8;cc.async.client_event_callback=event;
    error=usb_host_client_register(&cc,&client);Serial.printf("REGISTER %d\r\n",int(error));ok &= error==ESP_OK;
    ok &= usb_host_transfer_alloc(128,0,&control)==ESP_OK;
    for(auto &t:input)ok &= usb_host_transfer_alloc(128,0,&t)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed[1] && int32_t(until-millis())>0)events(1);
    ok &= claimed[0] && claimed[1] && endpoints[0]!=endpoints[1];
    ok &= probe(0,26,65) && probe(1,34,128);
    Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if(c=="errors") {
    bool ok=true;
    for(unsigned i=0;i<2 && ok;++i){unsigned itf=1+2*i;
      ok &= request(0xa1,0x85,0x100,itf,2) && control->data_buffer[8]==48 && !control->data_buffer[9];
      ok &= request(0xa1,0x86,0x100,itf,1) && control->data_buffer[8]==3;
      for (unsigned requestCode : {0x82u,0x83u,0x84u,0x87u}) {
        const unsigned expected=requestCode==0x82 ? 3 : requestCode==0x84 ? 1 : i ? 128 : 65;
        ok &= request(0xa1,requestCode,0x100,itf,48) && u32(control->data_buffer+8+22)==expected;
      }
      ok &= request(0xa1,0x81,0x200,itf,48);uint8_t data[48];memcpy(data,control->data_buffer+8,48);
      data[2]=2;ok &= stall(0x21,1,0x200,itf,48,data);data[2]=1;
      data[3]=2;ok &= stall(0x21,1,0x100,itf,48,data);data[3]=1;
      const uint32_t nominal=u32(data+4), maximum=u32(data+18), payload=u32(data+22);
      p32(data+4,nominal+1);ok &= stall(0x21,1,0x200,itf,48,data);p32(data+4,nominal);
      p32(data+18,maximum+1);ok &= stall(0x21,1,0x200,itf,48,data);p32(data+18,maximum);
      p32(data+22,payload+1);ok &= stall(0x21,1,0x200,itf,48,data);
      p32(data+22,2);ok &= stall(0x21,1,0x100,itf,48,data);p32(data+22,payload);
      ok &= request(0xa1,0x81,0x200,itf,48) && control->data_buffer[10]==1;
      ok &= request(0xa1,0x81,0x200,itf,26) && control->actual_num_bytes==34;
      ok &= stall(0x21,1,0x100,itf,25,data) && stall(0x21,1,0x100,itf,49);
      ok &= stall(0x21,1,0x101,itf,26,data) && stall(0xa1,0x81,0x0300,itf,1);
      ok &= stall(0x01,11,1,itf,0);
      ok &= request(0x81,10,0,itf,1) && !control->data_buffer[8];
      ok &= request(0xa1,0x81,0x0200,2*i,1) && control->data_buffer[8]!=0;
      ok &= request(0xa1,0x81,0x0200,2*i,1) && !control->data_buffer[8];
      ok &= probe(i,48,i ? 128 : 65);
    }
    Serial.printf("ERRORS %u\r\n",unsigned(ok));
  } else if(c=="alternate") {
    Serial.printf("ALTERNATE %u\r\n",unsigned(request(1,11,0,1,0)));
  } else if(c=="recommit") {
    Serial.printf("RECOMMIT %u\r\n",unsigned(probe(0,48,65)));

  } else if(c.startsWith("read ")) {
    unsigned i,n;sscanf(c.c_str(),"read %u %u",&i,&n);
    Serial.printf("FRAME %u\r\n",unsigned(i<2 && readFrame(i,n)));
  } else if(c=="stop")Serial.println(!client || stop() ? "STOPPED" : "STOP_FAILED");
}
