// USB CCID (smart card reader) device function.
//
// TinyUSB has no CCID class driver, and the copy under src/ is vendored
// upstream-verbatim (tools/verify_tinyusb_vendor.py), so the driver cannot live
// in src/class/. It is registered instead through the application driver hook
// TinyUSB provides for exactly this, by way of internal/EspUsbDeviceAppDriver -
// see there for why the driver is not named by usbd_app_driver_get_cb directly.
// Nothing outside this file references it, so a sketch without a card reader
// links none of it, and the message buffers are allocated in begin() rather than
// reserved in .bss.

#include "EspUsbDevice.h"
#include "internal/EspUsbDeviceAppDriver.h"

#include <stdlib.h>
#include <string.h>

#if __has_include("soc/soc_caps.h")
#include "soc/soc_caps.h"
#endif

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#include "tusb.h"
#include "device/usbd_pvt.h"
#define ESP_USB_DEVICE_CCID_HAS_TINYUSB 1
#if __has_include("esp_heap_caps.h")
#include "esp_heap_caps.h"
#define ESP_USB_DEVICE_CCID_HAS_HEAP_CAPS 1
#else
#define ESP_USB_DEVICE_CCID_HAS_HEAP_CAPS 0
#endif
#else
#define ESP_USB_DEVICE_CCID_HAS_TINYUSB 0
#define ESP_USB_DEVICE_CCID_HAS_HEAP_CAPS 0
#endif

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
static constexpr size_t CCID_MESSAGE_SIZE = ESP_USB_DEVICE_CCID_BUFFER_SIZE;
static constexpr uint16_t CCID_BULK_ENDPOINT_SIZE = 64;
static constexpr uint16_t CCID_INTERRUPT_ENDPOINT_SIZE = 8;
static constexpr uint8_t CCID_INTERRUPT_INTERVAL_MS = 16;
// A full-speed bulk endpoint is 64 bytes, but buildDescriptors() rewrites bulk
// packet sizes to 512 for the High Speed descriptor set, so a P4 build has to be
// able to receive a 512-byte packet.
#if ESP_USB_DEVICE_CCID_HAS_TINYUSB && (CFG_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED)
static constexpr size_t CCID_EP_OUT_CAPACITY = 512;
#else
static constexpr size_t CCID_EP_OUT_CAPACITY = 64;
#endif

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

static EspUsbDeviceCcid *g_activeCcid = nullptr;

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

#if ESP_USB_DEVICE_CCID_HAS_TINYUSB

//--------------------------------------------------------------------+
// TinyUSB class driver
//--------------------------------------------------------------------+

