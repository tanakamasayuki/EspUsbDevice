// USB Audio Class (UAC) implementation for EspUsbDevice.
//
// Portions of this file (the TinyUSB control-transfer callbacks, the descriptor
// loader, and the software volume helper) are derived from the Espressif
// Arduino-ESP32 USBAudioCard component:
//
//   Copyright 2015-2026 Espressif Systems (Shanghai) PTE LTD
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//
// The logic was folded directly into the EspUsbDeviceAudio class so the library
// owns a single, self-contained audio implementation (no separate USBAudioCard
// class, void* indirection, or duplicate enum set).

#include "EspUsbDevice.h"

#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED && __has_include("esp32-hal-tinyusb.h")
#include "sdkconfig.h"
#if defined(CONFIG_TINYUSB_AUDIO_ENABLED) && CONFIG_TINYUSB_AUDIO_ENABLED
#define ESP_USB_DEVICE_AUDIO_IMPL 1
#endif
#endif

#if ESP_USB_DEVICE_AUDIO_IMPL

#include "USB.h"
#include "esp32-hal-tinyusb.h"
#include "EspUsbDeviceAudioDescriptors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include <math.h>
#include <inttypes.h>

// Internal event base. Audio control events (volume / mute / interface /
// sample-rate) are posted here from the TinyUSB control-transfer callbacks and
// dispatched to the user onEvent() callback on a dedicated event loop.
ESP_EVENT_DEFINE_BASE(ESP_USB_DEVICE_AUDIO_EVENTS);

// UAC2 class-specific control-request layout.
//
// TinyUSB used to expose this as `audio20_control_request_t`, but the struct was
// removed from audio.h in the Arduino-ESP32 3.3.11 core (only the type name went
// away; the AUDIO20_* constants and audio20_control_cur_*/range_* types remain).
// The layout is fixed by the USB Audio 2.0 spec (section 5.2.2) and is byte-for-
// byte a reinterpretation of the standard 8-byte setup packet, so we define our
// own copy here and cast tusb_control_request_t to it. This keeps the code
// building on both the old cores (that still ship the type) and the new ones.
typedef struct TU_ATTR_PACKED {
  union {
    struct TU_ATTR_PACKED {
      uint8_t recipient : 5;  ///< Recipient type tusb_request_recipient_t.
      uint8_t type : 2;       ///< Request type tusb_request_type_t.
      uint8_t direction : 1;  ///< Direction type. tusb_dir_t
    } bmRequestType_bit;
    uint8_t bmRequestType;
  };
  uint8_t bRequest;  ///< Request type audio_cs_req_t
  uint8_t bChannelNumber;
  uint8_t bControlSelector;
  union {
    uint8_t bInterface;
    uint8_t bEndpoint;
  };
  uint8_t bEntityID;
  uint16_t wLength;
} esp_usb_audio20_control_request_t;

// The single active audio instance. TinyUSB calls the tud_audio_* callbacks by
// name (C linkage, no user context), so they reach the instance through this.
static EspUsbDeviceAudio *g_activeAudio = nullptr;

// TinyUSB-facing state for the one audio function (CFG_TUD_AUDIO_FUNC_1).
static uint8_t _itf_num = 0;
static uint32_t _sample_rate = 48000;
static uint8_t _spk_channels = 2;
static uint8_t _mic_channels = 2;
static uint8_t _bits_per_sample = 24;
static uint8_t _bytes_per_sample = 4;
static bool _mute[3] = {false, false, false};
static int16_t _volume[3] = {0, 0, 0};
static int32_t *_spk_buf = NULL;  //[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ / 4]
static TaskHandle_t _receiveTaskHandle = NULL;

// The user onEvent() callback must not run on the USB device task: the Arduino
// core "arduino_usb_events" loop only has a 2048-byte stack
// (ARDUINO_SERIAL_EVENT_TASK_STACK_SIZE), which overflows as soon as a user
// callback does anything nontrivial such as Serial.printf(). A Windows
// volume-slider drag sends a burst of SET_CUR requests and reliably crashed
// that loop with a stack-canary panic, so audio events run on this dedicated
// loop with a generous stack instead.
static esp_event_loop_handle_t _audio_event_loop = NULL;

static esp_event_loop_handle_t audioEventLoop()
{
  if (_audio_event_loop == NULL)
  {
    esp_event_loop_args_t args = {};
    args.queue_size = 32;
    args.task_name = "uacEvents";
    args.task_priority = 5;
    args.task_stack_size = 8192;
    args.task_core_id = tskNO_AFFINITY;
    if (esp_event_loop_create(&args, &_audio_event_loop) != ESP_OK)
    {
      _audio_event_loop = NULL;
    }
  }
  return _audio_event_loop;
}

// Non-blocking post: never block the USB device task inside a control-transfer
// callback. If the queue is full (e.g. a rapid volume-slider drag) the extra
// notifications are dropped. The authoritative volume/mute state is already in
// _volume[]/_mute[] and applied by applyVolume(), so dropping a notification is
// harmless.
static void audioPostEvent(const EspUsbDeviceAudioEvent &event)
{
  esp_event_loop_handle_t loop = audioEventLoop();
  if (loop != NULL)
  {
    esp_event_post_to(loop, ESP_USB_DEVICE_AUDIO_EVENTS, 0, &event, sizeof(event), 0);
  }
}

