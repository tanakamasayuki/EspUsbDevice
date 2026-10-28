#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
usb_host_client_handle_t client=nullptr;
usb_device_handle_t handle=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false};
struct Choice { uint8_t endpoint=0,feedback=0,bytes=0,bits=0; uint16_t packet=0; } choices[2][5];
usb_transfer_t *control=nullptr,*input=nullptr,*output=nullptr,*feedback=nullptr;
bool done=true,streaming=false,pending[3]={},claimed[2]={},parsed=false;
unsigned protocol=0,selected=0,rate=0,frame=0,received=0,sent=0,filled=0,feedbacks=0,bad=0,lost=0;
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
    if(p[1]==4 && p[0]==9){itf=p[2];alt=p[3];protocol=p[7]==0x20 ? 2:1;}
    if(itf<1 || itf>2 || !alt || alt>4)continue;
    auto &c=choices[itf-1][alt];
    if(p[1]==0x24 && p[2]==2) {
      c.bytes=protocol==2 ? p[4]:p[5];c.bits=protocol==2 ? p[5]:p[6];
      if(protocol==1)parsed &= p[0]==14 && p[7]==2;
    }
    if(p[1]==5 && p[0]>=7) {
      if((p[3]&0x30)==0x10)c.feedback=p[2];
      else {c.endpoint=p[2];c.packet=p[4]|unsigned(p[5])<<8;}
    }
  }
  for(unsigned i=0;i<2;++i)for(unsigned a=1;a<=4;++a) {
    const auto &c=choices[i][a];parsed &= c.endpoint && c.bytes==(a==1 ? 2:a==2 ? 3:4) && c.bits==(a==1 ? 16:a==3 ? 32:24);
  }
}
void completedControl(usb_transfer_t *){done=true;}
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t index,void *data=nullptr,uint16_t length=0) {
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};s->bmRequestType=type;s->bRequest=code;s->wValue=value;s->wIndex=index;s->wLength=length;
  if(!(type&128) && length)memcpy(control->data_buffer+8,data,length);
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+length;control->callback=completedControl;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  uint32_t until=millis()+3000;while(!done && int32_t(until-millis())>0)events();
  if(!done || control->status!=USB_TRANSFER_STATUS_COMPLETED || control->actual_num_bytes!=8+length)return false;
  if(type&128)memcpy(data,control->data_buffer+8,length);
  return true;
}
bool submitIso(usb_transfer_t *t);
void completedIso(usb_transfer_t *t) {
  const unsigned index=t==input ? 0:t==output ? 1:2;pending[index]=false;
  const auto &p=t->isoc_packet_desc[0];
  if(t->status!=USB_TRANSFER_STATUS_COMPLETED || p.status!=USB_TRANSFER_STATUS_COMPLETED)++lost;
  else if(index==0) {
    if(p.actual_num_bytes>unsigned(t->num_bytes) || (p.actual_num_bytes && p.actual_num_bytes!=rate/1000*frame)){++bad;return;}
    received+=p.actual_num_bytes;if(p.actual_num_bytes%frame)++bad;
    for(unsigned i=0;i<p.actual_num_bytes;++i) {
      if(t->data_buffer[i]!=0 && t->data_buffer[i]!=0x5a)++bad;
      if(t->data_buffer[i]==0x5a)++filled;
    }
  } else if(index==1)sent+=p.actual_num_bytes;
  else {
    uint32_t value=0;memcpy(&value,t->data_buffer,4);
    if(p.actual_num_bytes!=4 || value!=uint32_t((uint64_t(rate)<<16)/1000))++bad;
    ++feedbacks;
  }
  if(streaming && !submitIso(t))++bad;
}
bool submitIso(usb_transfer_t *t) {
  const unsigned index=t==input ? 0:t==output ? 1:2;
  pending[index]=true;
  if(usb_host_transfer_submit(t)==ESP_OK)return true;
  pending[index]=false;return false;
}
bool pauseIso() {
  streaming=false;uint32_t until=millis()+3000;
  while((pending[0] || pending[1] || pending[2]) && int32_t(until-millis())>0)events();
  if(pending[0] || pending[1] || pending[2])return false;
  bool ok=true;
  for(unsigned i=0;i<2;++i)if(claimed[i]) {
    ok &= request(1,11,0,i+1);
    ok &= usb_host_interface_release(client,handle,i+1)==ESP_OK;claimed[i]=false;
  }
  selected=0;return ok;
}
bool startIso(unsigned alternate,unsigned sampleRate) {
  if(!pauseIso() || alternate<1 || alternate>4)return false;
  selected=alternate;rate=sampleRate;frame=2*choices[0][selected].bytes;
  uint8_t bytes[4];for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(rate>>(8*i));
  bool ok=protocol==2 ? request(0x21,1,0x0100,0x0100,bytes,4) : request(0x22,1,0x0100,choices[0][selected].endpoint,bytes,3);
  if(protocol==1)ok &= request(0x22,1,0x0100,choices[1][selected].endpoint,bytes,3);
  for(unsigned i=0;i<2 && ok;++i) {
    ok=usb_host_interface_claim(client,handle,i+1,selected)==ESP_OK;claimed[i]=ok;
    if(ok)ok=request(1,11,selected,i+1);
  }
  if(!ok)return false;
  usb_transfer_t *transfers[]={input,output,feedback};
  for(unsigned i=0;i<3;++i) {
    auto *t=transfers[i];t->device_handle=handle;t->callback=completedIso;
    t->bEndpointAddress=i==0 ? choices[1][selected].endpoint:i==1 ? choices[0][selected].endpoint:choices[0][selected].feedback;
    t->num_bytes=i==0 ? choices[1][selected].packet:i==1 ? rate/1000*frame:4;
    t->isoc_packet_desc[0].num_bytes=t->num_bytes;
    if(i==1)memset(t->data_buffer,0x5a,t->num_bytes);
  }
  received=sent=filled=feedbacks=bad=lost=0;streaming=true;
  ok=submitIso(input) && submitIso(output);
  if(protocol==2)ok &= submitIso(feedback);
  const uint32_t until=millis()+1200;while(ok && int32_t(until-millis())>0)events();
  streaming=false;uint32_t drain=millis()+3000;
  while((pending[0] || pending[1] || pending[2]) && int32_t(drain-millis())>0)events();
  return ok && !pending[0] && !pending[1] && !pending[2] && received>=50*frame && sent>=50*frame && filled && !bad && (protocol!=2 || feedbacks);
}
bool stop() {
  bool ok=!handle || pauseIso();if(!ok)return false;
  for(auto **p:{&control,&input,&output,&feedback})if(*p){ok &= usb_host_transfer_free(*p)==ESP_OK;*p=nullptr;}
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
    running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"iso-alternates",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t c={};c.max_num_event_msg=8;c.async.client_event_callback=event;
    ok &= usb_host_client_register(&c,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(64,0,&control)==ESP_OK && usb_host_transfer_alloc(400,1,&input)==ESP_OK && usb_host_transfer_alloc(400,1,&output)==ESP_OK && usb_host_transfer_alloc(4,1,&feedback)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !handle && int32_t(until-millis())>0)events();
    Serial.printf("HOST_DETAILS install=%d ok=%u handle=%u parsed=%u protocol=%u\r\n",int(installed),unsigned(ok),unsigned(bool(handle)),unsigned(parsed),protocol);
    for(unsigned i=0;i<2;++i)for(unsigned a=1;a<=4;++a){const auto &v=choices[i][a];Serial.printf("CHOICE itf=%u alt=%u ep=%u bytes=%u bits=%u packet=%u\r\n",i+1,a,unsigned(v.endpoint),unsigned(v.bytes),unsigned(v.bits),unsigned(v.packet));}
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && handle && parsed));
  } else if(s.startsWith("run ")) {
    unsigned alternate,sampleRate;sscanf(s.c_str(),"run %u %u",&alternate,&sampleRate);
    const bool ok=startIso(alternate,sampleRate);
    Serial.printf("ISO ok=%u rx=%u tx=%u filled=%u feedback=%u bad=%u lost=%u\r\n",unsigned(ok),received,sent,filled,feedbacks,bad,lost);
  } else if(s=="invalid_alt") {
    bool ok=true;
    for(unsigned i=0;i<2;++i){ok &= !request(1,11,255,i+1);uint8_t value=0;ok &= request(0x81,10,0,i+1,&value,1) && value==selected;}
    Serial.printf("ALTERNATES %u\r\n",unsigned(ok));
  } else if(s=="pause")Serial.printf("PAUSED %u\r\n",unsigned(pauseIso()));
  else if(s=="stop")Serial.println(!running.load() || stop() ? "STOPPED":"STOP_FAILED");
}