namespace
{

struct CcidDriverState
{
  uint8_t rhport = 0;
  uint8_t interfaceNumber = 0xff;
  uint8_t endpointIn = 0;
  uint8_t endpointOut = 0;
  uint8_t endpointNotify = 0;
  uint16_t endpointInSize = 0;
  uint16_t endpointOutSize = 0;
  // Assembly of one PC_to_RDR message. A message is longer than one packet, so
  // `received` counts everything the host sent for it (including the part that
  // did not fit) and `stored` counts what is in the buffer.
  uint32_t received = 0;
  uint32_t stored = 0;
  // The last IN transfer ended on a packet boundary, so a ZLP still has to
  // follow before the host sees the transfer as complete.
  bool zlpPending = false;
};

CcidDriverState g_driver;

// Endpoint and assembly buffers. Heap-allocated in espUsbCcidDriverAttach() so
// that they exist only for a sketch that actually instantiates the class, and
// aligned because a DMA-capable port may transfer straight out of them.
uint8_t *g_epOutBuffer = nullptr;
uint8_t *g_epInBuffer = nullptr;
uint8_t *g_epNotifyBuffer = nullptr;
uint8_t *g_messageBuffer = nullptr;

void *ccidAlloc(size_t size)
{
  const size_t alignment = 64;
  const size_t rounded = ((size + alignment - 1) / alignment) * alignment;
#if ESP_USB_DEVICE_CCID_HAS_HEAP_CAPS
  return heap_caps_aligned_alloc(alignment, rounded,
                                 MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
  return aligned_alloc(alignment, rounded);
#endif
}

void ccidFreeBuffers()
{
  free(g_epOutBuffer);
  free(g_epInBuffer);
  free(g_epNotifyBuffer);
  free(g_messageBuffer);
  g_epOutBuffer = nullptr;
  g_epInBuffer = nullptr;
  g_epNotifyBuffer = nullptr;
  g_messageBuffer = nullptr;
}

bool ccidAllocBuffers()
{
  if (g_messageBuffer)
  {
    return true;
  }
  g_epOutBuffer = static_cast<uint8_t *>(ccidAlloc(CCID_EP_OUT_CAPACITY));
  g_epInBuffer = static_cast<uint8_t *>(ccidAlloc(CCID_MESSAGE_SIZE));
  g_epNotifyBuffer = static_cast<uint8_t *>(ccidAlloc(CCID_INTERRUPT_ENDPOINT_SIZE));
  g_messageBuffer = static_cast<uint8_t *>(ccidAlloc(CCID_MESSAGE_SIZE));
  if (!g_epOutBuffer || !g_epInBuffer || !g_epNotifyBuffer || !g_messageBuffer)
  {
    ccidFreeBuffers();
    return false;
  }
  return true;
}

bool ccidArmReceive()
{
  if (!g_driver.endpointOut || !g_epOutBuffer)
  {
    return false;
  }
  return usbd_edpt_xfer(g_driver.rhport, g_driver.endpointOut, g_epOutBuffer,
                        g_driver.endpointOutSize, false);
}

void ccidResetMessage()
{
  g_driver.received = 0;
  g_driver.stored = 0;
}

} // namespace

// Reply to a message that could not be stored in full. The host still expects a
// response carrying its bSeq, so the header is taken from what was buffered.
static void ccidReportOverrun()
{
  if (!g_activeCcid || g_driver.stored < CCID_HEADER_SIZE)
  {
    return;
  }
  g_activeCcid->handleOverrun(g_messageBuffer[0], g_messageBuffer[5], g_messageBuffer[6]);
}

// Defined below, next to the callbacks it names.
extern const usbd_class_driver_t espUsbDeviceCcidDriverTable[1];

bool espUsbCcidDriverAttach()
{
  if (!ccidAllocBuffers())
  {
    return false;
  }
  espUsbDeviceRegisterAppDrivers(espUsbDeviceCcidDriverTable, 1);
  return true;
}

void espUsbCcidDriverDetach()
{
  espUsbDeviceRegisterAppDrivers(nullptr, 0);
  g_driver = CcidDriverState();
  ccidFreeBuffers();
}

bool espUsbCcidDriverMounted()
{
  return g_driver.endpointIn != 0 && g_driver.endpointOut != 0;
}

bool espUsbCcidDriverSend(const uint8_t *message, size_t length)
{
  if (!message || length == 0 || length > CCID_MESSAGE_SIZE || !g_epInBuffer)
  {
    return false;
  }
  if (!g_driver.endpointIn)
  {
    return false;
  }
  if (!usbd_edpt_claim(g_driver.rhport, g_driver.endpointIn))
  {
    return false;
  }
  memcpy(g_epInBuffer, message, length);
  g_driver.zlpPending = g_driver.endpointInSize != 0 &&
                        (length % g_driver.endpointInSize) == 0;
  return usbd_edpt_xfer(g_driver.rhport, g_driver.endpointIn, g_epInBuffer,
                        static_cast<uint16_t>(length), false);
}

bool espUsbCcidDriverNotify(const uint8_t *data, size_t length)
{
  if (!data || length == 0 || length > CCID_INTERRUPT_ENDPOINT_SIZE || !g_epNotifyBuffer)
  {
    return false;
  }
  if (!g_driver.endpointNotify)
  {
    return false;
  }
  if (!usbd_edpt_claim(g_driver.rhport, g_driver.endpointNotify))
  {
    return false;
  }
  memcpy(g_epNotifyBuffer, data, length);
  return usbd_edpt_xfer(g_driver.rhport, g_driver.endpointNotify, g_epNotifyBuffer,
                        static_cast<uint16_t>(length), false);
}

static void ccidd_init(void)
{
  g_driver = CcidDriverState();
}

static bool ccidd_deinit(void)
{
  g_driver = CcidDriverState();
  return true;
}

static void ccidd_reset(uint8_t rhport)
{
  (void)rhport;
  g_driver = CcidDriverState();
}

static uint16_t ccidd_open(uint8_t rhport, tusb_desc_interface_t const *desc_itf, uint16_t max_len)
{
  TU_VERIFY(USB_CLASS_SMART_CARD == desc_itf->bInterfaceClass, 0);
  // Subclass / protocol 0 is bulk CCID. ICCD variants (protocol 1 and 2) speak
  // over control transfers only and are not what this driver implements.
  TU_VERIFY(0 == desc_itf->bInterfaceSubClass && 0 == desc_itf->bInterfaceProtocol, 0);
  TU_VERIFY(g_messageBuffer != nullptr, 0);

  const uint8_t *desc_end = reinterpret_cast<const uint8_t *>(desc_itf) + max_len;
  const uint8_t *p_desc = tu_desc_next(desc_itf);

  g_driver.rhport = rhport;
  g_driver.interfaceNumber = desc_itf->bInterfaceNumber;
  g_driver.endpointIn = 0;
  g_driver.endpointOut = 0;
  g_driver.endpointNotify = 0;
  ccidResetMessage();

  while (tu_desc_in_bounds(p_desc, desc_end))
  {
    const uint8_t desc_type = tu_desc_type(p_desc);
    if (desc_type == TUSB_DESC_INTERFACE || desc_type == TUSB_DESC_INTERFACE_ASSOCIATION)
    {
      break;
    }
    if (desc_type == TUSB_DESC_ENDPOINT)
    {
      const tusb_desc_endpoint_t *desc_ep = reinterpret_cast<const tusb_desc_endpoint_t *>(p_desc);
      const uint16_t packetSize = tu_edpt_packet_size(desc_ep);
      TU_ASSERT(usbd_edpt_open(rhport, desc_ep), 0);
      if (desc_ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT)
      {
        TU_ASSERT(packetSize <= CCID_INTERRUPT_ENDPOINT_SIZE, 0);
        g_driver.endpointNotify = desc_ep->bEndpointAddress;
      }
      else if (tu_edpt_dir(desc_ep->bEndpointAddress) == TUSB_DIR_IN)
      {
        g_driver.endpointIn = desc_ep->bEndpointAddress;
        g_driver.endpointInSize = packetSize;
      }
      else
      {
        TU_ASSERT(packetSize <= CCID_EP_OUT_CAPACITY, 0);
        g_driver.endpointOut = desc_ep->bEndpointAddress;
        g_driver.endpointOutSize = packetSize;
      }
    }
    p_desc = tu_desc_next(p_desc);
  }

  TU_ASSERT(g_driver.endpointIn && g_driver.endpointOut, 0);
  TU_ASSERT(ccidArmReceive(), 0);
  return static_cast<uint16_t>(reinterpret_cast<uintptr_t>(p_desc) -
                               reinterpret_cast<uintptr_t>(desc_itf));
}

static bool ccidd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
  if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_CLASS)
  {
    return false;
  }
  if (stage != CONTROL_STAGE_SETUP)
  {
    return true;
  }
  // GET_CLOCK_FREQUENCIES / GET_DATA_RATES are only legal when the class
  // descriptor announces more than one, which this device does not: stalling is
  // the conforming answer, so they fall through to the `false` below.
  if (request->bRequest == CCID_REQUEST_ABORT)
  {
    if (g_activeCcid)
    {
      g_activeCcid->handleAbort(tu_u16_low(request->wValue), tu_u16_high(request->wValue));
    }
    return tud_control_status(rhport, request);
  }
  return false;
}

