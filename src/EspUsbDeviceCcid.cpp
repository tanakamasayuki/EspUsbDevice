// Independent, per-Function CCID transport. The temporary DWC2 ABI remains internal.
#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"
#include "internal/EspUsbDeviceCore.h"
#include "internal/EspUsbMemory.h"
#include "internal/EspUsbTinyUsbRuntime.h"
#include "internal/EspUsbDeviceEndpoint.h"
#include "internal/EspUsbDeviceBus.h"
#include <algorithm>
#include <string.h>
#include <utility>
namespace {
portMUX_TYPE ccidLock = portMUX_INITIALIZER_UNLOCKED;
extern const espusb::internal::UsbClassDriver ccidDriver;
static constexpr uint8_t USB_DESC_INTERFACE = 0x04;
static constexpr uint8_t USB_DESC_ENDPOINT = 0x05;
// CCID functional descriptor. Shares its number with the HID descriptor, which
// is why a parser has to look at the interface class to tell them apart.
static constexpr uint8_t USB_DESC_CCID = 0x21;
static constexpr uint8_t USB_CLASS_SMART_CARD = 0x0b;
static constexpr uint8_t USB_ENDPOINT_ATTR_BULK = 0x02;
static constexpr uint8_t USB_ENDPOINT_ATTR_INTERRUPT = 0x03;

// PC_to_RDR message types (CCID 1.1 section 6.1).
static constexpr uint8_t CCID_PC_TO_RDR_SET_PARAMETERS = 0x61;
static constexpr uint8_t CCID_PC_TO_RDR_ICC_POWER_ON = 0x62;
static constexpr uint8_t CCID_PC_TO_RDR_ICC_POWER_OFF = 0x63;
static constexpr uint8_t CCID_PC_TO_RDR_GET_SLOT_STATUS = 0x65;
static constexpr uint8_t CCID_PC_TO_RDR_ICC_CLOCK = 0x6a;
static constexpr uint8_t CCID_PC_TO_RDR_T0_APDU = 0x6b;
static constexpr uint8_t CCID_PC_TO_RDR_GET_PARAMETERS = 0x6c;
static constexpr uint8_t CCID_PC_TO_RDR_RESET_PARAMETERS = 0x6d;
static constexpr uint8_t CCID_PC_TO_RDR_ESCAPE = 0x6e;
static constexpr uint8_t CCID_PC_TO_RDR_XFR_BLOCK = 0x6f;
static constexpr uint8_t CCID_PC_TO_RDR_ABORT = 0x72;

// RDR_to_PC message types (CCID 1.1 section 6.2).
static constexpr uint8_t CCID_RDR_TO_PC_DATA_BLOCK = 0x80;
static constexpr uint8_t CCID_RDR_TO_PC_SLOT_STATUS = 0x81;
static constexpr uint8_t CCID_RDR_TO_PC_PARAMETERS = 0x82;
static constexpr uint8_t CCID_RDR_TO_PC_ESCAPE = 0x83;
static constexpr uint8_t CCID_RDR_TO_PC_NOTIFY_SLOT_CHANGE = 0x50;

// bmCommandStatus, the top two bits of bStatus.
static constexpr uint8_t CCID_COMMAND_OK = 0;
static constexpr uint8_t CCID_COMMAND_FAILED = 1;

// CCID class requests (CCID 1.1 section 5.3).
static constexpr uint8_t CCID_REQUEST_ABORT = 0x01;

static constexpr size_t CCID_HEADER_SIZE = 10;
static constexpr uint16_t CCID_BULK_ENDPOINT_SIZE = 64;
static constexpr uint16_t CCID_INTERRUPT_ENDPOINT_SIZE = 8;
static constexpr uint8_t CCID_INTERRUPT_INTERVAL_MS = 16;
// T=1 parameters reported by GetParameters / SetParameters / ResetParameters.
// Fixed: the slot is emulated, so there is no real convention to negotiate.
static const uint8_t CCID_T1_PARAMETERS[7] = {
    0x11, // bmFindexDindex: Fi = 372, Di = 1
    0x10, // bmTCCKST1: T=1, direct convention, CRC/LRC per checksum bit 0
    0x00, // bGuardTimeT1
    0x4d, // bmWaitingIntegersT1: BWI = 4, CWI = 13
    0x00, // bClockStop: stopping the clock is not allowed
    0xfe, // bIFSC
    0x00, // bNadValue
};

static uint8_t ccidResponseType(uint8_t request) {
  if(request==CCID_PC_TO_RDR_XFR_BLOCK || request==CCID_PC_TO_RDR_ICC_POWER_ON)return CCID_RDR_TO_PC_DATA_BLOCK;
  if(request==CCID_PC_TO_RDR_ESCAPE)return CCID_RDR_TO_PC_ESCAPE;
  if(request==CCID_PC_TO_RDR_GET_PARAMETERS || request==CCID_PC_TO_RDR_SET_PARAMETERS || request==CCID_PC_TO_RDR_RESET_PARAMETERS)return CCID_RDR_TO_PC_PARAMETERS;
  return CCID_RDR_TO_PC_SLOT_STATUS;
}

static uint32_t ccidRead32(const uint8_t *src)
{
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8) |
         (static_cast<uint32_t>(src[2]) << 16) |
         (static_cast<uint32_t>(src[3]) << 24);
}

