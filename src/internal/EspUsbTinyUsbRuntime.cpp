#include "EspUsbTinyUsbRuntime.h"

#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

namespace {

usb_phy_handle_t g_phy = nullptr;
TaskHandle_t g_deviceTask = nullptr;
uint8_t g_rhport = 0xff;

void tinyUsbDeviceTask(void *)
{
  while (true)
  {
    tud_task();
  }
}

} // namespace

namespace espusb {
namespace internal {

esp_err_t startTinyUsbRuntime(const TinyUsbRuntimeOptions &options)
{
  if (g_phy || g_deviceTask)
  {
    return ESP_ERR_INVALID_STATE;
  }

  UsbController selected = options.controller;
  if (selected == UsbController::Auto)
  {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
    selected = UsbController::HighSpeed;
#else
    selected = UsbController::FullSpeed;
#endif
  }

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
  if (selected != UsbController::FullSpeed)
  {
    return ESP_ERR_NOT_SUPPORTED;
  }
#endif

  usb_phy_config_t phyConfig = {
      .controller = USB_PHY_CTRL_OTG,
      .target = selected == UsbController::HighSpeed ? USB_PHY_TARGET_UTMI : USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = selected == UsbController::HighSpeed ? USB_PHY_SPEED_HIGH : USB_PHY_SPEED_FULL,
      .ext_io_conf = nullptr,
      .otg_io_conf = nullptr,
  };

  esp_err_t err = usb_new_phy(&phyConfig, &g_phy);
  if (err != ESP_OK)
  {
    g_phy = nullptr;
    return err;
  }

  g_rhport = selected == UsbController::HighSpeed ? 1 : 0;
  const tusb_rhport_init_t init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = selected == UsbController::HighSpeed ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
  };
  if (!tusb_init(g_rhport, &init))
  {
    usb_del_phy(g_phy);
    g_phy = nullptr;
    g_rhport = 0xff;
    return ESP_FAIL;
  }

  const UBaseType_t priority =
      options.taskPriority == 0 ? configMAX_PRIORITIES - 1 : options.taskPriority;
  if (xTaskCreate(tinyUsbDeviceTask, "espusb-device", options.taskStackSize,
                  nullptr, priority, &g_deviceTask) != pdPASS)
  {
    tusb_deinit(g_rhport);
    usb_del_phy(g_phy);
    g_phy = nullptr;
    g_rhport = 0xff;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

void stopTinyUsbRuntime()
{
  if (g_deviceTask)
  {
    vTaskDelete(g_deviceTask);
    g_deviceTask = nullptr;
  }
  if (g_rhport != 0xff)
  {
    tusb_deinit(g_rhport);
    g_rhport = 0xff;
  }
  if (g_phy)
  {
    usb_del_phy(g_phy);
    g_phy = nullptr;
  }
}

bool tinyUsbRuntimeStarted()
{
  return g_phy != nullptr && g_deviceTask != nullptr;
}

uint8_t tinyUsbRuntimeRhport()
{
  return g_rhport;
}

} // namespace internal
} // namespace espusb