static bool ccidd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
  (void)rhport;

  if (ep_addr == g_driver.endpointIn)
  {
    if (g_driver.zlpPending)
    {
      g_driver.zlpPending = false;
      if (usbd_edpt_claim(g_driver.rhport, g_driver.endpointIn))
      {
        usbd_edpt_xfer(g_driver.rhport, g_driver.endpointIn, g_epInBuffer, 0, false);
      }
    }
    return true;
  }

  if (ep_addr == g_driver.endpointNotify)
  {
    // The endpoint is free again: send the slot state if a change was dropped
    // while this notification was waiting to be polled.
    if (g_activeCcid)
    {
      g_activeCcid->handleNotifyComplete();
    }
    return true;
  }

  if (ep_addr != g_driver.endpointOut)
  {
    return true;
  }

  if (result != XFER_RESULT_SUCCESS)
  {
    ccidResetMessage();
    ccidArmReceive();
    return true;
  }

  if (xferred_bytes > 0)
  {
    g_driver.received += xferred_bytes;
    if (g_driver.stored < CCID_MESSAGE_SIZE)
    {
      const uint32_t room = CCID_MESSAGE_SIZE - g_driver.stored;
      const uint32_t copy = xferred_bytes < room ? xferred_bytes : room;
      memcpy(&g_messageBuffer[g_driver.stored], g_epOutBuffer, copy);
      g_driver.stored += copy;
    }
  }

  if (g_driver.stored >= CCID_HEADER_SIZE)
  {
    const uint32_t total = CCID_HEADER_SIZE + ccidRead32(&g_messageBuffer[1]);
    if (g_driver.received >= total)
    {
      if (total <= CCID_MESSAGE_SIZE)
      {
        if (g_activeCcid)
        {
          g_activeCcid->handleMessage(g_messageBuffer, total);
        }
      }
      else
      {
        ccidReportOverrun();
      }
      ccidResetMessage();
    }
  }
  else if (xferred_bytes == 0 || xferred_bytes < g_driver.endpointOutSize)
  {
    // Short packet before even a header arrived: the host ended the transfer, so
    // whatever is buffered cannot become a message.
    ccidResetMessage();
  }

  ccidArmReceive();
  return true;
}