static void ccidWrite16(uint8_t *dst, uint16_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

static void ccidWrite32(uint8_t *dst, uint32_t value)
{
  dst[0] = static_cast<uint8_t>(value & 0xff);
  dst[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  dst[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  dst[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

}

struct EspUsbCcidState {
  static EspUsbCcidState *first;
  EspUsbCcidState *next=nullptr;
  EspUsbDeviceCcid &owner;
  uint8_t *packet=nullptr,*message=nullptr,*notify=nullptr;
  uint8_t port=0,interface=255,in=0,out=0,interrupt=0;
  uint16_t packetCapacity=64,packetSize=0;
  uint64_t received=0;
  size_t stored=0;
  bool txBusy=false,zlp=false,fenceQueued=false;
  uint8_t draining=0,resetPid=0; // bulk exchange = bits 0/1, slot notification = bit 2
  std::atomic<bool> stopping{false};
  uint32_t epoch=0,fenceEpoch=0;
  espusb::internal::TinyUsbRuntimeJob job{pump,this};
  explicit EspUsbCcidState(EspUsbDeviceCcid &o):owner(o) {}
  ~EspUsbCcidState() {
    espusb::internal::removeTinyUsbRuntimeJob(job);
    for(auto *p:{packet,message,notify,owner.response_})espusb::internal::freeUsbMemory(p);
    owner.response_=nullptr;
  }
  void receive() {
    if(!stopping && !(draining&3) && !txBusy && out && !espusb_device_edpt_stalled(port,in))espusb_device_edpt_xfer(port,out,packet,packetSize,false);
  }
  bool send(size_t bytes) {
    if(stopping || (draining&3) || !in || txBusy || bytes>owner.config_.messageBytes || !espusb_device_edpt_claim(port,in))return false;
    txBusy=true;zlp=bytes%packetSize==0;
    if(espusb_device_edpt_xfer(port,in,owner.response_,uint16_t(bytes),false))return true;
    txBusy=zlp=false;espusb_device_edpt_release(port,in);return false;
  }
  static void finishDrain(void *context) {
    auto *s=static_cast<EspUsbCcidState *>(context);s->fenceQueued=false;
    if(s->stopping || !s->draining)return;
    if(s->epoch!=s->fenceEpoch){espusb::internal::postTinyUsbRuntimeJob(s->job);return;}
    const uint8_t retired=s->draining;
    const uint8_t endpoints[]={s->in,s->out,s->interrupt};
    for(unsigned i=0;i<3;++i)if((s->resetPid&(1u<<i)) && endpoints[i])
      espusb_device_edpt_clear_stall(s->port,endpoints[i]);
    s->draining=s->resetPid=0;
    if(retired&3){s->received=s->stored=0;s->txBusy=s->zlp=false;}
    s->receive();
    if(retired&4)s->owner.notifySlotState();
    else if(s->owner.notifyPending_.load())espusb::internal::postTinyUsbRuntimeJob(s->job);
  }
  bool retire(uint8_t mask,uint8_t resetDataPid) {
    // A CCID command/response spans both bulk directions. Discard the whole
    // unfinished exchange, preserving the opposite endpoint's PID and HALT.
    ++epoch;draining|=mask;resetPid|=resetDataPid;
    const uint8_t endpoints[]={in,out,interrupt};
    for(unsigned i=0;i<3;++i)if((mask&(1u<<i)) && endpoints[i] &&
        !espusb_device_edpt_abort(port,endpoints[i]))return false;
    if(mask&3)owner.aborted_=false;
    return espusb::internal::postTinyUsbRuntimeJob(job);
  }

  static void pump(void *context) {
    auto *s=static_cast<EspUsbCcidState *>(context);
    if(s->stopping)return;
    if(s->draining) {
      if(!s->fenceQueued){s->fenceEpoch=s->epoch;s->fenceQueued=espusb_device_try_defer(finishDrain,s);if(!s->fenceQueued)espusb::internal::postTinyUsbRuntimeJob(s->job);}
      return;
    }
    if(s->interrupt && s->owner.notifyPending_.load() && espusb_device_edpt_claim(s->port,s->interrupt)) {
      portENTER_CRITICAL(&ccidLock);
      s->owner.notifyPending_=false;
      s->notify[0]=0x50;s->notify[1]=uint8_t((s->owner.cardPresent_ ? 1:0)|2);
      portEXIT_CRITICAL(&ccidLock);
      if(!espusb_device_edpt_xfer(s->port,s->interrupt,s->notify,2,false)){s->owner.notifyPending_=true;espusb_device_edpt_release(s->port,s->interrupt);}
    }
  }
  static void init() {}
  static void resetAll(uint8_t) {
    for(auto *s=first;s;s=s->next) {
      ++s->epoch;s->in=s->out=s->interrupt=0;s->interface=255;
      s->received=s->stored=0;s->txBusy=s->zlp=false;s->draining=s->fenceQueued ? 7:0;s->resetPid=0;
    }
  }
  static bool deinit(){resetAll(0);return true;}
  static uint16_t open(uint8_t port,const EspUsbInterfaceDescriptor *itf,uint16_t bytes) {
    if(itf->bLength!=9 || itf->bInterfaceClass!=11 || itf->bInterfaceSubClass || itf->bInterfaceProtocol)return 0;
    auto *s=first;while(s && s->owner.interfaceNumber_!=itf->bInterfaceNumber)s=s->next;if(!s)return 0;
    const unsigned count=s->owner.config_.removable ? 3:2,needed=63+7*count;
    if(bytes<needed || itf->bNumEndpoints!=count)return 0;
    const auto *d=reinterpret_cast<const uint8_t *>(itf);
    if(d[9]!=54 || d[10]!=0x21 || ccidRead32(d+9+44)!=s->owner.config_.messageBytes)return 0;
    s->port=port;s->interface=itf->bInterfaceNumber;
    for(unsigned n=0;n<count;++n) {
      const auto *ep=reinterpret_cast<const EspUsbEndpointDescriptor *>(d+63+7*n);
      if(ep->bLength!=7 || ep->bDescriptorType!=5)return 0;
      const uint16_t mps=espusb_endpoint_packet_size(ep);
      if(espusb_endpoint_type(ep)==ESPUSB_XFER_BULK && mps<=s->packetCapacity) {
        if(ep->bEndpointAddress&128){if(s->in)return 0;s->in=ep->bEndpointAddress;}
        else {if(s->out)return 0;s->out=ep->bEndpointAddress;s->packetSize=mps;}
      } else if(espusb_endpoint_type(ep)==ESPUSB_XFER_INTERRUPT && (ep->bEndpointAddress&128) && mps<=8 && s->notify) {
        if(s->interrupt)return 0;s->interrupt=ep->bEndpointAddress;
      } else return 0;
      if(!espusb_device_edpt_open(port,ep))return 0;
    }
    if(!s->in || !s->out)return 0;s->receive();return needed;
  }
  static bool control(uint8_t port,uint8_t stage,const EspUsbControlRequest *r) {
    const bool feature=r->bmRequestType==2 && (r->bRequest==1 || r->bRequest==3) && !r->wValue && !r->wLength;
    auto *s=first;
    while(s && (feature ? (s->in!=r->wIndex && s->out!=r->wIndex && s->interrupt!=r->wIndex):s->interface!=r->wIndex))s=s->next;
    if(!s)return false;
    if(feature) {
      if(stage!=ESPUSB_CONTROL_STAGE_SETUP)return true;
      const uint8_t endpoint=r->wIndex==s->interrupt ? 4:r->wIndex==s->in ? 1:2;
      if(r->bRequest==3)s->resetPid &= uint8_t(~endpoint);
      return s->retire(endpoint==4 ? 4:3,r->bRequest==1 ? endpoint:0) || espusb::internal::rejectUsbControl();
    }
    if(r->bmRequestType==1 && r->bRequest==11 && !r->wValue && !r->wLength) {
      if(stage!=ESPUSB_CONTROL_STAGE_SETUP)return true;
      if(!s->retire(7,7))return espusb::internal::rejectUsbControl();
      return espusb_device_control_status(port,r);
    }
    if(r->bmRequestType!=0x21 || r->bRequest!=1 || r->wLength || uint8_t(r->wValue))return false;
    if(stage==ESPUSB_CONTROL_STAGE_SETUP)return espusb_device_control_status(port,r);
    if(stage==ESPUSB_CONTROL_STAGE_ACK)s->owner.handleAbort(uint8_t(r->wValue),uint8_t(r->wValue>>8));
    return true;
  }
  static bool complete(uint8_t,uint8_t ep,EspUsbControllerResult result,uint32_t bytes) {
    auto *s=first;while(s && s->in!=ep && s->out!=ep && s->interrupt!=ep)s=s->next;if(!s)return false;
    if(s->stopping || (s->draining&(ep==s->interrupt ? 4:3)))return true;
    if(ep==s->interrupt){s->owner.handleNotifyComplete();return true;}
    if(ep==s->in) {
      if(result==ESPUSB_TRANSFER_SUCCESS && s->zlp){s->zlp=false;espusb_device_edpt_xfer(s->port,s->in,nullptr,0,false);return true;}
      s->txBusy=s->zlp=false;s->receive();return true;
    }
    if(result!=ESPUSB_TRANSFER_SUCCESS || bytes>s->packetSize){s->received=s->stored=0;s->receive();return true;}
    s->received+=bytes;
    const size_t copy=std::min(size_t(bytes),s->owner.config_.messageBytes-s->stored);
    memcpy(s->message+s->stored,s->packet,copy);s->stored+=copy;
    if(s->stored>=10) {
      const uint64_t wanted=uint64_t(ccidRead32(s->message+1))+10;
      if(s->received>=wanted || bytes<s->packetSize) {
        if(s->received!=wanted || wanted>s->owner.config_.messageBytes)s->owner.handleOverrun(s->message[0],s->message[5],s->message[6]);
        else s->owner.handleMessage(s->message,size_t(wanted));
        s->received=s->stored=0;
      }
    } else if(bytes<s->packetSize)s->received=s->stored=0;
    s->receive();return true;
  }
};
EspUsbCcidState *EspUsbCcidState::first=nullptr;
namespace {
const espusb::internal::UsbClassDriver ccidDriver={.init=EspUsbCcidState::init,.deinit=EspUsbCcidState::deinit,
.reset=EspUsbCcidState::resetAll,.open=EspUsbCcidState::open,.control_xfer_cb=EspUsbCcidState::control,.xfer_cb=EspUsbCcidState::complete,.sof=nullptr};
}
EspUsbDeviceCcid::EspUsbDeviceCcid(EspUsbDevice &d):EspUsbDeviceClass(d) {}
EspUsbDeviceCcid::~EspUsbDeviceCcid(){end();}
esp_err_t EspUsbDeviceCcid::configure(const EspUsbCcidConfig &c) {
  if(state_)return ESP_ERR_INVALID_STATE;
  if(c.messageBytes<64 || c.messageBytes>UINT16_MAX)return ESP_ERR_INVALID_SIZE;
  config_=c;return ESP_OK;
}
size_t EspUsbDeviceCcid::messageCapacity() const {
  portENTER_CRITICAL(&ccidLock);size_t n=state_ ? config_.messageBytes:0;portEXIT_CRITICAL(&ccidLock);return n;
}
bool EspUsbDeviceCcid::begin() {
  if(state_ || !registered_){startError_=ESP_ERR_INVALID_STATE;return false;}
  startError_=configure(config_);if(startError_!=ESP_OK)return false;
  if(!config_.removable && !cardPresent_){startError_=ESP_ERR_INVALID_STATE;return false;}
  uint16_t mps=64;
#if defined(CONFIG_IDF_TARGET_ESP32P4) || defined(CONFIG_IDF_TARGET_ESP32S31)
  if(device_.config().controller!=EspUsbController::FullSpeed)mps=512;
#endif
  if(config_.messageBytes<mps){startError_=ESP_ERR_INVALID_SIZE;return false;}
  auto *s=espusb::internal::newUsbObject<EspUsbCcidState>(*this);
  if(s) {
    s->packetCapacity=mps;
    s->packet=static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(mps,64,true));
    s->message=static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(config_.messageBytes));
    response_=static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(config_.messageBytes,64,true));
    if(config_.removable)s->notify=static_cast<uint8_t *>(espusb::internal::allocateUsbMemory(8,64,true));
  }
  if(!s || !s->packet || !s->message || !response_ || (config_.removable && !s->notify) ||
      (!EspUsbCcidState::first && !espUsbDeviceRegisterAppDrivers(&ccidDriver,1))) {
    espusb::internal::deleteUsbObject(s);startError_=ESP_ERR_NO_MEM;return false;
  }
  portENTER_CRITICAL(&ccidLock);s->next=EspUsbCcidState::first;EspUsbCcidState::first=s;state_=s;portEXIT_CRITICAL(&ccidLock);
  cardPowered_=false;aborted_=false;startError_=ESP_OK;return true;
}
void EspUsbDeviceCcid::prepareToStop(){portENTER_CRITICAL(&ccidLock);if(state_)state_->stopping=true;portEXIT_CRITICAL(&ccidLock);}
void EspUsbDeviceCcid::end() {
  if(state_ && device_.tinyusbStarted_){device_.end();return;}
  portENTER_CRITICAL(&ccidLock);
  auto *s=state_;state_=nullptr;auto **link=&EspUsbCcidState::first;while(*link && *link!=s)link=&(*link)->next;if(*link)*link=s->next;
  bool empty=!EspUsbCcidState::first;portEXIT_CRITICAL(&ccidLock);
  espusb::internal::deleteUsbObject(s);if(empty)espUsbDeviceUnregisterAppDrivers(&ccidDriver,1);
}


uint16_t EspUsbDeviceCcid::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber,
                                                   uint8_t endpointNumber, uint16_t endpointSize)
{
  if (endpointNumber == 0 || endpointNumber > (config_.removable ? 14:15))
  {
    return 0;
  }
  if (!dst) return 9 + 54 + (config_.removable ? 3:2) * 7;
  interfaceNumber_ = interfaceNumber;
  const uint16_t packet = endpointSize > 64 ? 512 : CCID_BULK_ENDPOINT_SIZE;
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t epNotify = static_cast<uint8_t>(0x80 | (endpointNumber + 1));

  uint16_t offset = 0;
  const uint8_t interfaceDescriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, uint8_t(config_.removable ? 3:2), USB_CLASS_SMART_CARD, 0x00, 0x00, stringIndex_,
  };
  memcpy(&dst[offset], interfaceDescriptor, sizeof(interfaceDescriptor));
  offset += sizeof(interfaceDescriptor);

  // CCID class descriptor (CCID 1.1 section 5.1), 54 bytes.
  uint8_t *ccid = &dst[offset];
  memset(ccid, 0, 54);
  ccid[0] = 54;
  ccid[1] = USB_DESC_CCID;
  ccidWrite16(&ccid[2], 0x0110);      // bcdCCID 1.10
  ccid[4] = 0;                        // bMaxSlotIndex: one slot
  ccid[5] = 0x07;                     // bVoltageSupport: 5V / 3V / 1.8V
  ccidWrite32(&ccid[6], 0x00000002);  // dwProtocols: T=1
  ccidWrite32(&ccid[10], 3580);       // dwDefaultClock (kHz)
  ccidWrite32(&ccid[14], 3580);       // dwMaximumClock (kHz)
  ccid[18] = 0;                       // bNumClockSupported: only the default
  ccidWrite32(&ccid[19], 9600);       // dwDataRate (bps)
  ccidWrite32(&ccid[23], 9600);       // dwMaxDataRate (bps)
  ccid[27] = 0;                       // bNumDataRatesSupported: only the default
  ccidWrite32(&ccid[28], 254);        // dwMaxIFSD
  ccidWrite32(&ccid[32], 0);          // dwSynchProtocols: none
  ccidWrite32(&ccid[36], 0);          // dwMechanical: no lock / eject mechanism
  // dwFeatures: the device handles activation, voltage, clock, baud rate, PPS
  // and parameter negotiation itself, and exchanges whole short APDUs. That last
  // part is what tells the host it may send an APDU rather than TPDUs.
  ccidWrite32(&ccid[40], 0x000204fe);
  ccidWrite32(&ccid[44], uint32_t(config_.messageBytes)); // dwMaxCCIDMessageLength
  ccid[48] = 0xff;                    // bClassGetResponse: echoes the APDU class
  ccid[49] = 0xff;                    // bClassEnvelope: echoes the APDU class
  ccidWrite16(&ccid[50], 0);          // wLcdLayout: no display
  ccid[52] = 0;                       // bPINSupport: no PIN pad
  ccid[53] = 1;                       // bMaxCCIDBusySlots
  offset += 54;

  const uint8_t endpoints[] = {
      7, USB_DESC_ENDPOINT, epOut, USB_ENDPOINT_ATTR_BULK,
      static_cast<uint8_t>(packet & 0xff),
      static_cast<uint8_t>((packet >> 8) & 0xff), 0,
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_BULK,
      static_cast<uint8_t>(packet & 0xff),
      static_cast<uint8_t>((packet >> 8) & 0xff), 0,
      7, USB_DESC_ENDPOINT, epNotify, USB_ENDPOINT_ATTR_INTERRUPT,
      static_cast<uint8_t>(CCID_INTERRUPT_ENDPOINT_SIZE & 0xff),
      static_cast<uint8_t>((CCID_INTERRUPT_ENDPOINT_SIZE >> 8) & 0xff),
      uint8_t(endpointSize > 64 ? 8 : CCID_INTERRUPT_INTERVAL_MS),
  };
  const size_t endpointBytes=config_.removable ? sizeof(endpoints):14;
  memcpy(&dst[offset], endpoints, endpointBytes);
  offset += endpointBytes;
  return offset;
}

