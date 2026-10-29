#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#include <hal/usb_dwc_ll.h>
#include <esp_rom_sys.h>
usb_host_client_handle_t client=nullptr;usb_device_handle_t handle=nullptr;
usb_transfer_t *input=nullptr,*output=nullptr,*control=nullptr;
bool advertised=false,held=false,savedGlobal=false;uint32_t savedMask=0;
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
  auto *d=reinterpret_cast<const uint8_t *>(cfg); advertised=(d[7]&32)!=0;
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
// Probe-only controller ownership while idle. The pinned HCD has no wake
// event handler, so keep its IRQ gated, observe the actual wake status, then
// complete Host resume before restoring the exact previous interrupt policy.
bool request(uint8_t type,uint8_t code,uint16_t value,uint16_t bytes) {
  if(!handle || !done.load() || held)return false;
  auto *p=reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *p={};p->bmRequestType=type;p->bRequest=code;p->wValue=value;p->wLength=bytes;
  control->device_handle=handle;control->bEndpointAddress=0;control->num_bytes=8+bytes;control->callback=complete;
  done=false;if(usb_host_transfer_submit_control(client,control)!=ESP_OK){done=true;return false;}
  return wait() && control->status==USB_TRANSFER_STATUS_COMPLETED && control->actual_num_bytes==8+bytes;
}
bool suspendPort() {
  if(!handle || !done.load() || held)return false;
  auto *hw=USB_DWC_LL_GET_HW(0);savedGlobal=hw->gahbcfg_reg.glbllntrmsk;savedMask=hw->gintmsk_reg.val;
  usb_dwc_ll_gahbcfg_dis_global_intr(hw);held=true;
  usb_dwc_ll_gintmsk_dis_intrs(hw,USB_DWC_LL_INTR_CORE_WKUPINT);
  usb_dwc_ll_gintsts_clear_intrs(hw,USB_DWC_LL_INTR_CORE_WKUPINT);
  usb_dwc_ll_hprt_set_port_suspend(hw);delay(30);
  const bool suspended=usb_dwc_ll_hprt_get_port_suspend(hw);
  if(suspended) {usb_dwc_ll_set_stoppclk(hw,true);usb_dwc_ll_set_gatehclk(hw,true);esp_rom_delay_us(1);}
  return suspended;
}
bool resumePort() {
  if(!handle)return false;auto *hw=USB_DWC_LL_GET_HW(0);
  if(held || usb_dwc_ll_hprt_get_port_suspend(hw)) {
    usb_dwc_ll_set_gatehclk(hw,false);usb_dwc_ll_set_stoppclk(hw,false);esp_rom_delay_us(1);
    usb_dwc_ll_hprt_set_port_resume(hw);delay(30);
    usb_dwc_ll_hprt_clr_port_resume(hw);delay(20);
  }
  if(held) {
    usb_dwc_ll_gintsts_clear_intrs(hw,USB_DWC_LL_INTR_CORE_WKUPINT);
    hw->gintmsk_reg.val=savedMask;
    if(savedGlobal)usb_dwc_ll_gahbcfg_en_global_intr(hw);
    held=false;
  }
  const uint32_t frame=usb_dwc_ll_hfnum_get_frame_num(hw);delay(20);
  Serial.printf("HOST_DIAG frame=%lu previous=%lu port=%08lx sts=%08lx mask=%08lx irq=%u\r\n",static_cast<unsigned long>(usb_dwc_ll_hfnum_get_frame_num(hw)),static_cast<unsigned long>(frame),hw->hprt_reg.val,hw->gintsts_reg.val,hw->gintmsk_reg.val,unsigned(hw->gahbcfg_reg.glbllntrmsk));
  return !usb_dwc_ll_hprt_get_port_suspend(hw);
}
bool watchWake(unsigned ms) {
  if(!held)return false;const uint32_t until=millis()+ms;
  while(int32_t(until-millis())>0) {
    if(USB_DWC_LL_GET_HW(0)->gintsts_reg.val & USB_DWC_LL_INTR_CORE_WKUPINT)return true;
    esp_rom_delay_us(50);
  }
  return false;
}
bool stop() {
  if(handle && !resumePort())return false;
  if(!done.load() && !wait())return false;bool ok=true;
  for(auto **t:{&input,&output,&control})if(*t){ok &= usb_host_transfer_free(*t)==ESP_OK;*t=nullptr;}
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
    ok &= usb_host_transfer_alloc(64,0,&input)==ESP_OK && usb_host_transfer_alloc(64,0,&output)==ESP_OK && usb_host_transfer_alloc(72,0,&control)==ESP_OK;
    uint32_t until=millis()+10000;while(ok && !claimed && int32_t(until-millis())>0)events();
    Serial.printf("HOST_READY ok=%u capable=%u\r\n",unsigned(ok && claimed),unsigned(advertised));
  } else if(s=="suspend")Serial.printf("SUSPENDED %u\r\n",unsigned(suspendPort()));
  else if(s.startsWith("feature ")) {
    const unsigned value=s.substring(8).toInt();bool accepted=request(0,value ? 3:1,1,0);
    const bool valid=request(0x80,0,0,2);unsigned status=valid ? (control->data_buffer[8]>>1)&1 : 99;
    Serial.printf("FEATURE value=%u accepted=%u status=%u valid=%u\r\n",value,unsigned(accepted),status,unsigned(valid));
  } else if(s=="status") {
    const bool valid=request(0x80,0,0,2);
    Serial.printf("STATUS valid=%u enabled=%u\r\n",unsigned(valid),valid ? (control->data_buffer[8]>>1)&1 : 99);
  } else if(s=="wake" || s=="quiet") {
    Serial.println("WATCHING");
    const bool seen=watchWake(s=="wake" ? 1500:150);const bool resumed=resumePort();
    Serial.printf("WAKE seen=%u resumed=%u\r\n",unsigned(seen),unsigned(resumed));
  }
  else if(s=="resume")Serial.printf("RESUMED %u\r\n",unsigned(resumePort()));
  else if(s=="exchange")Serial.printf("EXCHANGED %u\r\n",unsigned(exchange()));
  else if(s=="stop")Serial.println(!client || stop() ? "STOPPED":"STOP_FAILED");
}