const usbd_class_driver_t espUsbDeviceCcidDriverTable[1] = {
    {
        .name = "CCID",
        .init = ccidd_init,
        .deinit = ccidd_deinit,
        .reset = ccidd_reset,
        .open = ccidd_open,
        .control_xfer_cb = ccidd_control_xfer_cb,
        .xfer_cb = ccidd_xfer_cb,
        .xfer_isr = NULL,
        .sof = NULL,
    },
};

#else // !ESP_USB_DEVICE_CCID_HAS_TINYUSB

static bool espUsbCcidDriverAttach() { return false; }
static void espUsbCcidDriverDetach() {}
static bool espUsbCcidDriverMounted() { return false; }
static bool espUsbCcidDriverSend(const uint8_t *, size_t) { return false; }
static bool espUsbCcidDriverNotify(const uint8_t *, size_t) { return false; }

#endif // ESP_USB_DEVICE_CCID_HAS_TINYUSB

//--------------------------------------------------------------------+
// EspUsbDeviceCcid
//--------------------------------------------------------------------+

EspUsbDeviceCcid::EspUsbDeviceCcid(EspUsbDevice &device) : EspUsbDeviceClass(device)
{
}

EspUsbDeviceCcid::~EspUsbDeviceCcid()
{
  end();
}

bool EspUsbDeviceCcid::begin()
{
  if (g_activeCcid && g_activeCcid != this)
  {
    return false;
  }
  if (!espUsbCcidDriverAttach())
  {
    return false;
  }
  g_activeCcid = this;
  cardPowered_ = false;
  aborted_ = false;
  return true;
}

void EspUsbDeviceCcid::end()
{
  if (g_activeCcid != this)
  {
    return;
  }
  g_activeCcid = nullptr;
  espUsbCcidDriverDetach();
}

