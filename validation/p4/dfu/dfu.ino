#include <Arduino.h>
#include <usb/usb_host.h>
#include <atomic>
#include <EspUsbDevice.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
EspUsbDevice device;
EspUsbDeviceDfu dfu(device), runtime(device,EspUsbDeviceDfuMode::Runtime);
std::atomic<unsigned> blocks{0}, manifests{0}, aborts{0}, bad{0}, detached{0};
uint8_t storage[2048+2];size_t stored=0;bool installed=false;
usb_host_client_handle_t client = nullptr;
usb_device_handle_t handle = nullptr;
usb_transfer_t *control = nullptr;
std::atomic<bool> running{false}, exited{true}, allFree{false}, done{true};
bool claimed[3] = {};
uint16_t transferBytes = 0;
void daemon(void *) {
  while (running.load()) {
    uint32_t flags = 0; usb_host_lib_handle_events(pdMS_TO_TICKS(10), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) allFree = true;
  }
  exited = true; vTaskDelete(nullptr);
}
void event(const usb_host_client_event_msg_t *e, void *) {
  if (e->event != USB_HOST_CLIENT_EVENT_NEW_DEV || handle) return;
  esp_err_t error = usb_host_device_open(client, e->new_dev.address, &handle); Serial.printf("DEVICE_OPEN %d\r\n",int(error));
  if (error != ESP_OK) return;
  for(unsigned i=0;i<2;++i) claimed[i]=usb_host_interface_claim(client,handle,i,0)==ESP_OK;

}
void events(unsigned ms) {
  const uint32_t until = millis() + ms;
  do { usb_host_client_handle_events(client, pdMS_TO_TICKS(1)); } while (int32_t(until - millis()) > 0);
}
usb_transfer_status_t result = USB_TRANSFER_STATUS_ERROR;
uint32_t actual = 0;
bool request(uint8_t type, uint8_t code, uint16_t value, uint16_t n, uint16_t index = 0) {
  auto *s = reinterpret_cast<usb_setup_packet_t *>(control->data_buffer);
  *s = {}; s->bmRequestType = type; s->bRequest = code; s->wValue = value; s->wIndex = index; s->wLength = n;
  if (!(type & 0x80)) for (unsigned i = 0; i < n; ++i) control->data_buffer[i + 8] = uint8_t((value * 17u + i) % 251);
  control->device_handle = handle; control->bEndpointAddress = 0; control->num_bytes = 8 + n;
  control->callback = [](usb_transfer_t *) { done = true; }; done = false;
  if (esp_cache_msync(control->data_buffer, control->data_buffer_size,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) != ESP_OK) { done=true;return false; }
  if (usb_host_transfer_submit_control(client, control) != ESP_OK) { done = true; return false; }
  const uint32_t until = millis() + 3000;
  while (!done.load() && int32_t(until - millis()) > 0) events(1);
  if(!done.load()) return false;
  result = control->status; actual = control->actual_num_bytes;
  return done.load() && result == USB_TRANSFER_STATUS_COMPLETED && actual == 8u + n;
}
bool stall(uint8_t type, uint8_t code, uint16_t value, uint16_t n, uint16_t index = 0) {
  request(type, code, value, n, index); return done.load() && result == USB_TRANSFER_STATUS_STALL;
}
bool state(uint8_t expected, uint16_t index = 0) { return request(0xa1, 5, 0, 1, index) && control->data_buffer[8] == expected; }
bool status(uint8_t expectedState, uint8_t expectedStatus = 0, uint32_t expectedPoll = 0, uint16_t index = 0) {
  if (!request(0xa1, 3, 0, 6, index)) return false;
  const auto *d = control->data_buffer + 8;
  const uint32_t poll = d[1] | uint32_t(d[2]) << 8 | uint32_t(d[3]) << 16;
  bool ok = d[0] == expectedStatus && d[4] == expectedState && !d[5] && poll == expectedPoll;
  if (!ok) Serial.printf("STATUS_BAD state=%u status=%u poll=%u expected=%u/%u/%u\r\n",d[4],d[0],unsigned(poll),expectedState,expectedStatus,unsigned(expectedPoll));
  delay(poll); return ok;
}
bool download(uint16_t block, uint16_t n, uint16_t index = 0) {
  return request(0x21, 1, block, n, index) && state(3,index) && status(4,0,2,index) && status(5,0,0,index);
}
bool manifest(uint16_t block, uint16_t index = 0) {
  return request(0x21,1,block,0,index) && state(6,index) && status(7,0,3,index) && status(2,0,0,index);
}
bool clear(uint8_t expected = 15) { return status(10,expected) && request(0x21,4,0,0) && state(2); }
bool stop() {
  if (!installed) return device.end()==ESP_OK;
  if (!done.load()) return false;
  bool ok = true;
  if (control) { ok &= usb_host_transfer_free(control) == ESP_OK; control = nullptr; }
  for (unsigned i = 0; i < 3; ++i) { if (claimed[i]) ok &= usb_host_interface_release(client,handle,i) == ESP_OK; claimed[i] = false; }
  if (handle) { ok &= usb_host_device_close(client,handle) == ESP_OK; handle = nullptr; }
  if (client) { ok &= usb_host_client_deregister(client) == ESP_OK; client = nullptr; }
  const uint32_t until = millis() + 3000;
  while (!allFree.load() && int32_t(until - millis()) > 0) delay(1);
  running = false; while (!exited.load()) delay(1);
  ok &= allFree.load();
  const esp_err_t error=usb_host_uninstall();installed=error!=ESP_OK;ok &= !installed;
  ok &= device.end()==ESP_OK && !dfu.transferCapacity() && storage[0]==0xc3 && storage[sizeof(storage)-1]==0xc3;
  return ok && heap_caps_check_integrity_all(true);
}
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  EspUsbDfuBackend backend;
  backend.download=[](uint16_t block,const uint8_t *data,size_t n) {
    ++blocks;
    if(stored+n>sizeof(storage)-2) return EspUsbDfuStatus::Write;
    for(size_t i=0;i<n;++i) if(data[i]!=uint8_t((block*17u+i)%251)) ++bad;
    memcpy(storage+1+stored,data,n);stored+=n;return EspUsbDfuStatus::Ok;
  };
  backend.manifest=[]() {++manifests;return EspUsbDfuStatus::Ok;};
  backend.abort=[]() {++aborts;};
  dfu.setBackend(backend);runtime.onDetach([](){++detached;});
}
void loop() {
  if(!Serial.available()) {delay(1);return;}
  String command=Serial.readStringUntil('\n');command.trim();
  if(command.startsWith("start ")) {
    const bool hs=command.substring(6)=="hs";
    blocks=0;manifests=0;aborts=0;bad=0;detached=0;stored=0;memset(storage,0xc3,sizeof(storage));
    EspUsbDfuConfig c;c.transferBytes=1024;c.downloadPollMs=2;c.manifestPollMs=3;c.manifestationTolerant=true;
    bool ok=dfu.configure(c)==ESP_OK;
    c.detachTimeoutMs=500;ok &= runtime.configure(c)==ESP_OK;
    usb_host_config_t cfg={};cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;cfg.peripheral_map=hs?2u:1u;
#if CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK
    cfg.enum_filter_cb=[](const usb_device_desc_t *d,uint8_t *) {return d->idVendor==0x303a && d->idProduct==0x4000;};
#endif
    installed=usb_host_install(&cfg)==ESP_OK;ok &= installed;
    if(installed) {
      running=true;exited=false;allFree=false;
      ok &= xTaskCreate(daemon,"dfu-host",4096,nullptr,5,nullptr)==pdPASS;
      usb_host_client_config_t cc={};cc.max_num_event_msg=8;cc.async.client_event_callback=event;
      ok &= usb_host_client_register(&cc,&client)==ESP_OK;
      ok &= usb_host_transfer_alloc(8+1024,0,&control)==ESP_OK;
    }
    EspUsbDeviceConfig dc;dc.product="v3 P4 DFU";dc.controller=hs?EspUsbController::HighSpeed:EspUsbController::FullSpeed;
    ok &= device.begin(dc);
    const uint32_t until=millis()+12000;
    while(ok && !claimed[1] && int32_t(until-millis())>0) events(1);
    ok &= claimed[0] && claimed[1] && device.ready();
    if(ok) ok &= state(2) && state(0,1);
    usb_device_info_t info={};if(handle) ok &= usb_host_device_info(handle,&info)==ESP_OK;
    Serial.printf("START ok=%u controller=%s speed=%u capacity=%u error=%u\r\n",unsigned(ok),hs?"hs":"fs",unsigned(info.speed),unsigned(dfu.transferCapacity()),unsigned(device.lastError()));
  } else if(command=="exercise") {
    bool ok=true;uint16_t block=0;
    for(unsigned n:{1u,63u,64u,65u,127u,128u,129u,1024u}) {ok &= download(block++,n);if(!ok) break;}
    ok &= manifest(block);
    ok &= download(0,64) && request(0x21,6,0,0) && state(2);
    ok &= status(0,0,0,1) && request(0x21,0,500,0,1) && state(1,1);
    delay(550);ok &= state(0,1);
    ok &= blocks==9 && manifests==1 && aborts>=1 && bad==0 && detached==1 && stored==1665;
    size_t at=0;block=0;
    for(unsigned n:{1u,63u,64u,65u,127u,128u,129u,1024u,64u}) {
      if(block==8) block=0;
      for(size_t i=0;i<n;++i) ok &= storage[1+at++]==uint8_t((block*17u+i)%251);
      ++block;
    }
    for(size_t i=stored+1;i<sizeof(storage);++i) ok &= storage[i]==0xc3;
    ok &= heap_caps_check_integrity_all(true);
    Serial.printf("DFU ok=%u blocks=%u bytes=%u manifests=%u aborted=%u bad=%u detach=%u\r\n",unsigned(ok),blocks.load(),unsigned(stored),manifests.load(),unsigned(aborts>=1),bad.load(),detached.load());
  } else if(command=="stop") Serial.println(stop()?"STOPPED":"STOP_FAILED");
}