void EspUsbDeviceCcid::onBusAttached()
{
  cardPowered_ = false;
  aborted_ = false;
  // A reader reports the slot state once the host has configured it, so a host
  // that enumerates while a card is already in the slot learns about it without
  // polling.
  notifySlotState();
}

void EspUsbDeviceCcid::onBusDetached()
{
  cardPowered_ = false;
  aborted_ = false;
}

bool EspUsbDeviceCcid::insertCard(const uint8_t *atr,size_t bytes) {
  if(!atr || bytes<2 || bytes>ESP_USB_DEVICE_CCID_MAX_ATR)return false;
  portENTER_CRITICAL(&ccidLock);
  if(state_ && (!config_.removable || state_->stopping)){portEXIT_CRITICAL(&ccidLock);return false;}
  memcpy(atr_,atr,bytes);atrLength_=uint8_t(bytes);cardPowered_=false;cardPresent_=true;
  portEXIT_CRITICAL(&ccidLock);notifySlotState();return true;
}
esp_err_t EspUsbDeviceCcid::removeCard() {
  portENTER_CRITICAL(&ccidLock);
  if(state_ && (!config_.removable || state_->stopping)){portEXIT_CRITICAL(&ccidLock);return ESP_ERR_INVALID_STATE;}
  cardPresent_=cardPowered_=false;atrLength_=0;
  portEXIT_CRITICAL(&ccidLock);notifySlotState();return ESP_OK;
}

