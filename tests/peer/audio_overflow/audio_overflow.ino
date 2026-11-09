#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
// Raw SDK Host: playback only, exact OUT packet lengths and frame-numbered PCM.
usb_host_client_handle_t client=nullptr;
usb_device_handle_t handle=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false};
struct Choice { uint8_t endpoint=0,bytes=0; uint16_t packet=0; } choices[5];
usb_transfer_t *control=nullptr,*output=nullptr;
bool done=true,pending=false,claimed=false,parsed=false;
unsigned selected=0,frame=0,nextFrame=0,lost=0;
void daemon(void *) {
  while(running.load()) {
    uint32_t flags=0;usb_host_lib_handle_events(pdMS_TO_TICKS(10),&flags);
    if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)usb_host_device_free_all();
    if(flags&USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)allFree=true;
  }
  exited=true;vTaskDelete(nullptr);
}
void events(){usb_host_client_handle_events(client,pdMS_TO_TICKS(1));}
void event(const usb_host_client_event_msg_t *e,void *) {
  if(e->event!=USB_HOST_CLIENT_EVENT_NEW_DEV || handle)return;
  if(usb_host_device_open(client,e->new_dev.address,&handle)!=ESP_OK)return;
  const usb_config_desc_t *cfg=nullptr;if(usb_host_get_active_config_descriptor(handle,&cfg)!=ESP_OK)return;
  memset(choices,0,sizeof(choices));const auto *d=reinterpret_cast<const uint8_t *>(cfg);
  unsigned itf=0,alt=0;parsed=true;
  for(size_t n=9;n+2<=cfg->wTotalLength && d[n]>=2 && d[n]<=cfg->wTotalLength-n;n+=d[n]) {
    const auto *p=d+n;
    if(p[1]==4 && p[0]==9){itf=p[2];alt=p[3];parsed &= p[7]!=0x20;}
    if(itf!=1 || !alt || alt>4)continue;
    if(p[1]==0x24 && p[2]==2)choices[alt].bytes=p[5];
    if(p[1]==5 && p[0]>=7 && (p[3]&0x30)!=0x10){choices[alt].endpoint=p[2];choices[alt].packet=p[4]|unsigned(p[5])<<8;}
  }
  for(unsigned a=1;a<=4;++a)parsed &= choices[a].endpoint && choices[a].bytes==(a==1 ? 2:a==2 ? 3:4);
}
void completedControl(usb_transfer_t *){done=true;}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t index,void *data=nullptr,uint16_t length=0) {
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=index;s->wLength=length;
  if(!(type&128) && length)memcpy(control->data_buffer+8,data,length);
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+length;control->callback=completedControl;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  uint32_t until=millis()+3000;while(!done && int32_t(until-millis())>0)events();
  return done && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8+length;
}
void completedIso(usb_transfer_t *t) {
  pending=false;
  if(t->status!=USB_TRANSFER_STATUS_COMPLETED || t->isoc_packet_desc[0].status!=USB_TRANSFER_STATUS_COMPLETED)++lost;
}
bool release() {
  if(!claimed)return true;
  bool ok=request(1,11,0,1) && usb_host_interface_release(client,handle,1)==ESP_OK;
  claimed=false;selected=0;return ok;
}
bool select(unsigned alternate) {
  if(!release() || alternate<1 || alternate>4)return false;
  uint8_t rate[3]={0xc0,0x5d,0}; // 24000 Hz
  bool ok=request(0x22,1,0x0100,choices[alternate].endpoint,rate,3);
  ok=ok && usb_host_interface_claim(client,handle,1,alternate)==ESP_OK;claimed=ok;
  ok=ok && request(1,11,alternate,1);
  if(ok){selected=alternate;frame=2*choices[alternate].bytes;nextFrame=0;lost=0;}
  return ok;
}
// Whole frames carry their number; a partial tail is 0xee and never a frame.
bool send(unsigned count,unsigned length) {
  if(!selected || length>choices[selected].packet)return false;
  for(unsigned i=0;i<count;++i) {
    uint8_t *p=output->data_buffer;
    for(unsigned at=0;at+frame<=length;at+=frame,++nextFrame) {
      p[at]=uint8_t(nextFrame);p[at+1]=uint8_t(nextFrame>>8);p[at+2]=0xc5;p[at+3]=0x3a;
      for(unsigned j=4;j<frame;++j)p[at+j]=uint8_t(0x40+j+nextFrame);
    }
    memset(p+length/frame*frame,0xee,length%frame);
    output->device_handle=handle;output->callback=completedIso;output->bEndpointAddress=choices[selected].endpoint;
    output->num_bytes=length;output->isoc_packet_desc[0].num_bytes=length;
    pending=true;if(usb_host_transfer_submit(output)!=ESP_OK){pending=false;return false;}
    uint32_t until=millis()+1000;while(pending && int32_t(until-millis())>0)events();
    if(pending)return false;
  }
  return true;
}
bool stop() {
  bool ok=!handle || release();
  for(auto **p:{&control,&output})if(*p){ok &= usb_host_transfer_free(*p)==ESP_OK;*p=nullptr;}
  if(handle){ok &= usb_host_device_close(client,handle)==ESP_OK;handle=nullptr;}
  if(client){ok &= usb_host_client_deregister(client)==ESP_OK;client=nullptr;}
  uint32_t until=millis()+3000;while(!allFree.load() && int32_t(until-millis())>0)delay(1);
  running=false;while(!exited.load())delay(1);
  return ok && allFree.load() && usb_host_uninstall()==ESP_OK;
}
void setup(){Serial.begin(115200);Serial.setTimeout(100);}
void loop() {
  if(!Serial.available()){if(client)events();else delay(1);return;}
  String s=Serial.readStringUntil('\n');s.trim();
  if(s=="start") {
    usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
    cfg.fifo_settings_custom.nptx_fifo_lines=32;cfg.fifo_settings_custom.ptx_fifo_lines=64;cfg.fifo_settings_custom.rx_fifo_lines=64;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *){return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    const auto installed=usb_host_install(&cfg);bool ok=installed==ESP_OK;
    if(!ok){Serial.printf("HOST_INSTALL_FAILED %d\r\n",int(installed));return;}
    running=true;exited=false;allFree=false;handle=nullptr;
    ok &= xTaskCreate(daemon,"iso-overflow",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t c={};c.max_num_event_msg=8;c.async.client_event_callback=event;
    ok &= usb_host_client_register(&c,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(64,0,&control)==ESP_OK && usb_host_transfer_alloc(400,1,&output)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !handle && int32_t(until-millis())>0)events();
    for(unsigned a=1;a<=4;++a)Serial.printf("CHOICE alt=%u ep=%u bytes=%u packet=%u\r\n",a,unsigned(choices[a].endpoint),unsigned(choices[a].bytes),unsigned(choices[a].packet));
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && handle && parsed));
  } else if(s.startsWith("alt ")) {
    Serial.printf("SELECTED %u\r\n",unsigned(select(s.substring(4).toInt())));
  } else if(s.startsWith("send ")) {
    unsigned count=0,length=0;sscanf(s.c_str(),"send %u %u",&count,&length);
    const bool ok=send(count,length);delay(20);
    Serial.printf("SENT ok=%u next=%u lost=%u\r\n",unsigned(ok),nextFrame,lost);
  } else if(s=="pause")Serial.printf("PAUSED %u\r\n",unsigned(release()));
  else if(s=="stop")Serial.println(!running.load() || stop() ? "STOPPED":"STOP_FAILED");
}
