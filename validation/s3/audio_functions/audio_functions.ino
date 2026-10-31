#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#include "../common/probe_link.h"
// 24 kHz stereo 16-bit = 96 B/ms; HS splits it into 8 microframe packets per URB.
constexpr unsigned PACKETS=PROBE_BULK_MPS==512 ? 8:1,PAYLOAD=96/PACKETS,MPS=PAYLOAD+4;
usb_host_client_handle_t client=nullptr;
usb_device_handle_t handle=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false};
struct AudioPipe {uint8_t itf=255,ep=0;uint16_t packet=0;} streams[4][2];
usb_transfer_t *control=nullptr,*input=nullptr,*output=nullptr;
bool parsed=false,done=true,streaming=false,pending[2]={},claimed[2]={};
unsigned active=0,received=0,sent=0,filled=0,bad=0,lost=0;
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
  unsigned functions=0,itf=0,alt=0;bool as=false;usb_device_info_t info;
  parsed=usb_host_device_info(handle,&info)==ESP_OK && (info.speed==USB_SPEED_HIGH)==(PACKETS==8);
  const auto *d=reinterpret_cast<const uint8_t *>(cfg);
  for(size_t n=9;n+2<=cfg->wTotalLength && d[n]>=2 && d[n]<=cfg->wTotalLength-n;n+=d[n]) {
    const auto *p=d+n;
    if(p[1]==4 && p[0]==9) {
      itf=p[2];alt=p[3];as=p[5]==1 && p[6]==2;
      if(p[5]==1 && p[6]==1)++functions;
    }
    if(!as || !alt || !functions || functions>4)continue;
    if(p[1]==0x24 && p[2]==2)parsed &= p[0]==11 && p[4]==2 && p[5]==2 && p[6]==16 && p[7]==1;
    if(p[1]==5 && p[0]>=7) {
      const unsigned dir=(p[2]&128) ? 1:0;auto &s=streams[functions-1][dir];
      s.itf=itf;s.ep=p[2];s.packet=p[4]|unsigned(p[5])<<8;
      parsed &= s.packet==MPS && (p[3]&3)==1;
    }
  }
  parsed &= functions==4;
  for(unsigned f=0;f<4;++f)for(unsigned dir=0;dir<2;++dir)parsed &= streams[f][dir].ep!=0;
}
void completedControl(usb_transfer_t *){done=true;}
bool alternate(uint8_t itf,uint8_t alt) {
  auto *s=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);*s={};s->bmRequestType=1;s->bRequest=11;s->wValue=alt;s->wIndex=itf;
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8;control->callback=completedControl;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  const uint32_t until=millis()+3000;while(!done && int32_t(until-millis())>0)events();
  return done && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8;
}
bool submit(usb_transfer_t *t) {
  const unsigned i=t==input ? 0:1;pending[i]=true;
  if(usb_host_transfer_submit(t)==ESP_OK)return true;
  pending[i]=false;return false;
}
void completed(usb_transfer_t *t) {
  const unsigned i=t==input ? 0:1;pending[i]=false;
  if(t->status!=USB_TRANSFER_STATUS_COMPLETED)++lost;
  else for(unsigned k=0;k<PACKETS;++k) {
    const auto &p=t->isoc_packet_desc[k];const uint8_t *d=t->data_buffer+k*p.num_bytes;
    if(p.status!=USB_TRANSFER_STATUS_COMPLETED)++lost;
    else if(!i) {
      if(p.actual_num_bytes && p.actual_num_bytes!=PAYLOAD)++bad;
      received+=p.actual_num_bytes;
      for(unsigned n=0;n<p.actual_num_bytes;++n){if(d[n]!=0 && d[n]!=0x50+active)++bad;if(d[n]==0x50+active)++filled;}
    } else sent+=p.actual_num_bytes;
  }
  if(streaming && !submit(t))++bad;
}
bool pauseStreams() {
  streaming=false;const uint32_t until=millis()+3000;
  while((pending[0] || pending[1]) && int32_t(until-millis())>0)events();
  if(pending[0] || pending[1])return false;
  bool ok=true;
  for(unsigned d=0;d<2;++d)if(claimed[d]) {
    ok &= alternate(streams[active][d].itf,0);
    ok &= usb_host_interface_release(client,handle,streams[active][d].itf)==ESP_OK;claimed[d]=false;
  }
  return ok;
}
bool exchange(unsigned f) {
  if(f>=4 || !pauseStreams())return false;
  active=f;bool ok=true;
  for(unsigned d=0;d<2 && ok;++d){ok=usb_host_interface_claim(client,handle,streams[f][d].itf,1)==ESP_OK;claimed[d]=ok;if(ok)ok=alternate(streams[f][d].itf,1);}
  received=sent=filled=bad=lost=0;
  for(unsigned i=0;i<2;++i) {
    auto *t=i ? output:input;t->device_handle=handle;t->callback=completed;t->bEndpointAddress=streams[f][i ? 0:1].ep;
    for(unsigned k=0;k<PACKETS;++k)t->isoc_packet_desc[k].num_bytes=i ? PAYLOAD:MPS;
    t->num_bytes=PACKETS*(i ? PAYLOAD:MPS);
    if(i)memset(t->data_buffer,0x50+f,t->num_bytes);
  }
  streaming=true;ok &= submit(input) && submit(output);
  const uint32_t until=millis()+800;while(ok && int32_t(until-millis())>0)events();
  ok &= pauseStreams();return ok && received>=4096 && sent>=4096 && filled && !bad;
}
bool stop() {
  bool ok=!handle || pauseStreams();if(!ok)return false;
  for(auto **p:{&control,&input,&output})if(*p){ok &= usb_host_transfer_free(*p)==ESP_OK;*p=nullptr;}
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
    for(auto &f:streams)for(auto &v:f)v=AudioPipe{};
    usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
    cfg.fifo_settings_custom.nptx_fifo_lines=32;cfg.fifo_settings_custom.ptx_fifo_lines=64;cfg.fifo_settings_custom.rx_fifo_lines=64;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *){return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    bool ok=usb_host_install(&cfg)==ESP_OK;
    if(!ok){Serial.println("HOST_READY 0");return;}
    running=true;exited=false;allFree=false;
    ok &= xTaskCreate(daemon,"four-audio",4096,nullptr,5,nullptr)==pdPASS;
    usb_host_client_config_t c={};c.max_num_event_msg=8;c.async.client_event_callback=event;
    ok &= usb_host_client_register(&c,&client)==ESP_OK;
    ok &= usb_host_transfer_alloc(64,0,&control)==ESP_OK && usb_host_transfer_alloc(256,PACKETS,&input)==ESP_OK && usb_host_transfer_alloc(256,PACKETS,&output)==ESP_OK;
    const uint32_t until=millis()+10000;while(ok && !handle && int32_t(until-millis())>0)events();
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && handle && parsed));
  } else if(s.startsWith("run ")) {
    unsigned f=s.substring(4).toInt();bool ok=exchange(f);
    Serial.printf("FUNCTION index=%u ok=%u rx=%u tx=%u filled=%u bad=%u lost=%u\r\n",f,unsigned(ok),received,sent,filled,bad,lost);
  } else if(s=="stop")Serial.println(!running.load() || stop() ? "STOPPED":"STOP_FAILED");
}
