#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client=nullptr;usb_device_handle_t handle=nullptr;
usb_transfer_t *control=nullptr,*input[2]={},*output[2]={};
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed[2]={};uint8_t interfaces[2]={},in[2]={},out[2]={},notice[2]={};
uint32_t capacities[2]={};unsigned selected=0;uint8_t sequence=0;
void daemon(void *) {
  while (running.load()) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree.store(true);
  }
  exited.store(true); vTaskDelete(nullptr);
}

void event(const usb_host_client_event_msg_t *e,void *) {
  if(e->event!=USB_HOST_CLIENT_EVENT_NEW_DEV || handle)return;
  if(usb_host_device_open(client,e->new_dev.address,&handle)!=ESP_OK)return;
  const usb_config_desc_t *cfg=nullptr;if(usb_host_get_active_config_descriptor(handle,&cfg)!=ESP_OK)return;
  const auto *d=reinterpret_cast<const uint8_t *>(cfg);int selected=-1;unsigned found=0;
  for(size_t n=0;n+2<=cfg->wTotalLength && d[n]>=2 && d[n]<=cfg->wTotalLength-n;n+=d[n]) {
    if(d[n+1]==4 && d[n]==9) {
      selected=-1;
      if(d[n+5]==11) {if(found<2){selected=found;interfaces[found++]=d[n+2];}}
    } else if(selected>=0 && d[n+1]==0x21 && d[n]==54) {
      capacities[selected]=uint32_t(d[n+44]) | uint32_t(d[n+45])<<8 | uint32_t(d[n+46])<<16 | uint32_t(d[n+47])<<24;
    } else if(d[n+1]==5 && d[n]==7) {
      if(selected>=0 && (d[n+3]&3)==2) {(d[n+2]&128 ? in[selected]:out[selected])=d[n+2];}
      else if(selected>=0 && (d[n+3]&3)==3)notice[selected]=d[n+2];
    }
  }
  for(unsigned i=0;i<2;++i)claimed[i]=in[i] && out[i] && usb_host_interface_claim(client,handle,interfaces[i],0)==ESP_OK;
}
void events(unsigned ms){uint32_t until=millis()+ms;do{usb_host_client_handle_events(client,pdMS_TO_TICKS(1));}while(int32_t(until-millis())>0);}
void complete(usb_transfer_t *){done=true;}
bool wait(){uint32_t until=millis()+3000;while(!done.load() && int32_t(until-millis())>0)events(1);return done.load();}

