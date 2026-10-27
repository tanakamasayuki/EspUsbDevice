#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client=nullptr;
usb_device_handle_t handle=nullptr;
usb_transfer_t *control=nullptr,*input=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed=false,submitted=false,gone=false;uint8_t endpoint=0;
unsigned connections=0;
void closeGone() {
  if(!gone || !handle || !done.load())return;
  if(claimed)usb_host_interface_release(client,handle,0);
  usb_host_device_close(client,handle);handle=nullptr;claimed=false;
}
void daemon(void *) {
  while (running.load()) {
    uint32_t flags = 0; usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
  }
  exited = true; vTaskDelete(nullptr);
}


void event(const usb_host_client_event_msg_t *e,void *) {
  if(e->event==USB_HOST_CLIENT_EVENT_DEV_GONE){gone=true;closeGone();return;}
  if(e->event!=USB_HOST_CLIENT_EVENT_NEW_DEV || handle)return;
  if(usb_host_device_open(client,e->new_dev.address,&handle)!=ESP_OK)return;
  gone=false;++connections;
  const usb_config_desc_t *cfg=nullptr;if(usb_host_get_active_config_descriptor(handle,&cfg)!=ESP_OK)return;
  const uint8_t *d=reinterpret_cast<const uint8_t *>(cfg);
  for(size_t n=0;n+2<=cfg->wTotalLength && d[n]>=2 && d[n]<=cfg->wTotalLength-n;n+=d[n])
    if(d[n]==7 && d[n+1]==5 && (d[n+2]&128))endpoint=d[n+2];
  claimed=endpoint && usb_host_interface_claim(client,handle,0,0)==ESP_OK;
}
void events(unsigned ms) {
  uint32_t until=millis()+ms;do{usb_host_client_handle_events(client,pdMS_TO_TICKS(1));closeGone();}while(int32_t(until-millis())>0);
}
void complete(usb_transfer_t *){done=true;}
bool wait(){uint32_t until=millis()+3000;while(!done.load() && int32_t(until-millis())>0)events(1);return done.load();}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t index,uint16_t n,int expected=-1) {
  if(!done.load())return false;
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};
  s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=index;s->wLength=n;
  if(!(type&128))for(unsigned i=0;i<n;++i)control->data_buffer[8+i]=uint8_t(i*37+13);
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+n;control->callback=complete;
  submitted=false;done=false;
  if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  submitted=true;bool ok=wait() && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8u+unsigned(expected<0 ? n : expected);
  if(!ok)Serial.printf("REQUEST type=%u code=%u value=%u index=%u length=%u status=%u actual=%u\r\n",type,code,value,index,n,unsigned(control->status),unsigned(control->actual_num_bytes));
  return ok;
}
bool configuration(unsigned value){return request(0x80,8,0,0,1) && control->data_buffer[8]==value;}
bool reject(uint8_t type,uint8_t code,uint16_t value,uint16_t index,uint16_t n) {
  request(type,code,value,index,n);
  return submitted && done.load() && control->status==USB_TRANSFER_STATUS_STALL && configuration(1);
}
bool stop() {
  if(!done.load()){usb_host_lib_set_root_port_power(false);wait();}
  if(!done.load())return false;bool ok=true;
  if(control){ok &= usb_host_transfer_free(control)==ESP_OK;control=nullptr;}
  if(input){ok &= usb_host_transfer_free(input)==ESP_OK;input=nullptr;}
  if(claimed){ok &= usb_host_interface_release(client,handle,0)==ESP_OK;claimed=false;}
  if(handle){ok &= usb_host_device_close(client,handle)==ESP_OK;handle=nullptr;}
  if(client){ok &= usb_host_client_deregister(client)==ESP_OK;client=nullptr;}
  uint32_t until=millis()+3000;while(!allFree.load() && int32_t(until-millis())>0)delay(1);
  running=false;while(!exited.load())delay(1);
  esp_err_t uninstalled=usb_host_uninstall();
  if(!ok || !allFree.load() || uninstalled!=ESP_OK)Serial.printf("CLEANUP ok=%u free=%u uninstall=%d\r\n",unsigned(ok),unsigned(allFree.load()),int(uninstalled));
  return ok && allFree.load() && uninstalled==ESP_OK;
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop(){
  if(!Serial.available()){delay(1);return;}String c=Serial.readStringUntil('\n');c.trim();
  if(c=="start") {
    endpoint=0;claimed=gone=false;connections=0;usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *){return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    bool ok=usb_host_install(&cfg)==ESP_OK;running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"core-host",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t cc={};cc.max_num_event_msg=8;cc.async.client_event_callback=event;
    ok &= usb_host_client_register(&cc,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(512,0,&control)==ESP_OK && usb_host_transfer_alloc(128,0,&input)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed && int32_t(until-millis())>0)events(1);
    ok &= claimed && configuration(1);Serial.printf("HOST_READY %u\r\n",unsigned(ok));
  } else if(c=="data") {
    bool ok=true;
    for(unsigned n:{0u,1u,8u,18u,64u})
      ok &= request(0x80,6,0x100,0,n,n<18 ? n : 18) && (!n || control->data_buffer[8]==18);
    for(unsigned n:{0u,1u,63u,64u,65u,127u,128u,129u}) {
      ok &= request(0xc0,0x60,n,0,n ? n : 1,n);
      for(unsigned i=0;i<n;++i)ok &= control->data_buffer[8+i]==uint8_t(i*37+13);
      if(n)ok &= request(0x40,0x61,0,0,n);
    }
    for(unsigned n:{64u,128u,129u}) {
      ok &= request(0xc0,0x60,n,0,n+1,n);
      for(unsigned i=0;i<n;++i)ok &= control->data_buffer[8+i]==uint8_t(i*37+13);
    }
    ok &= request(0x40,0x62,0,0,0) && request(0xc0,0x62,0,0,0);
    Serial.printf("DATA %u\r\n",unsigned(ok));
  } else if(c=="errors") {
    bool ok=request(0x80,0,0,0,2) && control->data_buffer[8]==1 && !control->data_buffer[9];
    for(const auto &r: {usb_setup_packet_t{.bmRequestType=0,.bRequest=6,.wValue=0x100,.wIndex=0,.wLength=18},
                       usb_setup_packet_t{.bmRequestType=0x80,.bRequest=0,.wValue=0,.wIndex=0,.wLength=1},
                       usb_setup_packet_t{.bmRequestType=0x80,.bRequest=0,.wValue=1,.wIndex=0,.wLength=2},
                       usb_setup_packet_t{.bmRequestType=0x80,.bRequest=8,.wValue=1,.wIndex=0,.wLength=1},
                       usb_setup_packet_t{.bmRequestType=0,.bRequest=9,.wValue=2,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0,.bRequest=9,.wValue=257,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0,.bRequest=9,.wValue=1,.wIndex=1,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0,.bRequest=5,.wValue=128,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0,.bRequest=5,.wValue=1,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0,.bRequest=3,.wValue=1,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0x81,.bRequest=10,.wValue=1,.wIndex=0,.wLength=1},
                       usb_setup_packet_t{.bmRequestType=1,.bRequest=11,.wValue=1,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=1,.bRequest=11,.wValue=256,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0x81,.bRequest=10,.wValue=0,.wIndex=256,.wLength=1},
                       usb_setup_packet_t{.bmRequestType=0x81,.bRequest=10,.wValue=0,.wIndex=1,.wLength=1},
                       usb_setup_packet_t{.bmRequestType=2,.bRequest=3,.wValue=0,.wIndex=0,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=2,.bRequest=3,.wValue=1,.wIndex=0x81,.wLength=0},
                       usb_setup_packet_t{.bmRequestType=0x82,.bRequest=0,.wValue=0,.wIndex=0x181,.wLength=2},
                       usb_setup_packet_t{.bmRequestType=0x82,.bRequest=0,.wValue=0,.wIndex=0x8f,.wLength=2}})
      ok &= reject(r.bmRequestType,r.bRequest,r.wValue,r.wIndex,r.wLength);
    ok &= request(0x81,10,0,0,1) && !control->data_buffer[8];
    ok &= request(1,11,0,0,0) && request(0x81,0,0,0,2) && !control->data_buffer[8] && !control->data_buffer[9];
    ok &= request(2,3,0,endpoint,0) && request(0x82,0,0,endpoint,2) && control->data_buffer[8]==1;
    ok &= request(2,1,0,endpoint,0) && request(0x82,0,0,endpoint,2) && !control->data_buffer[8];
    Serial.printf("ERRORS %u\r\n",unsigned(ok));
  } else if(c=="deconfigure") {
    Serial.printf("DECONFIGURED %u\r\n",unsigned(request(0,9,0,0,0) && configuration(0)));
  } else if(c=="configure") {
    Serial.printf("CONFIGURED %u\r\n",unsigned(request(0,9,1,0,0) && configuration(1)));
  } else if(c=="recover") {
    const unsigned before=connections;uint32_t until=millis()+10000;
    while((connections==before || !claimed) && int32_t(until-millis())>0)events(1);
    Serial.printf("RECOVERED %u\r\n",unsigned(connections>before && claimed && configuration(1)));
  } else if(c=="read") {
    input->device_handle=handle;input->bEndpointAddress=endpoint;input->num_bytes=128;input->callback=complete;
    done=false;bool ok=usb_host_transfer_submit(input)==ESP_OK && wait() && input->status==USB_TRANSFER_STATUS_COMPLETED && input->actual_num_bytes==64;
    for(unsigned i=0;i<64 && ok;++i)ok &= input->data_buffer[i]==uint8_t(i*17+7);
    Serial.printf("READ %u\r\n",unsigned(ok));
  } else if(c=="stop")Serial.println(!client || stop() ? "STOPPED" : "STOP_FAILED");
}
