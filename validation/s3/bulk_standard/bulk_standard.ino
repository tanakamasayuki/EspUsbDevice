#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#include "../common/probe_link.h"
usb_host_client_handle_t client=nullptr;usb_device_handle_t handle=nullptr;
usb_transfer_t *control=nullptr,*input[2]={},*output[2]={};
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed[2]={};uint8_t interfaces[2]={},in[2]={},out[2]={};
unsigned rxOffset[2]={},txOffset[2]={};uint8_t notification=0;
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
      if(d[n+5]==255 || d[n+5]==10) {if(found<2){selected=found;interfaces[found++]=d[n+2];}}
    } else if(d[n+1]==5 && d[n]==7) {
      if(selected>=0 && (d[n+3]&3)==2) {(d[n+2]&128 ? in[selected]:out[selected])=d[n+2];}
      else if((d[n+3]&3)==3)notification=d[n+2];
    }
  }
  for(unsigned i=0;i<2;++i)claimed[i]=in[i] && out[i] && usb_host_interface_claim(client,handle,interfaces[i],0)==ESP_OK;
}
void events(unsigned ms){uint32_t until=millis()+ms;do{usb_host_client_handle_events(client,pdMS_TO_TICKS(1));}while(int32_t(until-millis())>0);}
void complete(usb_transfer_t *){done=true;}
bool wait(){uint32_t until=millis()+3000;while(!done.load() && int32_t(until-millis())>0)events(1);return done.load();}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t index,uint16_t n=0) {
  if(!done.load())return false;auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};
  s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=index;s->wLength=n;
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+n;control->callback=complete;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  return wait() && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8+n;
}
bool coding(unsigned n,unsigned invalid) {
  if(n>7 || !notification || !done.load())return false;
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};
  s->bmRequestType=0x21;s->bRequest=0x20;s->wIndex=interfaces[1]-1;s->wLength=n;
  const uint32_t baud=n==7 ? 115200:9600;
  for(unsigned i=0;i<4;++i)control->data_buffer[8+i]=uint8_t(baud>>(8*i));
  control->data_buffer[12]=control->data_buffer[13]=0;control->data_buffer[14]=8;
  if(invalid==1)memset(control->data_buffer+8,0,4);
  if(invalid==2)control->data_buffer[12]=3;
  if(invalid==3)control->data_buffer[13]=5;
  if(invalid==4)control->data_buffer[14]=4;
  if(invalid==5)control->data_buffer[14]=9;
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+n;control->callback=complete;
  done=false;const esp_err_t submitted=usb_host_transfer_submit_control(client,control);
  if(submitted!=ESP_OK){done=true;Serial.printf("CODING_SUBMIT %s\r\n",esp_err_to_name(submitted));return false;}
  if(!wait())return false;
  const unsigned status=control->status,actual=control->actual_num_bytes;
  const bool expected=control->status==(n==7 && !invalid ? USB_TRANSFER_STATUS_COMPLETED:USB_TRANSFER_STATUS_STALL);
  bool ok=request(0xa1,0x21,0,interfaces[1]-1,7);
  const uint8_t wanted[]={0x00,0xc2,0x01,0,0,0,8};
  ok &= !memcmp(control->data_buffer+8,wanted,7);
  Serial.printf("CODING_RESULT n=%u status=%u actual=%u preserved=%u\r\n",n,status,actual,unsigned(ok));
  return expected && ok;
}
bool bulk(unsigned i,bool reading,unsigned n) {
  if(!done.load())return false;auto *t=reading ? input[i]:output[i];
  t->device_handle=handle;t->bEndpointAddress=reading ? in[i]:out[i];t->num_bytes=reading ? (n+PROBE_BULK_MPS-1)/PROBE_BULK_MPS*PROBE_BULK_MPS:n;t->callback=complete;
  if(!reading)for(unsigned j=0;j<n;++j)t->data_buffer[j]=uint8_t((txOffset[i]+j)*17+7+i*53);
  done=false;if(usb_host_transfer_submit(t)!=ESP_OK){done=true;return false;}
  if(!wait() || t->status!=USB_TRANSFER_STATUS_COMPLETED || t->actual_num_bytes!=n){Serial.printf("BULK_ERROR i=%u in=%u requested=%u status=%u actual=%u\r\n",i,unsigned(reading),n,unsigned(t->status),unsigned(t->actual_num_bytes));return false;}
  if(reading){for(unsigned j=0;j<n;++j)if(t->data_buffer[j]!=uint8_t((rxOffset[i]+j)*37+13+i*41)){Serial.printf("DATA_ERROR i=%u offset=%u got=%u expected=%u\r\n",i,rxOffset[i]+j,t->data_buffer[j],uint8_t((rxOffset[i]+j)*37+13+i*41));return false;}rxOffset[i]+=n;}
  else txOffset[i]+=n;
  return true;
}
bool clear(unsigned i,bool reading) {
  const uint8_t ep=reading ? in[i]:out[i];
  bool ok=usb_host_endpoint_halt(handle,ep)==ESP_OK && usb_host_endpoint_flush(handle,ep)==ESP_OK && usb_host_endpoint_clear(handle,ep)==ESP_OK;
  if(reading)rxOffset[i]=0;else txOffset[i]=0;
  return ok && request(2,1,0,ep);
}
bool reclaim(unsigned i) {
  bool ok=usb_host_interface_release(client,handle,interfaces[i])==ESP_OK;claimed[i]=false;
  if(ok)claimed[i]=usb_host_interface_claim(client,handle,interfaces[i],0)==ESP_OK;
  rxOffset[i]=txOffset[i]=0;return ok && claimed[i];
}
bool alternate(unsigned i) {return request(1,11,0,interfaces[i]) && reclaim(i);}
bool stop() {
  if(!done.load())return false;bool ok=true;
  if(control){ok &= usb_host_transfer_free(control)==ESP_OK;control=nullptr;}
  for(unsigned i=0;i<2;++i){
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
void loop(){
  if(!Serial.available()){delay(1);return;}String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    notification=0;memset(in,0,sizeof(in));memset(out,0,sizeof(out));memset(rxOffset,0,sizeof(rxOffset));memset(txOffset,0,sizeof(txOffset));
    usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *){return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    bool ok=usb_host_install(&cfg)==ESP_OK;running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"bulk-standard",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t cc={};cc.max_num_event_msg=8;cc.async.client_event_callback=event;
    ok &= usb_host_client_register(&cc,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(64,0,&control)==ESP_OK;
    for(unsigned i=0;i<2;++i)ok &= usb_host_transfer_alloc(4*PROBE_BULK_MPS,0,&input[i])==ESP_OK && usb_host_transfer_alloc(4*PROBE_BULK_MPS,0,&output[i])==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed[1] && int32_t(until-millis())>0)events(1);
    ok &= claimed[0] && claimed[1];if(notification)ok &= request(0x21,0x22,1,interfaces[1]-1);
    Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if(s.startsWith("fresh ")) {
    unsigned i=s.substring(6).toInt();rxOffset[i]=txOffset[i]=0;Serial.println("FRESH");
  } else if(s.startsWith("read ") || s.startsWith("write ")) {
    unsigned i,n;char cmd[6];sscanf(s.c_str(),"%5s %u %u",cmd,&i,&n);
    Serial.printf("BULK %u\r\n",unsigned(i<2 && bulk(i,cmd[0]=='r',n)));
  } else if(s.startsWith("alternate ")) {
    unsigned i=s.substring(10).toInt();Serial.printf("ALTERNATE %u\r\n",unsigned(alternate(i)));
  } else if(s.startsWith("reclaim ")) {
    unsigned i=s.substring(8).toInt();Serial.printf("RECLAIMED %u\r\n",unsigned(reclaim(i)));
  } else if(s.startsWith("halt ") || s.startsWith("clear ")) {
    unsigned i,input;char cmd[6];sscanf(s.c_str(),"%5s %u %u",cmd,&i,&input);
    bool ok=cmd[0]=='h' ? request(2,3,0,input ? in[i]:out[i]) : clear(i,input);
    Serial.printf("FEATURE %u\r\n",unsigned(ok));
  } else if(s.startsWith("coding ")) {
    unsigned n,invalid;sscanf(s.c_str(),"coding %u %u",&n,&invalid);
    Serial.printf("CODING %u\r\n",unsigned(coding(n,invalid)));
  } else if(s=="control") {
    bool ok=notification && request(2,3,0,notification) && request(1,11,0,interfaces[1]-1);
    ok &= request(0x82,0,0,notification,2) && !control->data_buffer[8];
    Serial.printf("CONTROL %u\r\n",unsigned(ok));
  } else if(s=="stop")Serial.println(!client || stop() ? "STOPPED":"STOP_FAILED");
}
