#pragma once
// Legacy build knobs are not a second configuration path.
#if defined(CFG_TUD_AUDIO) || \
    defined(CFG_TUD_AUDIO_CTRL_BUF_SZ) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ) || \
    defined(CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) || \
    defined(CFG_TUD_CDC) || \
    defined(CFG_TUD_CDC_RX_BUFSIZE) || \
    defined(CFG_TUD_CDC_TX_BUFSIZE) || \
    defined(CFG_TUD_DFU) || \
    defined(CFG_TUD_DFU_RUNTIME) || \
    defined(CFG_TUD_DWC2_DMA_ENABLE) || \
    defined(CFG_TUD_DWC2_SLAVE_ENABLE) || \
    defined(CFG_TUD_ENDPOINT0_BUFSIZE) || \
    defined(CFG_TUD_ENDPOINT0_SIZE) || \
    defined(CFG_TUD_ENDPPOINT_MAX) || \
    defined(CFG_TUD_HID) || \
    defined(CFG_TUD_HID_BUFSIZE) || \
    defined(CFG_TUD_HID_EP_BUFSIZE) || \
    defined(CFG_TUD_INTERFACE_MAX) || \
    defined(CFG_TUD_MIDI) || \
    defined(CFG_TUD_MIDI_EP_BUFSIZE) || \
    defined(CFG_TUD_MIDI_RX_BUFSIZE) || \
    defined(CFG_TUD_MIDI_RX_EPSIZE) || \
    defined(CFG_TUD_MIDI_TX_BUFSIZE) || \
    defined(CFG_TUD_MIDI_TX_EPSIZE) || \
    defined(CFG_TUD_MSC) || \
    defined(CFG_TUD_NCM) || \
    defined(CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB) || \
    defined(CFG_TUD_NCM_IN_NTB_MAX_SIZE) || \
    defined(CFG_TUD_NCM_IN_NTB_N) || \
    defined(CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB) || \
    defined(CFG_TUD_NCM_OUT_NTB_MAX_SIZE) || \
    defined(CFG_TUD_NCM_OUT_NTB_N) || \
    defined(CFG_TUD_NET_MTU) || \
    defined(CFG_TUD_TASK_EVENTS_PER_RUN) || \
    defined(CFG_TUD_TASK_QUEUE_SZ) || \
    defined(CFG_TUD_TEST_MODE) || \
    defined(CFG_TUD_VBUS_DETECT_HW) || \
    defined(CFG_TUD_VBUS_DETECT_HW_DEFAULT) || \
    defined(CFG_TUD_VENDOR_RX_BUFFERED) || \
    defined(CFG_TUD_VENDOR_RX_BUFSIZE) || \
    defined(CFG_TUD_VENDOR_TX_BUFFERED) || \
    defined(CFG_TUD_VENDOR_TX_BUFSIZE) || \
    defined(CFG_TUD_VIDEO) || \
    defined(CFG_TUD_VIDEO_STREAMING) || \
    defined(CFG_TUD_VIDEO_STREAMING_BULK) || \
    defined(CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE) || \
    defined(CFG_TUSB_DEBUG) || \
    defined(CFG_TUSB_MCU) || \
    defined(CFG_TUSB_OS) || \
    defined(ESP_USB_TINYUSB_AUDIO_MAX_SAMPLE_RATE) || \
    defined(ESP_USB_TINYUSB_AUDIO_SW_PACKETS) || \
    defined(CFG_TUD_DFU_XFER_BUFSIZE) || \
    defined(CFG_TUD_MSC_EP_BUFSIZE)
#error "Remove TinyUSB build flags; configure EspUsbDevice/Function runtime APIs instead"
#endif
