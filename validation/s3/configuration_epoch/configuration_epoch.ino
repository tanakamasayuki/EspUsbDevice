#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#include "../common/probe_link.h"
usb_host_client_handle_t client=nullptr;usb_device_handle_t handle=nullptr;
usb_transfer_t *control=nullptr,*input=nullptr,*output=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed=false;uint8_t in=0,out=0;
std::atomic<bool> oldDone{true};
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
  auto *d=reinterpret_cast<const uint8_t *>(cfg);
  for(size_t n=9;n+2<=cfg->wTotalLength && d[n]>=2 && d[n]<=cfg->wTotalLength-n;n+=d[n])
    if(d[n+1]==5 && d[n]>=7)(d[n+2]&128 ? in:out)=d[n+2];
  claimed=in && out && usb_host_interface_claim(client,handle,0,0)==ESP_OK;
}
void events(){usb_host_client_handle_events(client,pdMS_TO_TICKS(1));}
void complete(usb_transfer_t *){done=true;}
bool wait(){uint32_t until=millis()+3000;while(!done.load() && int32_t(until-millis())>0)events();return done.load();}
bool configureAsync() {
  if(!done.load())return false;auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};s->bRequest=9;s->wValue=1;
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8;control->callback=complete;
  done=false;if(usb_host_transfer_submit_control(client,control)==ESP_OK)return true;done=true;return false;
}
bool configurationDone() {
  if(!wait() || control->status!=USB_TRANSFER_STATUS_COMPLETED)return false;
  bool ok=usb_host_interface_release(client,handle,0)==ESP_OK;claimed=false;
  if(ok)claimed=usb_host_interface_claim(client,handle,0,0)==ESP_OK;
  return ok && claimed;
}
bool oldRead() {
  input->device_handle=handle;input->bEndpointAddress=in;input->num_bytes=PROBE_BULK_MPS;input->callback=[](usb_transfer_t *){oldDone=true;};
  oldDone=false;if(usb_host_transfer_submit(input)!=ESP_OK){oldDone=true;return false;}
  uint32_t until=millis()+3000;while(!oldDone.load() && int32_t(until-millis())>0)events();
  if(!oldDone.load())return false;
  bool ok=input->status==USB_TRANSFER_STATUS_COMPLETED && input->actual_num_bytes==64;
  for(unsigned j=0;ok && j<64;++j)ok &= input->data_buffer[j]==0xdd;
  return ok;
}
bool exchange() {
  if(!done.load())return false;input->device_handle=handle;input->bEndpointAddress=in;input->num_bytes=PROBE_BULK_MPS;input->callback=complete;
  done=false;if(usb_host_transfer_submit(input)!=ESP_OK){done=true;return false;}
  bool ok=wait() && input->status==USB_TRANSFER_STATUS_COMPLETED && input->actual_num_bytes==64;
  for(unsigned j=0;ok && j<64;++j)ok &= input->data_buffer[j]==uint8_t(j*37+13);
  output->device_handle=handle;output->bEndpointAddress=out;output->num_bytes=64;output->callback=complete;
  for(unsigned j=0;j<64;++j)output->data_buffer[j]=uint8_t(j*17+7);
  done=false;if(usb_host_transfer_submit(output)!=ESP_OK){done=true;return false;}
  return wait() && ok && output->status==USB_TRANSFER_STATUS_COMPLETED && output->actual_num_bytes==64;
}
bool stop() {
  if(!done.load() && !wait())return false;bool ok=true;
  if(!oldDone.load()) {
    ok &= usb_host_endpoint_halt(handle,in)==ESP_OK && usb_host_endpoint_flush(handle,in)==ESP_OK;
    uint32_t until=millis()+3000;while(!oldDone.load() && int32_t(until-millis())>0)events();
    if(!oldDone.load())return false;
  }
  for(auto **t:{&control,&input,&output})if(*t){ok &= usb_host_transfer_free(*t)==ESP_OK;*t=nullptr;}
  if(claimed)ok &= usb_host_interface_release(client,handle,0)==ESP_OK;claimed=false;
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
    ok &= xTaskCreate(daemon,"configuration-epoch",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t c={};c.max_num_event_msg=8;c.async.client_event_callback=event;
    ok &= usb_host_client_register(&c,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(8,0,&control)==ESP_OK && usb_host_transfer_alloc(PROBE_BULK_MPS,0,&input)==ESP_OK && usb_host_transfer_alloc(64,0,&output)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed && int32_t(until-millis())>0)events();
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && claimed));
  } else if(s=="configure")Serial.printf("CONFIG_SUBMITTED %u\r\n",unsigned(configureAsync()));
  else if(s=="old")Serial.printf("OLD_READ %u\r\n",unsigned(oldRead()));
  else if(s=="complete")Serial.printf("CONFIGURED %u\r\n",unsigned(configurationDone()));
  else if(s=="exchange")Serial.printf("EXCHANGED %u\r\n",unsigned(exchange()));
  else if(s=="stop")Serial.println(!client || stop() ? "STOPPED":"STOP_FAILED");
}