// Runs on the dedicated event loop task (generous stack): safe to invoke the
// user callback here.
static void audioEventDispatch(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  (void)arg;
  (void)id;
  if (g_activeAudio != nullptr && base == ESP_USB_DEVICE_AUDIO_EVENTS && data != nullptr)
  {
    g_activeAudio->handleEvent(*static_cast<const EspUsbDeviceAudioEvent *>(data));
  }
}

uint16_t tusb_audio_load_descriptor(uint8_t *dst, uint8_t *itf)
{
  _itf_num = *itf;
#if TUD_OPT_HIGH_SPEED
  uint8_t str_index = tinyusb_add_string_descriptor("TinyUSB UAC2");
  if (_spk_channels == 2 && _mic_channels > 0)
  {
    // Stereo Headset
    uint8_t ep_num = tinyusb_get_free_duplex_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t int_ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(int_ep_num != 0);
    uint8_t descriptor[TUD_AUDIO20_HEADSET_STEREO_DESC_LEN] = {
      TUD_AUDIO20_HEADSET_STEREO_DESCRIPTOR(
        _itf_num, str_index, ep_num, (uint8_t)(ep_num | 0x80), (uint8_t)(int_ep_num | 0x80), CFG_TUD_AUDIO_MAX_SAMPLE_RATE, _spk_channels, _mic_channels,
        _bytes_per_sample, _bits_per_sample
      )
    };
    *itf += 3;
    memcpy(dst, descriptor, TUD_AUDIO20_HEADSET_STEREO_DESC_LEN);
    return TUD_AUDIO20_HEADSET_STEREO_DESC_LEN;
  }
  else if (_spk_channels == 1 && _mic_channels > 0)
  {
    // Mono Headset
    uint8_t ep_num = tinyusb_get_free_duplex_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t int_ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(int_ep_num != 0);
    uint8_t descriptor[TUD_AUDIO20_HEADSET_MONO_DESC_LEN] = {
      TUD_AUDIO20_HEADSET_MONO_DESCRIPTOR(
        _itf_num, str_index, ep_num, (uint8_t)(ep_num | 0x80), (uint8_t)(int_ep_num | 0x80), CFG_TUD_AUDIO_MAX_SAMPLE_RATE, _spk_channels, _mic_channels,
        _bytes_per_sample, _bits_per_sample
      )
    };
    *itf += 3;
    memcpy(dst, descriptor, TUD_AUDIO20_HEADSET_MONO_DESC_LEN);
    return TUD_AUDIO20_HEADSET_MONO_DESC_LEN;
  }
  else if (_spk_channels == 2 && _mic_channels == 0)
  {
    // Stereo Speaker
    uint8_t ep_num = tinyusb_get_free_out_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t int_ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(int_ep_num != 0);
    uint8_t descriptor[TUD_AUDIO20_SPEAKER_STEREO_DESC_LEN] = {
      TUD_AUDIO20_SPEAKER_STEREO_DESCRIPTOR(
        _itf_num, str_index, ep_num, (uint8_t)(int_ep_num | 0x80), CFG_TUD_AUDIO_MAX_SAMPLE_RATE, _spk_channels, _bytes_per_sample, _bits_per_sample
      )
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_AUDIO20_SPEAKER_STEREO_DESC_LEN);
    return TUD_AUDIO20_SPEAKER_STEREO_DESC_LEN;
  }
  else if (_spk_channels == 1 && _mic_channels == 0)
  {
    // Mono Speaker
    uint8_t ep_num = tinyusb_get_free_out_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t int_ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(int_ep_num != 0);
    uint8_t descriptor[TUD_AUDIO20_SPEAKER_MONO_DESC_LEN] = {
      TUD_AUDIO20_SPEAKER_MONO_DESCRIPTOR(
        _itf_num, str_index, ep_num, (uint8_t)(int_ep_num | 0x80), CFG_TUD_AUDIO_MAX_SAMPLE_RATE, _spk_channels, _bytes_per_sample, _bits_per_sample
      )
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_AUDIO20_SPEAKER_MONO_DESC_LEN);
    return TUD_AUDIO20_SPEAKER_MONO_DESC_LEN;
  }
  else if (_spk_channels == 0 && _mic_channels > 0)
  {
    // Microphone(s)
    uint8_t ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t int_ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(int_ep_num != 0);
    uint8_t descriptor[TUD_AUDIO20_MICROPHONE_DESC_LEN] = {
      TUD_AUDIO20_MICROPHONE_DESCRIPTOR(
        _itf_num, str_index, (uint8_t)(ep_num | 0x80), (uint8_t)(int_ep_num | 0x80), CFG_TUD_AUDIO_MAX_SAMPLE_RATE, _mic_channels, _bytes_per_sample,
        _bits_per_sample
      )
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_AUDIO20_MICROPHONE_DESC_LEN);
    return TUD_AUDIO20_MICROPHONE_DESC_LEN;
  }
#else
  uint8_t str_index = tinyusb_add_string_descriptor("TinyUSB UAC1");
  if (_spk_channels == 2 && _mic_channels > 0)
  {
    // Stereo Headset
    uint8_t ep_num = tinyusb_get_free_duplex_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t descriptor[TUD_AUDIO10_HEADSET_STEREO_DESC_LEN(1)] = {
      TUD_AUDIO10_HEADSET_STEREO_DESCRIPTOR(
        _itf_num, str_index, ep_num, (uint8_t)(ep_num | 0x80), 48000, _spk_channels, _mic_channels, _bytes_per_sample, _bits_per_sample, _sample_rate
      )
    };
    *itf += 3;
    memcpy(dst, descriptor, TUD_AUDIO10_HEADSET_STEREO_DESC_LEN(1));
    return TUD_AUDIO10_HEADSET_STEREO_DESC_LEN(1);
  }
  else if (_spk_channels == 1 && _mic_channels > 0)
  {
    // Mono Headset
    uint8_t ep_num = tinyusb_get_free_duplex_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t descriptor[TUD_AUDIO10_HEADSET_MONO_DESC_LEN(1)] = {
      TUD_AUDIO10_HEADSET_MONO_DESCRIPTOR(
        _itf_num, str_index, ep_num, (uint8_t)(ep_num | 0x80), 48000, _spk_channels, _mic_channels, _bytes_per_sample, _bits_per_sample, _sample_rate
      )
    };
    *itf += 3;
    memcpy(dst, descriptor, TUD_AUDIO10_HEADSET_MONO_DESC_LEN(1));
    return TUD_AUDIO10_HEADSET_MONO_DESC_LEN(1);
  }
  else if (_spk_channels == 2 && _mic_channels == 0)
  {
    // Stereo Speaker
    uint8_t ep_num = tinyusb_get_free_out_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t descriptor[TUD_AUDIO10_SPEAKER_STEREO_DESC_LEN(1)] = {
      TUD_AUDIO10_SPEAKER_STEREO_DESCRIPTOR(_itf_num, str_index, ep_num, 48000, _spk_channels, _bytes_per_sample, _bits_per_sample, _sample_rate)
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_AUDIO10_SPEAKER_STEREO_DESC_LEN(1));
    return TUD_AUDIO10_SPEAKER_STEREO_DESC_LEN(1);
  }
  else if (_spk_channels == 1 && _mic_channels == 0)
  {
    // Mono Speaker
    uint8_t ep_num = tinyusb_get_free_out_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t descriptor[TUD_AUDIO10_SPEAKER_MONO_DESC_LEN(1)] = {
      TUD_AUDIO10_SPEAKER_MONO_DESCRIPTOR(_itf_num, str_index, ep_num, 48000, _spk_channels, _bytes_per_sample, _bits_per_sample, _sample_rate)
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_AUDIO10_SPEAKER_MONO_DESC_LEN(1));
    return TUD_AUDIO10_SPEAKER_MONO_DESC_LEN(1);
  }
  else if (_spk_channels == 0 && _mic_channels > 0)
  {
    // Microphone(s)
    uint8_t ep_num = tinyusb_get_free_in_endpoint();
    TU_VERIFY(ep_num != 0);
    uint8_t descriptor[TUD_AUDIO10_MICROPHONE_DESC_LEN(1)] = {
      TUD_AUDIO10_MICROPHONE_DESCRIPTOR(_itf_num, str_index, (uint8_t)(ep_num | 0x80), 48000, _mic_channels, _bytes_per_sample, _bits_per_sample, _sample_rate)
    };
    *itf += 2;
    memcpy(dst, descriptor, TUD_AUDIO10_MICROPHONE_DESC_LEN(1));
    return TUD_AUDIO10_MICROPHONE_DESC_LEN(1);
  }
#endif
  return 0;
}