uint32_t get32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void put32(uint8_t *p,uint32_t n){for(unsigned j=0;j<4;++j)p[j]=uint8_t(n>>(j*8));}
bool transfer(usb_transfer_t *t,uint8_t endpoint,unsigned bytes) {
  if(!done.load())return false;t->device_handle=handle;t->bEndpointAddress=endpoint;t->num_bytes=bytes;t->callback=complete;
  done=false;if(usb_host_transfer_submit(t)!=ESP_OK){done=true;return false;}
  bool ok=wait() && t->status==USB_TRANSFER_STATUS_COMPLETED;
  if(!ok)Serial.printf("TRANSFER_ERROR ep=%u status=%u actual=%u\r\n",endpoint,unsigned(t->status),unsigned(t->actual_num_bytes));
  return ok;
}
bool message(uint8_t type,unsigned n,unsigned expected,int error=-1,uint8_t slot=0,uint64_t declared=UINT64_MAX,uint8_t specific=0) {
  auto *t=output[selected];memset(t->data_buffer,0,10);t->data_buffer[0]=type;put32(t->data_buffer+1,declared==UINT64_MAX ? n:uint32_t(declared));t->data_buffer[5]=slot;t->data_buffer[6]=++sequence;t->data_buffer[7]=specific;
  for(unsigned j=0;j<n;++j)t->data_buffer[10+j]=uint8_t(j*37+n);
  if(!transfer(t,out[selected],10+n) || t->actual_num_bytes!=10+int(n))return false;
  t=input[selected];if(!transfer(t,in[selected],((expected+10+63)/64+1)*64))return false;
  bool ok=t->actual_num_bytes==int(expected+10) && get32(t->data_buffer+1)==expected && t->data_buffer[6]==sequence && (t->data_buffer[7]&0xc0)==(error<0 ? 0:0x40) && (error<0 || t->data_buffer[8]==error) && t->data_buffer[0]==(type==0x62 || type==0x6f ? 0x80:type==0x6e ? 0x83:(type==0x61 || type==0x6c || type==0x6d) ? 0x82:0x81);
  if(!ok)Serial.printf("REPLY_ERROR type=%u status=%u error=%u actual=%u length=%u\r\n",t->data_buffer[0],t->data_buffer[7],t->data_buffer[8],unsigned(t->actual_num_bytes),unsigned(get32(t->data_buffer+1)));
  return ok;
}
bool setupRequest(uint8_t type,uint8_t code,uint16_t value) {
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=interfaces[selected];
  control->device_handle=handle;control->num_bytes=8;control->bEndpointAddress=0;control->callback=complete;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  return wait() && control->status==USB_TRANSFER_STATUS_COMPLETED;
}
bool abortCommand() {
  return setupRequest(0x21,1,uint16_t(uint8_t(sequence+2))<<8) && message(0x72,0,0,0xff) && message(0x72,0,0);
}
bool abandon(bool aborting=false) {
  if(aborting && !setupRequest(0x21,1,uint16_t(uint8_t(sequence+1))<<8))return false;
  auto *t=output[selected];memset(t->data_buffer,0,10);t->data_buffer[0]=0x62;t->data_buffer[6]=++sequence;
  if(!transfer(t,out[selected],10) || !setupRequest(1,11,0))return false;
  bool ok=usb_host_interface_release(client,handle,interfaces[selected])==ESP_OK;claimed[selected]=false;
  if(ok)claimed[selected]=usb_host_interface_claim(client,handle,interfaces[selected],0)==ESP_OK;
  return ok && claimed[selected];
}
bool stop() {
  if(!done.load())return false;bool ok=true;
  if(control){ok &= usb_host_transfer_free(control)==ESP_OK;control=nullptr;}
  for(unsigned i=0;i<2;++i) {
    if(input[i]){ok &= usb_host_transfer_free(input[i])==ESP_OK;input[i]=nullptr;}
    if(output[i]){ok &= usb_host_transfer_free(output[i])==ESP_OK;output[i]=nullptr;}
    if(claimed[i])ok &= usb_host_interface_release(client,handle,interfaces[i])==ESP_OK;claimed[i]=false;
  }
  if(handle){ok &= usb_host_device_close(client,handle)==ESP_OK;handle=nullptr;}
  if(client){ok &= usb_host_client_deregister(client)==ESP_OK;client=nullptr;}
  uint32_t until=millis()+3000;while(!allFree.load() && int32_t(until-millis())>0)delay(1);
  running=false;while(!exited.load())delay(1);
  return ok && allFree.load() && usb_host_uninstall()==ESP_OK;
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop() {
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    memset(in,0,sizeof(in));memset(out,0,sizeof(out));memset(notice,0,sizeof(notice));
    usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *){return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    bool ok=usb_host_install(&cfg)==ESP_OK;running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"ccid-stream",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t cc={};cc.max_num_event_msg=8;cc.async.client_event_callback=event;
    ok &= usb_host_client_register(&cc,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(8,0,&control)==ESP_OK;
    for(unsigned i=0;i<2;++i)ok &= usb_host_transfer_alloc(2048,0,&input[i])==ESP_OK && usb_host_transfer_alloc(2048,0,&output[i])==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed[1] && int32_t(until-millis())>0)events(1);
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && claimed[0] && claimed[1]));
  } else if(s.startsWith("open ")) {
    unsigned bytes,removable;sscanf(s.c_str(),"open %u %u %u",&selected,&bytes,&removable);Serial.printf("OPENED %u\r\n",unsigned(selected<2 && capacities[selected]==bytes && bool(notice[selected])==bool(removable)));
  } else if(s=="power") {
    bool ok=message(0x62,0,4);auto *p=input[selected]->data_buffer;
    Serial.printf("POWERED %u\r\n",unsigned(ok && p[0]==0x80 && p[10]==0x3b && p[11]==0x80 && p[12]==1 && p[13]==0x81));
  } else if(s.startsWith("exchange ")) {
    unsigned n,escape;sscanf(s.c_str(),"exchange %u %u",&n,&escape);
    bool ok=n<=1536 && message(escape ? 0x6e:0x6f,n,n);auto *p=input[selected]->data_buffer;
    for(unsigned j=0;ok && j<n;++j)ok &= p[10+j]==uint8_t(uint8_t(j*37+n)^(0x31+selected));
    Serial.printf("EXCHANGED %u\r\n",unsigned(ok));
  } else if(s.startsWith("present ")) {
    bool ok=message(0x65,0,0);Serial.printf("PRESENT %u\r\n",unsigned(ok && ((input[selected]->data_buffer[7]&3)!=2)==bool(s.substring(8).toInt())));
  } else if(s=="abort")Serial.printf("ABORTED %u\r\n",unsigned(abortCommand()));
  else if(s=="errors") {
    bool ok=message(0x65,0,0,5,1) && message(0xff,0,0,0) && message(0x6e,capacities[selected]-9,0,0xfc) && message(0x6f,0,0,0xfc,0,UINT32_MAX);
    Serial.printf("ERRORS %u\r\n",unsigned(ok));
  } else if(s.startsWith("notice ")) {
    bool ok=notice[selected] && transfer(input[selected],notice[selected],8);
    auto *t=input[selected];ok &= t->actual_num_bytes==2 && t->data_buffer[0]==0x50 && (t->data_buffer[1]&1)==s.substring(7).toInt();
    Serial.printf("NOTICE %u\r\n",unsigned(ok));
  } else if(s=="abandon" || s=="abandon_abort")Serial.printf("ABANDONED %u\r\n",unsigned(abandon(s=="abandon_abort")));
  else if(s=="off")Serial.printf("POWER_OFF %u\r\n",unsigned(message(0x63,0,0) && (input[selected]->data_buffer[7]&3)==1));
  else if(s=="parameters") {
    const uint8_t expected[]={0x11,0x10,0,0x4d,0,0xfe,0};
    bool ok=message(0x6c,0,7) && !memcmp(input[selected]->data_buffer+10,expected,7);
    ok &= message(0x6d,0,7) && !memcmp(input[selected]->data_buffer+10,expected,7);
    ok &= message(0x61,7,0,7) && message(0x61,7,7,-1,0,UINT64_MAX,1) && !memcmp(input[selected]->data_buffer+10,expected,7);
    Serial.printf("PARAMETERS %u\r\n",unsigned(ok));
  }
  else if(s=="stop")Serial.println(!client || stop() ? "STOPPED":"STOP_FAILED");
}