bool EspUsbDeviceCcid::cardPresent() const
{
  return cardPresent_;
}

bool EspUsbDeviceCcid::cardPowered() const
{
  return cardPowered_;
}

size_t EspUsbDeviceCcid::atr(uint8_t *buffer,size_t capacity) const {
  portENTER_CRITICAL(&ccidLock);size_t n=atrLength_;
  if(!buffer || capacity<n)n=0;else memcpy(buffer,atr_,n);
  portEXIT_CRITICAL(&ccidLock);return n;
}

esp_err_t EspUsbDeviceCcid::onApdu(ApduCallback callback) {
  if(state_)return ESP_ERR_INVALID_STATE;
  apduCallback_=std::move(callback);return ESP_OK;
}

esp_err_t EspUsbDeviceCcid::onEscape(EscapeCallback callback) {
  if(state_)return ESP_ERR_INVALID_STATE;
  escapeCallback_=std::move(callback);return ESP_OK;
}

esp_err_t EspUsbDeviceCcid::onPower(PowerCallback callback) {
  if(state_)return ESP_ERR_INVALID_STATE;
  powerCallback_=std::move(callback);return ESP_OK;
}

bool EspUsbDeviceCcid::mounted() const
{
  portENTER_CRITICAL(&ccidLock);bool ready=state_ && !state_->stopping && state_->in && espusb_device_ready();portEXIT_CRITICAL(&ccidLock);return ready;
}

