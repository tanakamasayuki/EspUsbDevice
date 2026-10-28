#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#include <hal/usb_dwc_ll.h>
usb_host_client_handle_t client=nullptr;usb_device_handle_t handle=nullptr;
usb_transfer_t *input=nullptr,*output=nullptr;
std::atomic<bool> running{false},exited{true},allFree{false},done{true};
bool claimed=false;uint8_t in=0,out=0;
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
bool exchange() {
  if(!done.load())return false;input->device_handle=handle;input->bEndpointAddress=in;input->num_bytes=64;input->callback=complete;
  done=false;if(usb_host_transfer_submit(input)!=ESP_OK){done=true;return false;}
  bool ok=wait() && input->status==USB_TRANSFER_STATUS_COMPLETED && input->actual_num_bytes==64;
  for(unsigned j=0;ok && j<64;++j)ok &= input->data_buffer[j]==uint8_t(j*37+13);
  output->device_handle=handle;output->bEndpointAddress=out;output->num_bytes=64;output->callback=complete;
  for(unsigned j=0;j<64;++j)output->data_buffer[j]=uint8_t(j*17+7);
  done=false;if(usb_host_transfer_submit(output)!=ESP_OK){done=true;return false;}
  return wait() && ok && output->status==USB_TRANSFER_STATUS_COMPLETED && output->actual_num_bytes==64;
}
// Probe-only root-port control: SDK USB Host has no public suspend API.
// No transfer is submitted by the Host while the port is suspended.
bool suspendPort() {
  if(!handle || !done.load())return false;
  usb_dwc_ll_hprt_set_port_suspend(USB_DWC_LL_GET_HW(0));
  delay(30);
  return usb_dwc_ll_hprt_get_port_suspend(USB_DWC_LL_GET_HW(0));
}
bool resumePort() {
  if(!handle)return false;
  if(usb_dwc_ll_hprt_get_port_suspend(USB_DWC_LL_GET_HW(0))) {
    usb_dwc_ll_hprt_set_port_resume(USB_DWC_LL_GET_HW(0));
    delay(30); // At least 20 ms resume signalling, then 10 ms recovery.
    usb_dwc_ll_hprt_clr_port_resume(USB_DWC_LL_GET_HW(0));
    delay(20);
  }
  return !usb_dwc_ll_hprt_get_port_suspend(USB_DWC_LL_GET_HW(0));
}
bool stop() {
  if(handle && !resumePort())return false;
  if(!done.load() && !wait())return false;bool ok=true;
  for(auto **t:{&input,&output})if(*t){ok &= usb_host_transfer_free(*t)==ESP_OK;*t=nullptr;}
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
    ok &= usb_host_transfer_alloc(64,0,&input)==ESP_OK && usb_host_transfer_alloc(64,0,&output)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed && int32_t(until-millis())>0)events();
    Serial.printf("HOST_READY %u\r\n",unsigned(ok && claimed));
  } else if(s=="suspend")Serial.printf("SUSPENDED %u\r\n",unsigned(suspendPort()));
  else if(s=="resume")Serial.printf("RESUMED %u\r\n",unsigned(resumePort()));
  else if(s=="exchange")Serial.printf("EXCHANGED %u\r\n",unsigned(exchange()));
  else if(s=="stop")Serial.println(!client || stop() ? "STOPPED":"STOP_FAILED");
}