static void audioReceiveTask(void *pvParameters)
{
  (void)pvParameters;
  for (;;)
  {
    uint16_t len = tud_audio_read(_spk_buf, CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ);
    if (len > 0 && g_activeAudio != nullptr)
    {
      g_activeAudio->handleData(_spk_buf, len);
    }
    delay(2);
  }
}

#define dump_control_request(p_request)                                                                                                          \
  log_v(                                                                                                                                         \
    "Control request: bRequest = 0x%x, wValue = 0x%x, wIndex = 0x%x, wLength = 0x%x", p_request->bRequest, p_request->wValue, p_request->wIndex, \
    p_request->wLength                                                                                                                           \
  )

// Invoked when audio class specific set request received for an EP
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff)
{
  (void)rhport;
  dump_control_request(p_request);
#if TUD_OPT_HIGH_SPEED
  (void)pBuff;
  (void)p_request;
  return false;
#else
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  if (ctrlSel == AUDIO10_EP_CTRL_SAMPLING_FREQ && p_request->bRequest == AUDIO10_CS_REQ_SET_CUR && p_request->wLength == 3)
  {
    _sample_rate = tu_unaligned_read32(pBuff) & 0x00FFFFFF;
    log_d("EP set current freq: %" PRIu32, _sample_rate);
    EspUsbDeviceAudioEvent event;
    event.type = ESP_USB_DEVICE_AUDIO_EVENT_SAMPLE_RATE;
    event.sampleRate = _sample_rate;
    audioPostEvent(event);
    return true;
  }
  log_w("Set EP request not handled, ctrlSel = %d, bRequest = %d, wLength = %d", ctrlSel, p_request->bRequest, p_request->wLength);
  return false;
#endif
}

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  dump_control_request(p_request);
#if TUD_OPT_HIGH_SPEED
  (void)rhport;
  (void)p_request;
  return false;