uint32_t EspUsbDeviceCcid::commandCount() const
{
  return commandCount_;
}

uint32_t EspUsbDeviceCcid::apduCount() const
{
  return apduCount_;
}

uint8_t EspUsbDeviceCcid::lastMessageType() const
{
  return lastMessageType_;
}

uint8_t EspUsbDeviceCcid::iccStatus() const
{
  if (!cardPresent_)
  {
    return ESP_USB_DEVICE_CCID_ICC_ABSENT;
  }
  return cardPowered_ ? ESP_USB_DEVICE_CCID_ICC_ACTIVE : ESP_USB_DEVICE_CCID_ICC_INACTIVE;
}

void EspUsbDeviceCcid::notifySlotState() {
  portENTER_CRITICAL(&ccidLock);
  notifyPending_=true;
  if(state_ && !state_->stopping && config_.removable)espusb::internal::postTinyUsbRuntimeJob(state_->job);
  portEXIT_CRITICAL(&ccidLock);
}
void EspUsbDeviceCcid::handleNotifyComplete(){if(notifyPending_)notifySlotState();}

size_t EspUsbDeviceCcid::writeHeader(uint8_t messageType, uint32_t dataLength, uint8_t slot,
                                     uint8_t sequence, uint8_t icc, uint8_t commandStatus,
                                     uint8_t error, uint8_t parameter)
{
  response_[0] = messageType;
  ccidWrite32(&response_[1], dataLength);
  response_[5] = slot;
  response_[6] = sequence;
  response_[7] = static_cast<uint8_t>((commandStatus << 6) | (icc & 0x03));
  response_[8] = error;
  response_[9] = parameter;
  return CCID_HEADER_SIZE;
}

