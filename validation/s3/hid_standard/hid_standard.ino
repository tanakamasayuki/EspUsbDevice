#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client=nullptr;usb_device_handle_t handle=nullptr;
usb_transfer_t *control=nullptr,*input=nullptr,*output=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed=false;uint8_t in=0,out=0,interface=0;
void daemon(void *) {
  while(running.load()) {
    uint32_t flags=0;usb_host_lib_handle_events(pdMS_TO_TICKS(10),&flags);
    if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)usb_host_device_free_all();
    if(flags&USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)allFree=true;
  }
  exited=true;vTaskDelete(nullptr);
}
void event(const usb_host_client_event_msg_t *e,void *) {
  if(e->event!=USB_HOST_CLIENT_EVENT_NEW_DEV || handle)return;
  if(usb_host_device_open(client,e->new_dev.address,&handle)!=ESP_OK)return;
  const usb_config_desc_t *cfg=nullptr;if(usb_host_get_active_config_descriptor(handle,&cfg)!=ESP_OK)return;
  auto *d=reinterpret_cast<const uint8_t *>(cfg);bool selected=false;
  for(size_t n=0;n+2<=cfg->wTotalLength && d[n]>=2 && d[n]<=cfg->wTotalLength-n;n+=d[n]) {
    if(d[n+1]==4 && d[n]==9){selected=d[n+5]==3;if(selected)interface=d[n+2];}
    else if(selected && d[n+1]==5 && d[n]==7)(d[n+2]&128 ? in:out)=d[n+2];
  }
  claimed=in && out && usb_host_interface_claim(client,handle,interface,0)==ESP_OK;
}
void events(){usb_host_client_handle_events(client,pdMS_TO_TICKS(1));}
void complete(usb_transfer_t *){done=true;}
bool wait(){uint32_t until=millis()+3000;while(!done.load() && int32_t(until-millis())>0)events();return done.load();}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t index) {
  if(!done.load())return false;auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};
  s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=index;
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8;control->callback=complete;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  return wait() && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8;
}
uint8_t pattern(unsigned offset){return offset ? uint8_t((offset-1)*37+13):6;}
bool transfer(bool reading,unsigned offset,unsigned bytes) {
  if(!done.load() || offset+bytes>129 || !bytes)return false;
  auto *t=reading ? input:output;t->device_handle=handle;t->bEndpointAddress=reading ? in:out;
  t->num_bytes=reading ? (bytes+63)/64*64:bytes;t->callback=complete;
  if(!reading)for(unsigned i=0;i<bytes;++i)t->data_buffer[i]=pattern(offset+i);
  done=false;if(usb_host_transfer_submit(t)!=ESP_OK){done=true;return false;}
  if(!wait() || t->status!=USB_TRANSFER_STATUS_COMPLETED || unsigned(t->actual_num_bytes)!=bytes) {
    Serial.printf("TRANSFER_ERROR in=%u status=%u actual=%u\r\n",unsigned(reading),unsigned(t->status),unsigned(t->actual_num_bytes));return false;
  }
  if(reading)for(unsigned i=0;i<bytes;++i)if(t->data_buffer[i]!=pattern(offset+i))return false;
  return true;
}
bool reclaim() {
  bool ok=usb_host_interface_release(client,handle,interface)==ESP_OK;claimed=false;
  if(ok)claimed=usb_host_interface_claim(client,handle,interface,0)==ESP_OK;
  return ok && claimed;
}
bool feature(bool reading,bool halt) {
  uint8_t ep=reading ? in:out;
  if(!halt && (usb_host_endpoint_halt(handle,ep)!=ESP_OK || usb_host_endpoint_flush(handle,ep)!=ESP_OK || usb_host_endpoint_clear(handle,ep)!=ESP_OK))return false;
  return request(2,halt ? 3:1,0,ep);
}
bool stop() {
  if(!done.load())return false;bool ok=true;
  for(auto **t:{&control,&input,&output})if(*t){ok &= usb_host_transfer_free(*t)==ESP_OK;*t=nullptr;}
  if(claimed)ok &= usb_host_interface_release(client,handle,interface)==ESP_OK;claimed=false;
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
    in=out=0;usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *){return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    bool ok=usb_host_install(&cfg)==ESP_OK;running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"hid-standard",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t c={};c.max_num_event_msg=8;c.async.client_event_callback=event;
    ok &= usb_host_client_register(&c,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(8,0,&control)==ESP_OK && usb_host_transfer_alloc(192,0,&input)==ESP_OK && usb_host_transfer_alloc(192,0,&output)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed && int32_t(until-millis())>0)events();
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && claimed));
  } else if(s.startsWith("read ") || s.startsWith("write ")) {
    char cmd[6];unsigned offset,n;sscanf(s.c_str(),"%5s %u %u",cmd,&offset,&n);
    Serial.printf("TRANSFER %u\r\n",unsigned(transfer(cmd[0]=='r',offset,n)));
  } else if(s=="alternate")Serial.printf("ALTERNATE %u\r\n",unsigned(request(1,11,0,interface) && reclaim()));
  else if(s=="reclaim")Serial.printf("RECLAIMED %u\r\n",unsigned(reclaim()));
  else if(s.startsWith("feature ")) {
    unsigned input,halt;sscanf(s.c_str(),"feature %u %u",&input,&halt);Serial.printf("FEATURE %u\r\n",unsigned(feature(input,halt)));
  } else if(s=="stop")Serial.println(!client || stop() ? "STOPPED":"STOP_FAILED");
}