#else
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  if (ctrlSel == AUDIO10_EP_CTRL_SAMPLING_FREQ && p_request->bRequest == AUDIO10_CS_REQ_GET_CUR)
  {
    log_d("EP get current freq");
    uint8_t freq[3];
    freq[0] = (uint8_t)(_sample_rate & 0xFF);
    freq[1] = (uint8_t)((_sample_rate >> 8) & 0xFF);
    freq[2] = (uint8_t)((_sample_rate >> 16) & 0xFF);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, freq, sizeof(freq));
  }
  log_w("Get EP request not handled, ctrlSel = %d, bRequest = %d, wLength = %d", ctrlSel, p_request->bRequest, p_request->wLength);
  return false;
#endif
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  dump_control_request(p_request);
#if TUD_OPT_HIGH_SPEED
  esp_usb_audio20_control_request_t const *request = (esp_usb_audio20_control_request_t const *)p_request;
  if (request->bEntityID == UAC2_ENTITY_CLOCK)
  {
    if (request->bControlSelector == AUDIO20_CS_CTRL_SAM_FREQ)
    {
      if (request->bRequest == AUDIO20_CS_REQ_CUR)
      {
        log_d("Clock get current freq %" PRIu32, _sample_rate);
        audio20_control_cur_4_t curf = {(int32_t)tu_htole32(_sample_rate)};
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
      }
      else if (request->bRequest == AUDIO20_CS_REQ_RANGE)
      {
        audio20_control_range_4_n_t(1) rangef = {};
        rangef.wNumSubRanges = tu_htole16(1);
        rangef.subrange[0].bMin = (int32_t)tu_htole32(_sample_rate);
        rangef.subrange[0].bMax = (int32_t)tu_htole32(_sample_rate);
        rangef.subrange[0].bRes = (int32_t)tu_htole32(0);
        log_d("Clock Range %" PRIu32 ", %" PRIu32 ", %d", _sample_rate, _sample_rate, 0);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
      }
    }
    else if (request->bControlSelector == AUDIO20_CS_CTRL_CLK_VALID && request->bRequest == AUDIO20_CS_REQ_CUR)
    {
      audio20_control_cur_1_t cur_valid = {.bCur = 1};
      log_d("Clock get is valid %u", cur_valid.bCur);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
    }
    log_w("Clock get request not supported, entity = %u, selector = %u, request = %u", request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
  else if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
  {
    uint8_t channel = request->bChannelNumber;
    if (channel != 0 && channel > _spk_channels)
    {
      log_w("Invalid speaker channel %u for feature unit get request (spk_channels=%u)", channel, _spk_channels);
      return false;
    }
    if (request->bControlSelector == AUDIO20_FU_CTRL_MUTE && request->bRequest == AUDIO20_CS_REQ_CUR)
    {
      audio20_control_cur_1_t mute1 = {.bCur = _mute[request->bChannelNumber]};
      log_d("Get channel %u mute %d", request->bChannelNumber, mute1.bCur);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
    }
    else if (request->bControlSelector == AUDIO20_FU_CTRL_VOLUME)
    {
      if (request->bRequest == AUDIO20_CS_REQ_RANGE)
      {
        audio20_control_range_2_n_t(1)
          range_vol = {.wNumSubRanges = tu_htole16(1), .subrange = {{.bMin = tu_htole16(-50 * 256), .bMax = tu_htole16(0), .bRes = tu_htole16(256)}}};
        log_d(
          "Get channel %u volume range (%d, %d, %u) dB", request->bChannelNumber, range_vol.subrange[0].bMin / 256, range_vol.subrange[0].bMax / 256,
          range_vol.subrange[0].bRes / 256
        );
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
      }
      else if (request->bRequest == AUDIO20_CS_REQ_CUR)
      {
        audio20_control_cur_2_t cur_vol = {.bCur = tu_htole16(_volume[request->bChannelNumber])};
        log_d("Get channel %u volume %d dB", request->bChannelNumber, cur_vol.bCur / 256);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
      }
    }
    log_w("Feature unit get request not supported, entity = %u, selector = %u, request = %u", request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
  log_w("Get request not handled, entity = %d, selector = %d, request = %d", request->bEntityID, request->bControlSelector, request->bRequest);
  return false;
#else
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT)
  {
    if (channelNum >= (sizeof(_mute) / sizeof(_mute[0])))
    {
      log_w("Invalid channel %u for feature unit get request", channelNum);
      return false;
    }
    if (ctrlSel == AUDIO10_FU_CTRL_MUTE)
    {
      uint8_t muted = _mute[channelNum];
      log_d("Get Mute of channel: %u", channelNum);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &muted, 1);
    }
    else if (ctrlSel == AUDIO10_FU_CTRL_VOLUME)
    {
      if (p_request->bRequest == AUDIO10_CS_REQ_GET_CUR)
      {
        log_d("Get Volume of channel: %u", channelNum);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &_volume[channelNum], sizeof(_volume[channelNum]));
      }
      else if (p_request->bRequest == AUDIO10_CS_REQ_GET_MIN)
      {
        log_d("Get Volume min of channel: %u", channelNum);
        int16_t vol_min = (int16_t)(-50 * 256);
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol_min, sizeof(vol_min));
      }
      else if (p_request->bRequest == AUDIO10_CS_REQ_GET_MAX)
      {
        log_d("Get Volume max of channel: %u", channelNum);
        int16_t vol_max = 0;
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol_max, sizeof(vol_max));
      }
      else if (p_request->bRequest == AUDIO10_CS_REQ_GET_RES)
      {
        log_d("Get Volume res of channel: %u", channelNum);
        int16_t vol_res = 256;  // 1 dB in 1/256 dB units
        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol_res, sizeof(vol_res));
      }
      else
      {
        log_w(
          "Get Volume request not supported, entityID = %d, ctrlSel = %d, bRequest = %d, wLength = %d", entityID, ctrlSel, p_request->bRequest,
          p_request->wLength
        );
        return false;
      }
    }
  }
  log_w("Get request not handled, entityID = %d, ctrlSel = %d, bRequest = %d, wLength = %d", entityID, ctrlSel, p_request->bRequest, p_request->wLength);
  return false;