void EspUsbDeviceCcid::sendStatus(uint8_t messageType, uint8_t slot, uint8_t sequence,
                                  uint8_t commandStatus, uint8_t error, uint8_t parameter)
{
  writeHeader(messageType, 0, slot, sequence, iccStatus(), commandStatus, error, parameter);
  state_->send(CCID_HEADER_SIZE);
}

void EspUsbDeviceCcid::sendData(uint8_t messageType, uint8_t slot, uint8_t sequence,
                                const uint8_t *data, size_t length, uint8_t parameter)
{
  if (length > config_.messageBytes - CCID_HEADER_SIZE)
  {
    sendStatus(messageType, slot, sequence, CCID_COMMAND_FAILED,
               ESP_USB_DEVICE_CCID_ERROR_XFR_OVERRUN, parameter);
    return;
  }
  writeHeader(messageType, static_cast<uint32_t>(length), slot, sequence, iccStatus(),
              CCID_COMMAND_OK, 0, parameter);
  if (length > 0 && data != response_ + CCID_HEADER_SIZE)
  {
    memcpy(&response_[CCID_HEADER_SIZE], data, length);
  }
  state_->send(CCID_HEADER_SIZE + length);
}

void EspUsbDeviceCcid::handleAbort(uint8_t slot, uint8_t sequence)
{
  if (slot != 0)
  {
    return;
  }
  // The class request only announces the abort. Everything until the matching
  // PC_to_RDR_Abort arrives has to fail with CMD_ABORTED (CCID 1.1 section 5.3.1),
  // which is what this flag makes handleMessage() do.
  aborted_ = true;
  abortSequence_ = sequence;
}