uint16_t EspUsbDeviceCcid::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber,
                                                   uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)endpointSize;
  if (!dst || endpointNumber == 0 || endpointNumber > 14)
  {
    return 0;
  }
  const uint8_t epOut = endpointNumber;
  const uint8_t epIn = static_cast<uint8_t>(0x80 | endpointNumber);
  const uint8_t epNotify = static_cast<uint8_t>(0x80 | (endpointNumber + 1));

  uint16_t offset = 0;
  const uint8_t interfaceDescriptor[] = {
      9, USB_DESC_INTERFACE, interfaceNumber, 0, 3, USB_CLASS_SMART_CARD, 0x00, 0x00, 0,
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
  ccidWrite32(&ccid[44], CCID_HEADER_SIZE + 261); // dwMaxCCIDMessageLength
  ccid[48] = 0xff;                    // bClassGetResponse: echoes the APDU class
  ccid[49] = 0xff;                    // bClassEnvelope: echoes the APDU class
  ccidWrite16(&ccid[50], 0);          // wLcdLayout: no display
  ccid[52] = 0;                       // bPINSupport: no PIN pad
  ccid[53] = 1;                       // bMaxCCIDBusySlots
  offset += 54;

  const uint8_t endpoints[] = {
      7, USB_DESC_ENDPOINT, epOut, USB_ENDPOINT_ATTR_BULK,
      static_cast<uint8_t>(CCID_BULK_ENDPOINT_SIZE & 0xff),
      static_cast<uint8_t>((CCID_BULK_ENDPOINT_SIZE >> 8) & 0xff), 0,
      7, USB_DESC_ENDPOINT, epIn, USB_ENDPOINT_ATTR_BULK,
      static_cast<uint8_t>(CCID_BULK_ENDPOINT_SIZE & 0xff),
      static_cast<uint8_t>((CCID_BULK_ENDPOINT_SIZE >> 8) & 0xff), 0,
      7, USB_DESC_ENDPOINT, epNotify, USB_ENDPOINT_ATTR_INTERRUPT,
      static_cast<uint8_t>(CCID_INTERRUPT_ENDPOINT_SIZE & 0xff),
      static_cast<uint8_t>((CCID_INTERRUPT_ENDPOINT_SIZE >> 8) & 0xff),
      CCID_INTERRUPT_INTERVAL_MS,
  };
  memcpy(&dst[offset], endpoints, sizeof(endpoints));
  offset += sizeof(endpoints);
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

bool EspUsbDeviceCcid::insertCard(const uint8_t *atr, size_t length)
{
  if (!atr || length < 2 || length > ESP_USB_DEVICE_CCID_MAX_ATR)
  {
    return false;
  }
  // Write the ATR before announcing the card: the device task only reads it once
  // cardPresent_ is set, so no lock is needed between the two tasks.
  memcpy(atr_, atr, length);
  atrLength_ = static_cast<uint8_t>(length);
  cardPowered_ = false;
  cardPresent_ = true;
  notifySlotState();
  return true;
}

void EspUsbDeviceCcid::removeCard()
{
  cardPresent_ = false;
  cardPowered_ = false;
  atrLength_ = 0;
  notifySlotState();
}

bool EspUsbDeviceCcid::cardPresent() const
{
  return cardPresent_;
}

bool EspUsbDeviceCcid::cardPowered() const
{
  return cardPowered_;
}

size_t EspUsbDeviceCcid::atr(uint8_t *buffer, size_t capacity) const
{
  if (!buffer || capacity < atrLength_)
  {
    return 0;
  }
  memcpy(buffer, atr_, atrLength_);
  return atrLength_;
}

void EspUsbDeviceCcid::onApdu(ApduCallback callback)
{
  apduCallback_ = callback;
}

void EspUsbDeviceCcid::onEscape(EscapeCallback callback)
{
  escapeCallback_ = callback;
}

void EspUsbDeviceCcid::onPower(PowerCallback callback)
{
  powerCallback_ = callback;
}

bool EspUsbDeviceCcid::mounted() const
{
  return espUsbCcidDriverMounted();
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

void EspUsbDeviceCcid::notifySlotState()
{
  // RDR_to_PC_NotifySlotChange: two bits per slot, bit 0 the current state and
  // bit 1 "changed since the last notification".
  const uint8_t message[2] = {
      CCID_RDR_TO_PC_NOTIFY_SLOT_CHANGE,
      static_cast<uint8_t>((cardPresent_ ? 0x01 : 0x00) | 0x02),
  };
  // The endpoint holds one notification at a time and only frees up when the
  // host polls it - which it does not do at all until it opens the interface, so
  // the very first notification can sit there for a long time. A card that moves
  // in the meantime must not be lost: remember that the host is behind, and send
  // the state it ends up in once the endpoint is free (handleNotifyComplete()).
  notifyPending_ = !espUsbCcidDriverNotify(message, sizeof(message));
}

void EspUsbDeviceCcid::handleNotifyComplete()
{
  if (!notifyPending_)
  {
    return;
  }
  notifyPending_ = false;
  notifySlotState();
}

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
  espUsbCcidDriverSend(response_, CCID_HEADER_SIZE);
}

void EspUsbDeviceCcid::sendData(uint8_t messageType, uint8_t slot, uint8_t sequence,
                                const uint8_t *data, size_t length, uint8_t parameter)
{
  if (length > sizeof(response_) - CCID_HEADER_SIZE)
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
  espUsbCcidDriverSend(response_, CCID_HEADER_SIZE + length);
}

void EspUsbDeviceCcid::handleAbort(uint8_t slot, uint8_t sequence)
{
  (void)sequence;
  if (slot != 0)
  {
    return;
  }
  // The class request only announces the abort. Everything until the matching
  // PC_to_RDR_Abort arrives has to fail with CMD_ABORTED (CCID 1.1 section 5.3.1),
  // which is what this flag makes handleMessage() do.
  aborted_ = true;
}

void EspUsbDeviceCcid::handleOverrun(uint8_t messageType, uint8_t slot, uint8_t sequence)
{
  const uint8_t responseType = (messageType == CCID_PC_TO_RDR_XFR_BLOCK ||
                                messageType == CCID_PC_TO_RDR_ICC_POWER_ON)
                                   ? CCID_RDR_TO_PC_DATA_BLOCK
                                   : CCID_RDR_TO_PC_SLOT_STATUS;
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
    const uint8_t responseType = (messageType == CCID_PC_TO_RDR_XFR_BLOCK ||
                                  messageType == CCID_PC_TO_RDR_ICC_POWER_ON)
                                     ? CCID_RDR_TO_PC_DATA_BLOCK
                                     : CCID_RDR_TO_PC_SLOT_STATUS;
    writeHeader(responseType, 0, slot, sequence, ESP_USB_DEVICE_CCID_ICC_ABSENT,
                CCID_COMMAND_FAILED, ESP_USB_DEVICE_CCID_ERROR_BAD_SLOT, 0);
    espUsbCcidDriverSend(response_, CCID_HEADER_SIZE);
    return;
  }

  // An ABORT class request was received: everything up to the PC_to_RDR_Abort
  // that closes the sequence is refused (CCID 1.1 section 5.3.1).
  if (aborted_ && messageType != CCID_PC_TO_RDR_ABORT)
  {
    const uint8_t responseType = (messageType == CCID_PC_TO_RDR_XFR_BLOCK ||
                                  messageType == CCID_PC_TO_RDR_ICC_POWER_ON)
                                     ? CCID_RDR_TO_PC_DATA_BLOCK
                                     : CCID_RDR_TO_PC_SLOT_STATUS;
    sendStatus(responseType, slot, sequence, CCID_COMMAND_FAILED,
               ESP_USB_DEVICE_CCID_ERROR_CMD_ABORTED, 0);
    return;
  }

  switch (messageType)
  {
  case CCID_PC_TO_RDR_ICC_POWER_ON:
  {
    if (!cardPresent_)
    {
      sendStatus(CCID_RDR_TO_PC_DATA_BLOCK, slot, sequence, CCID_COMMAND_FAILED,
                 ESP_USB_DEVICE_CCID_ERROR_ICC_MUTE, 0);
      return;
    }
    const bool wasPowered = cardPowered_;
    cardPowered_ = true;
    if (!wasPowered && powerCallback_)
    {
      powerCallback_(true);
    }
    sendData(CCID_RDR_TO_PC_DATA_BLOCK, slot, sequence, atr_, atrLength_, 0);
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
                                     sizeof(response_) - CCID_HEADER_SIZE);
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
                                       sizeof(response_) - CCID_HEADER_SIZE);
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
      espUsbCcidDriverSend(response_, CCID_HEADER_SIZE);
      return;
    }
    // The emulated slot has nothing to configure, so the answer is the same
    // fixed parameter set regardless of what was asked for.
    sendData(CCID_RDR_TO_PC_PARAMETERS, slot, sequence, CCID_T1_PARAMETERS,
             sizeof(CCID_T1_PARAMETERS), 1);
    return;
  }

  case CCID_PC_TO_RDR_ABORT:
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