#endif
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
  (void)rhport;
  dump_control_request(p_request);
#if TUD_OPT_HIGH_SPEED
  esp_usb_audio20_control_request_t const *request = (esp_usb_audio20_control_request_t const *)p_request;
  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT && request->bRequest == AUDIO20_CS_REQ_CUR)
  {
    if (request->bChannelNumber >= (sizeof(_mute) / sizeof(_mute[0])))
    {
      log_w("Invalid channel %u for feature unit set request", request->bChannelNumber);
      return false;
    }
    if (request->bControlSelector == AUDIO20_FU_CTRL_MUTE)
    {
      TU_VERIFY(request->wLength == sizeof(audio20_control_cur_1_t));
      _mute[request->bChannelNumber] = ((audio20_control_cur_1_t const *)buf)->bCur;
      log_d("Set channel %d Mute: %d", request->bChannelNumber, _mute[request->bChannelNumber]);
      EspUsbDeviceAudioEvent event;
      event.type = ESP_USB_DEVICE_AUDIO_EVENT_MUTE;
      event.channel = (EspUsbDeviceAudioChannel)request->bChannelNumber;
      event.muted = _mute[request->bChannelNumber];
      audioPostEvent(event);
      return true;
    }
    else if (request->bControlSelector == AUDIO20_FU_CTRL_VOLUME)
    {
      TU_VERIFY(request->wLength == sizeof(audio20_control_cur_2_t));
      _volume[request->bChannelNumber] = ((audio20_control_cur_2_t const *)buf)->bCur;
      log_d("Set channel %d volume: %d dB", request->bChannelNumber, _volume[request->bChannelNumber] / 256);
      EspUsbDeviceAudioEvent event;
      event.type = ESP_USB_DEVICE_AUDIO_EVENT_VOLUME;
      event.channel = (EspUsbDeviceAudioChannel)request->bChannelNumber;
      event.volumeDb = _volume[request->bChannelNumber] / 256;
      audioPostEvent(event);
      return true;
    }
    log_w("Feature unit set request not supported, entity = %u, selector = %u, request = %u", request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
  else if (request->bEntityID == UAC2_ENTITY_CLOCK && request->bRequest == AUDIO20_CS_REQ_CUR)
  {
    if (request->bControlSelector == AUDIO20_CS_CTRL_SAM_FREQ)
    {
      TU_VERIFY(request->wLength == sizeof(audio20_control_cur_4_t));
      _sample_rate = (uint32_t)((audio20_control_cur_4_t const *)buf)->bCur;
      log_d("Clock set current freq: %" PRIu32, _sample_rate);
      EspUsbDeviceAudioEvent event;
      event.type = ESP_USB_DEVICE_AUDIO_EVENT_SAMPLE_RATE;
      event.sampleRate = _sample_rate;
      audioPostEvent(event);
      return true;
    }
    log_w("Clock set request not supported, entity = %u, selector = %u, request = %u", request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
  log_w("Set request not handled, entity = %d, selector = %d, request = %d", request->bEntityID, request->bControlSelector, request->bRequest);
  return false;
#else
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT && p_request->bRequest == AUDIO10_CS_REQ_SET_CUR)
  {
    if (channelNum >= (sizeof(_mute) / sizeof(_mute[0])))
    {
      log_w("Invalid channel %u for feature unit set request", channelNum);
      return false;
    }
    if (ctrlSel == AUDIO10_FU_CTRL_MUTE && p_request->wLength == 1)
    {
      _mute[channelNum] = buf[0];
      log_d("Set Mute: %d of channel: %u", _mute[channelNum], channelNum);
      EspUsbDeviceAudioEvent event;
      event.type = ESP_USB_DEVICE_AUDIO_EVENT_MUTE;
      event.channel = (EspUsbDeviceAudioChannel)channelNum;
      event.muted = _mute[channelNum];
      audioPostEvent(event);
      return true;
    }
    else if (ctrlSel == AUDIO10_FU_CTRL_VOLUME && p_request->wLength == 2)
    {
      _volume[channelNum] = (int16_t)tu_unaligned_read16(buf);
      log_d("Set Volume: %d dB of channel: %u", _volume[channelNum] / 256, channelNum);
      EspUsbDeviceAudioEvent event;
      event.type = ESP_USB_DEVICE_AUDIO_EVENT_VOLUME;
      event.channel = (EspUsbDeviceAudioChannel)channelNum;
      event.volumeDb = _volume[channelNum] / 256;
      audioPostEvent(event);
      return true;
    }
  }
  log_w("Set request not handled, entityID = %d, ctrlSel = %d, bRequest = %d, wLength = %d", entityID, ctrlSel, p_request->bRequest, p_request->wLength);
  return false;
#endif
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  (void)rhport;
  dump_control_request(p_request);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex)) - _itf_num;
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  log_d("Close EP interface %d alt %d", itf, alt);
#endif
  return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  (void)rhport;
  dump_control_request(p_request);
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex)) - _itf_num;
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));
  log_d("Set interface %d alt %d", itf, alt);
  EspUsbDeviceAudioEvent event;
  event.type = ESP_USB_DEVICE_AUDIO_EVENT_INTERFACE;
  if (_spk_channels > 0 && itf == 1)
  {
    event.interface = ESP_USB_DEVICE_AUDIO_INTERFACE_SPEAKER;
  }
  else
  {
    event.interface = ESP_USB_DEVICE_AUDIO_INTERFACE_MIC;
  }
  event.enabled = alt != 0;
  audioPostEvent(event);
  return true;
}