void EspUsbDeviceCcid::handleOverrun(uint8_t messageType, uint8_t slot, uint8_t sequence)
{
  const uint8_t responseType = ccidResponseType(messageType);
  commandCount_++;
  lastMessageType_ = messageType;
  sendStatus(responseType, slot, sequence, CCID_COMMAND_FAILED,
             ESP_USB_DEVICE_CCID_ERROR_XFR_OVERRUN, 0);
}

void EspUsbDeviceCcid::handleMessage(const uint8_t *message, size_t length)
{
  if (!message || length < CCID_HEADER_SIZE)
  {
    return;
  }
  const uint8_t messageType = message[0];
  const uint32_t dataLength = ccidRead32(&message[1]);
  const uint8_t slot = message[5];
  const uint8_t sequence = message[6];
  const uint8_t *data = &message[CCID_HEADER_SIZE];
  const size_t available = length - CCID_HEADER_SIZE;
  const size_t dataSize = dataLength < available ? dataLength : available;

  commandCount_++;
  lastMessageType_ = messageType;

  // Every response but the parameters one carries the same slot status, so a
  // message for a slot this reader does not have is refused the same way.
  if (slot != 0)
  {
    const uint8_t responseType = ccidResponseType(messageType);
    writeHeader(responseType, 0, slot, sequence, ESP_USB_DEVICE_CCID_ICC_ABSENT,
                CCID_COMMAND_FAILED, ESP_USB_DEVICE_CCID_ERROR_BAD_SLOT, 0);
    state_->send(CCID_HEADER_SIZE);
    return;
  }

  // An ABORT class request was received: everything up to the PC_to_RDR_Abort
  // that closes the sequence is refused (CCID 1.1 section 5.3.1).
  if (aborted_ && (messageType != CCID_PC_TO_RDR_ABORT || sequence != abortSequence_))
  {
    const uint8_t responseType = ccidResponseType(messageType);
    sendStatus(responseType, slot, sequence, CCID_COMMAND_FAILED,
               ESP_USB_DEVICE_CCID_ERROR_CMD_ABORTED, 0);
    return;
  }

  switch (messageType)
  {
  case CCID_PC_TO_RDR_ICC_POWER_ON:
  {
    uint8_t snapshot[ESP_USB_DEVICE_CCID_MAX_ATR];size_t bytes=0;bool wasPowered=false;
    portENTER_CRITICAL(&ccidLock);
    const bool present=cardPresent_;
    if(present){wasPowered=cardPowered_;cardPowered_=true;bytes=atrLength_;memcpy(snapshot,atr_,bytes);}
    portEXIT_CRITICAL(&ccidLock);
    if(!present){sendStatus(CCID_RDR_TO_PC_DATA_BLOCK,slot,sequence,CCID_COMMAND_FAILED,ESP_USB_DEVICE_CCID_ERROR_ICC_MUTE,0);return;}
    if(!wasPowered && powerCallback_)powerCallback_(true);
    sendData(CCID_RDR_TO_PC_DATA_BLOCK,slot,sequence,snapshot,bytes,0);
    return;
  }

  case CCID_PC_TO_RDR_ICC_POWER_OFF:
  {
    const bool wasPowered = cardPowered_;
    cardPowered_ = false;
    if (wasPowered && powerCallback_)
    {
      powerCallback_(false);
    }
    sendStatus(CCID_RDR_TO_PC_SLOT_STATUS, slot, sequence, CCID_COMMAND_OK, 0, 0);
    return;
  }

  case CCID_PC_TO_RDR_GET_SLOT_STATUS:
    // bClockStatus 0: the clock is running.
    sendStatus(CCID_RDR_TO_PC_SLOT_STATUS, slot, sequence, CCID_COMMAND_OK, 0, 0);
    return;

  case CCID_PC_TO_RDR_XFR_BLOCK:
  {
    if (!cardPresent_ || !cardPowered_)
    {
      sendStatus(CCID_RDR_TO_PC_DATA_BLOCK, slot, sequence, CCID_COMMAND_FAILED,
                 ESP_USB_DEVICE_CCID_ERROR_ICC_MUTE, 0);
      return;
    }
    apduCount_++;
    size_t responseLength = 0;
    if (apduCallback_)
    {
      responseLength = apduCallback_(data, dataSize, &response_[CCID_HEADER_SIZE],
                                     config_.messageBytes - CCID_HEADER_SIZE);
    }
    else
    {
      // No card application in the sketch: answer like a card that does not know
      // the instruction (ISO 7816-4 SW 6D00) rather than failing the exchange.
      response_[CCID_HEADER_SIZE] = 0x6d;
      response_[CCID_HEADER_SIZE + 1] = 0x00;
      responseLength = 2;
    }
    if (responseLength == 0)
    {
      sendStatus(CCID_RDR_TO_PC_DATA_BLOCK, slot, sequence, CCID_COMMAND_FAILED,
                 ESP_USB_DEVICE_CCID_ERROR_ICC_MUTE, 0);
      return;
    }
    sendData(CCID_RDR_TO_PC_DATA_BLOCK, slot, sequence, &response_[CCID_HEADER_SIZE],
             responseLength, 0);
    return;
  }

  case CCID_PC_TO_RDR_ESCAPE:
  {
    // No handler means the device has no vendor extensions at all, and a handler
    // that returns 0 is refusing this one. Both are "not supported" to the host.
    size_t responseLength = 0;
    if (escapeCallback_)
    {
      responseLength = escapeCallback_(data, dataSize, &response_[CCID_HEADER_SIZE],
                                       config_.messageBytes - CCID_HEADER_SIZE);
    }
    if (responseLength == 0)
    {
      sendStatus(CCID_RDR_TO_PC_ESCAPE, slot, sequence, CCID_COMMAND_FAILED,
                 ESP_USB_DEVICE_CCID_ERROR_CMD_NOT_SUPPORTED, 0);
      return;
    }
    sendData(CCID_RDR_TO_PC_ESCAPE, slot, sequence, &response_[CCID_HEADER_SIZE],
             responseLength, 0);
    return;
  }

  case CCID_PC_TO_RDR_GET_PARAMETERS:
  case CCID_PC_TO_RDR_RESET_PARAMETERS:
    // bProtocolNum 1 = T=1, the only protocol this slot declares.
    sendData(CCID_RDR_TO_PC_PARAMETERS, slot, sequence, CCID_T1_PARAMETERS,
             sizeof(CCID_T1_PARAMETERS), 1);
    return;

  case CCID_PC_TO_RDR_SET_PARAMETERS:
  {
    const uint8_t protocol = message[7];
    if (protocol != 1)
    {
      // bError 7: the offending byte is bProtocolNum.
      writeHeader(CCID_RDR_TO_PC_PARAMETERS, 0, slot, sequence, iccStatus(),
                  CCID_COMMAND_FAILED, 7, protocol);
      state_->send(CCID_HEADER_SIZE);
      return;
    }
    // The emulated slot has nothing to configure, so the answer is the same
    // fixed parameter set regardless of what was asked for.
    sendData(CCID_RDR_TO_PC_PARAMETERS, slot, sequence, CCID_T1_PARAMETERS,
             sizeof(CCID_T1_PARAMETERS), 1);
    return;
  }

  case CCID_PC_TO_RDR_ABORT:
    if (!aborted_) {
      sendStatus(CCID_RDR_TO_PC_SLOT_STATUS, slot, sequence, CCID_COMMAND_FAILED, ESP_USB_DEVICE_CCID_ERROR_CMD_ABORTED, 0);
      return;
    }
    aborted_ = false;
    sendStatus(CCID_RDR_TO_PC_SLOT_STATUS, slot, sequence, CCID_COMMAND_OK, 0, 0);
    return;

  case CCID_PC_TO_RDR_ICC_CLOCK:
  case CCID_PC_TO_RDR_T0_APDU:
    // Nothing to do - the device already handles clock and T=0 header handling
    // itself - but a slot status answer is what the host expects.
    sendStatus(CCID_RDR_TO_PC_SLOT_STATUS, slot, sequence, CCID_COMMAND_OK, 0, 0);
    return;

  default:
    // bError 0 is the index of bMessageType: "that message is not supported".
    sendStatus(CCID_RDR_TO_PC_SLOT_STATUS, slot, sequence, CCID_COMMAND_FAILED,
               ESP_USB_DEVICE_CCID_ERROR_CMD_NOT_SUPPORTED, 0);
    return;
  }
}
