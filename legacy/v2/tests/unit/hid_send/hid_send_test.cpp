#include <cassert>
#include <cstddef>
#include <cstdint>

#define ESP_USB_DEVICE_HAS_TINYUSB 1
#define CFG_TUD_HID 2
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_STATE, ESP_ERR_INVALID_ARG, ESP_ERR_TIMEOUT };

static uint32_t clockMs, waited, readyAfter, disconnectAfter;
static bool connected, accepted;
static unsigned submissions;
static uint8_t submittedInstance, submittedId, submittedByte;
static size_t submittedLength;

uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; waited += ms; }
bool tud_ready() { return connected && waited < disconnectAfter; }
bool tud_hid_n_ready(uint8_t instance)
{
  assert(instance < CFG_TUD_HID);
  return tud_ready() && waited >= readyAfter;
}
bool tud_hid_n_report(uint8_t instance, uint8_t id, const void *data, size_t length)
{
  ++submissions;
  submittedInstance = instance;
  submittedId = id;
  submittedByte = *static_cast<const uint8_t *>(data);
  submittedLength = length;
  return accepted;
}

class EspUsbDevice
{
public:
  bool tinyusbStarted_ = true;
  int error = ESP_OK;
  void setLastError(int value) { error = value; }
  bool sendHidReport(uint8_t, uint8_t, const void *, size_t, uint32_t);
};

// SHIPPED_SENDER

static void reset()
{
  clockMs = waited = readyAfter = submissions = 0;
  disconnectAfter = UINT32_MAX;
  connected = accepted = true;
}

int main()
{
  const uint8_t payload[] = {42, 43};
  EspUsbDevice device;
  reset();
  assert(device.sendHidReport(1, 7, payload, sizeof(payload), 0));
  assert(waited == 0 && submissions == 1 && device.error == ESP_OK);
  assert(submittedInstance == 1 && submittedId == 7);
  assert(submittedByte == 42 && submittedLength == sizeof(payload));

  reset(); readyAfter = 8;
  assert(device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 8 && submissions == 1);

  reset(); readyAfter = 8;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 0));
  assert(waited == 0 && submissions == 0 && device.error == ESP_ERR_TIMEOUT);

  reset(); readyAfter = 21;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 20 && submissions == 0 && device.error == ESP_ERR_TIMEOUT);

  reset(); readyAfter = 20;
  assert(device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 20 && submissions == 1);

  reset(); clockMs = UINT32_MAX - 3; readyAfter = 8;
  assert(device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 8 && submissions == 1);

  reset(); clockMs = UINT32_MAX - 3; readyAfter = 21;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 20 && device.error == ESP_ERR_TIMEOUT);

  reset(); readyAfter = 10; disconnectAfter = 3;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 3 && submissions == 0 && device.error == ESP_ERR_INVALID_STATE);

  reset(); connected = false;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 0 && device.error == ESP_ERR_INVALID_STATE);

  reset(); device.tinyusbStarted_ = false;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 0 && device.error == ESP_ERR_INVALID_STATE);
  device.tinyusbStarted_ = true;

  reset();
  assert(!device.sendHidReport(CFG_TUD_HID, 0, payload, sizeof(payload), 20));
  assert(waited == 0 && submissions == 0 && device.error == ESP_ERR_INVALID_ARG);
  assert(!device.sendHidReport(0, 0, nullptr, sizeof(payload), 20));
  assert(!device.sendHidReport(0, 0, payload, 0, 20));
  assert(waited == 0 && submissions == 0);

  reset(); accepted = false;
  assert(!device.sendHidReport(0, 0, payload, sizeof(payload), 20));
  assert(waited == 0 && submissions == 1 && device.error == ESP_FAIL);
}