#endif /* ESP_USB_DEVICE_AUDIO_IMPL */

EspUsbDeviceAudio::EspUsbDeviceAudio(EspUsbDevice &device,
                                     uint32_t sampleRate,
                                     EspUsbDeviceAudioBitsPerSample bitsPerSample,
                                     EspUsbDeviceAudioSpeakerChannels speakerChannels,
                                     EspUsbDeviceAudioMicChannels micChannels) : EspUsbDeviceClass(device)
{
  sampleRate_ = sampleRate;
  bitsPerSample_ = bitsPerSample;
  speakerChannels_ = speakerChannels;
  micChannels_ = micChannels;
}

EspUsbDeviceAudio::~EspUsbDeviceAudio()
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  if (_receiveTaskHandle != NULL)
  {
    vTaskDelete(_receiveTaskHandle);
    _receiveTaskHandle = NULL;
  }
  if (_spk_buf != NULL)
  {
    free(_spk_buf);
    _spk_buf = NULL;
  }
  if (g_activeAudio == this)
  {
    g_activeAudio = nullptr;
  }
#endif
}

bool EspUsbDeviceAudio::begin()
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  if (g_activeAudio != nullptr && g_activeAudio != this)
  {
    return false;
  }
  if ((uint8_t)speakerChannels_ > 2)
  {
    log_e("Maximum of 2 speaker channels supported!");
    return false;
  }

  _sample_rate = sampleRate_;
  _bits_per_sample = (uint8_t)bitsPerSample_;
  _bytes_per_sample = (_bits_per_sample <= 16) ? 2 : 4;
  _spk_channels = (uint8_t)speakerChannels_;
  _mic_channels = (uint8_t)micChannels_;

  uint16_t descriptor_len = 0;
#if TUD_OPT_HIGH_SPEED
  if (_sample_rate > CFG_TUD_AUDIO_MAX_SAMPLE_RATE)
  {
    log_e("Maximum %u sample rate supported!", CFG_TUD_AUDIO_MAX_SAMPLE_RATE);
    return false;
  }
  if (_spk_channels == 2 && _mic_channels > 0)
  {
    descriptor_len = TUD_AUDIO20_HEADSET_STEREO_DESC_LEN;
  }
  else if (_spk_channels == 1 && _mic_channels > 0)
  {
    descriptor_len = TUD_AUDIO20_HEADSET_MONO_DESC_LEN;
  }
  else if (_spk_channels == 2 && _mic_channels == 0)
  {
    descriptor_len = TUD_AUDIO20_SPEAKER_STEREO_DESC_LEN;
  }
  else if (_spk_channels == 1 && _mic_channels == 0)
  {
    descriptor_len = TUD_AUDIO20_SPEAKER_MONO_DESC_LEN;
  }
  else if (_spk_channels == 0 && _mic_channels > 0)
  {
    descriptor_len = TUD_AUDIO20_MICROPHONE_DESC_LEN;
  }
#else
  if (_sample_rate > 48000)
  {
    log_e("Maximum 48000 sample rate supported!");
    return false;
  }
  if (((_spk_channels + _mic_channels) * _bytes_per_sample) > 8)
  {
    log_e("Too many channels or too high bits per sample selected! Audio might not work!");
  }
  if (_spk_channels == 2 && _mic_channels > 0)
  {
    descriptor_len = TUD_AUDIO10_HEADSET_STEREO_DESC_LEN(1);
  }
  else if (_spk_channels == 1 && _mic_channels > 0)
  {
    descriptor_len = TUD_AUDIO10_HEADSET_MONO_DESC_LEN(1);
  }
  else if (_spk_channels == 2 && _mic_channels == 0)
  {
    descriptor_len = TUD_AUDIO10_SPEAKER_STEREO_DESC_LEN(1);
  }
  else if (_spk_channels == 1 && _mic_channels == 0)
  {
    descriptor_len = TUD_AUDIO10_SPEAKER_MONO_DESC_LEN(1);
  }
  else if (_spk_channels == 0 && _mic_channels > 0)
  {
    descriptor_len = TUD_AUDIO10_MICROPHONE_DESC_LEN(1);
  }
#endif

  tinyusb_enable_interface(USB_INTERFACE_AUDIO, descriptor_len, tusb_audio_load_descriptor);
  (void)USB;
  g_activeAudio = this;

  esp_event_loop_handle_t loop = audioEventLoop();
  if (loop != NULL)
  {
    esp_event_handler_register_with(loop, ESP_USB_DEVICE_AUDIO_EVENTS, ESP_EVENT_ANY_ID, audioEventDispatch, nullptr);
  }
  return true;
#else
  device_.setLastError(ESP_ERR_NOT_SUPPORTED);
  return false;
#endif
}

bool EspUsbDeviceAudio::afterDeviceStarted()
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  if (_spk_channels > 0 && _spk_buf == NULL)
  {
    _spk_buf = (int32_t *)malloc(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ);
    if (_spk_buf == NULL)
    {
      log_e("Failed to allocate speaker data buffer!");
      return false;
    }
    BaseType_t created = xTaskCreate(audioReceiveTask, "uacReceiveTask", 8092, this, 5, &_receiveTaskHandle);
    if (created != pdPASS || _receiveTaskHandle == NULL)
    {
      log_e("Failed to create receive task!");
      free(_spk_buf);
      _spk_buf = NULL;
      return false;
    }
  }
  return true;
#else
  return false;
#endif
}

uint16_t EspUsbDeviceAudio::configurationDescriptor(uint8_t *dst, uint8_t interfaceNumber, uint8_t endpointNumber, uint16_t endpointSize)
{
  (void)dst;
  (void)interfaceNumber;
  (void)endpointNumber;
  (void)endpointSize;
  return 0;
}

void EspUsbDeviceAudio::onData(EspUsbDeviceAudioDataCallback callback)
{
  dataCallback_ = callback;
}

void EspUsbDeviceAudio::onPcm(EspUsbDeviceAudioPcmCallback callback)
{
  pcmCallback_ = callback;
}

void EspUsbDeviceAudio::onEvent(EspUsbDeviceAudioEventCallback callback)
{
  eventCallback_ = callback;
}

uint16_t EspUsbDeviceAudio::writeMic(const void *data, uint16_t length)
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  return g_activeAudio == this ? tud_audio_write(data, length) : 0;
#else
  (void)data;
  (void)length;
  return 0;
#endif
}

// Apply currently set channel volumes directly to audio data (if the DAC does
// not support setting volume).
void EspUsbDeviceAudio::applyVolume(void *data, uint16_t length)
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  if (_spk_channels == 0)
  {
    return;
  }
  // Convert volume from 1/256 dB to linear multiplier (Q16 fixed-point).
  // _volume[0] ranges from -12800 (-50 dB) to 0 (0 dB) in 1/256 dB units.
  if (_mute[0])
  {
    memset(data, 0, length);
  }
  else if (_volume[0] != 0)
  {
    int32_t vol_mul = _mute[0] ? 0 : (int32_t)(powf(10.0f, _volume[0] / 256.0f / 20.0f) * 65536.0f);
    if (_bits_per_sample == 16)
    {
      int16_t *src = (int16_t *)data;
      int16_t *limit = (int16_t *)data + length / 2;
      while (src < limit)
      {
        *src = (int16_t)(((int32_t)*src * vol_mul) >> 16);
        src++;
      }
    }
    else
    {
      int32_t *src = (int32_t *)data;
      int32_t *limit = (int32_t *)data + length / 4;
      while (src < limit)
      {
        *src = (int32_t)(((int64_t)*src * vol_mul) >> 16);
        src++;
      }
    }
  }

  // Apply volume to secondary channels
  if (_volume[1] != 0 || _volume[2] != 0 || _mute[1] || _mute[2])
  {
    int32_t vol_mul1 = _mute[1] ? 0 : (int32_t)(powf(10.0f, _volume[1] / 256.0f / 20.0f) * 65536.0f);
    int32_t vol_mul2 = _mute[2] ? 0 : (int32_t)(powf(10.0f, _volume[2] / 256.0f / 20.0f) * 65536.0f);
    if (_bits_per_sample == 16)
    {
      int16_t *src = (int16_t *)data;
      int16_t *limit = (int16_t *)data + length / 2;
      while (src < limit)
      {
        *src = (int16_t)(((int32_t)*src * vol_mul1) >> 16);
        src++;
        if (_spk_channels == 2)
        {
          *src = (int16_t)(((int32_t)*src * vol_mul2) >> 16);
          src++;
        }
      }
    }
    else
    {
      int32_t *src = (int32_t *)data;
      int32_t *limit = (int32_t *)data + length / 4;
      while (src < limit)
      {
        *src = (int32_t)(((int64_t)*src * vol_mul1) >> 16);
        src++;
        if (_spk_channels == 2)
        {
          *src = (int32_t)(((int64_t)*src * vol_mul2) >> 16);
          src++;
        }
      }
    }
  }
#else
  (void)data;
  (void)length;
#endif
}

bool EspUsbDeviceAudio::mute(EspUsbDeviceAudioChannel channel) const
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  if (_spk_channels == 0 || (uint8_t)channel > _spk_channels)
  {
    log_e("Bad speaker channel %u", (uint8_t)channel);
    return false;
  }
  return _mute[channel];
#else
  (void)channel;
  return false;
#endif
}

bool EspUsbDeviceAudio::mute(EspUsbDeviceAudioChannel channel, bool muted)
{
#if ESP_USB_DEVICE_AUDIO_IMPL && CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP
  if (_spk_channels == 0 || (uint8_t)channel > _spk_channels)
  {
    log_e("Bad speaker channel %u", (uint8_t)channel);
    return false;
  }

  _mute[channel] = muted;

  // 6.1 Interrupt Data Message
  const audio_interrupt_data_t data =
    {.v2 = {
       .bInfo = 0,                                        // Class-specific interrupt, originated from an interface
       .bAttribute = AUDIO20_CS_REQ_CUR,                  // Caused by current settings
       .wValue_cn_or_mcn = (uint8_t)channel,              // CH0: master volume
       .wValue_cs = AUDIO20_FU_CTRL_MUTE,                 // Volume change
       .wIndex_ep_or_int = 0,                             // From the interface itself
       .wIndex_entity_id = UAC2_ENTITY_SPK_FEATURE_UNIT,  // From feature unit
     }};

  if (tud_audio_int_write(&data))
  {
    EspUsbDeviceAudioEvent event;
    event.type = ESP_USB_DEVICE_AUDIO_EVENT_MUTE;
    event.channel = channel;
    event.muted = _mute[channel];
    audioPostEvent(event);
    return true;
  }
  return false;
#else
  (void)channel;
  (void)muted;
  return false;
#endif
}

int8_t EspUsbDeviceAudio::volume(EspUsbDeviceAudioChannel channel) const
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  if (_spk_channels == 0 || (uint8_t)channel > _spk_channels)
  {
    log_e("Bad speaker channel %u", (uint8_t)channel);
    return INT8_MIN;
  }
  return _volume[channel] / 256;
#else
  (void)channel;
  return 0;
#endif
}

bool EspUsbDeviceAudio::volume(EspUsbDeviceAudioChannel channel, int8_t volumeDb)
{
#if ESP_USB_DEVICE_AUDIO_IMPL && CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP
  if (_spk_channels == 0 || (uint8_t)channel > _spk_channels)
  {
    log_e("Bad speaker channel %u", (uint8_t)channel);
    return false;
  }
  _volume[channel] = volumeDb * 256;

  // 6.1 Interrupt Data Message
  const audio_interrupt_data_t data =
    {.v2 = {
       .bInfo = 0,                                        // Class-specific interrupt, originated from an interface
       .bAttribute = AUDIO20_CS_REQ_CUR,                  // Caused by current settings
       .wValue_cn_or_mcn = (uint8_t)channel,              // CH0: master volume
       .wValue_cs = AUDIO20_FU_CTRL_VOLUME,               // Volume change
       .wIndex_ep_or_int = 0,                             // From the interface itself
       .wIndex_entity_id = UAC2_ENTITY_SPK_FEATURE_UNIT,  // From feature unit
     }};

  if (tud_audio_int_write(&data))
  {
    EspUsbDeviceAudioEvent event;
    event.type = ESP_USB_DEVICE_AUDIO_EVENT_VOLUME;
    event.channel = channel;
    event.volumeDb = _volume[channel] / 256;
    audioPostEvent(event);
    return true;
  }
  return false;
#else
  (void)channel;
  (void)volumeDb;
  return false;
#endif
}

uint32_t EspUsbDeviceAudio::sampleRate() const
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  return _sample_rate;
#else
  return sampleRate_;
#endif
}

uint8_t EspUsbDeviceAudio::bytesPerSample() const
{
#if ESP_USB_DEVICE_AUDIO_IMPL
  return _bytes_per_sample;
#else
  return bitsPerSample_ <= ESP_USB_DEVICE_AUDIO_BITS_16 ? 2 : 4;
#endif
}

EspUsbDeviceAudioBitsPerSample EspUsbDeviceAudio::bitsPerSample() const
{
  return bitsPerSample_;
}

EspUsbDeviceAudioSpeakerChannels EspUsbDeviceAudio::speakerChannels() const
{
  return speakerChannels_;
}

EspUsbDeviceAudioMicChannels EspUsbDeviceAudio::micChannels() const
{
  return micChannels_;
}

void EspUsbDeviceAudio::handleData(void *data, uint16_t length)
{
  if (dataCallback_)
  {
    dataCallback_(data, length);
  }
  if (pcmCallback_)
  {
    EspUsbDeviceAudioPcm pcm;
    pcm.data = data;
    pcm.length = length;
    pcm.sampleRate = sampleRate();
    pcm.channels = static_cast<uint8_t>(speakerChannels_);
    pcm.bytesPerSample = bytesPerSample();
    pcm.bitsPerSample = bitsPerSample_;
    pcm.interface = ESP_USB_DEVICE_AUDIO_INTERFACE_SPEAKER;
    pcmCallback_(pcm);
  }
}

void EspUsbDeviceAudio::handleEvent(const EspUsbDeviceAudioEvent &event)
{
  if (eventCallback_)
  {
    eventCallback_(event);
  }
}
