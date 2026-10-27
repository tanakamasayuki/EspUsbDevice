#include "EspUsbTinyUsbRuntime.h"
#include "EspUsbDeviceCore.h"

#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include "tusb.h"

namespace {

usb_phy_handle_t g_phy = nullptr;
TaskHandle_t g_deviceTask = nullptr;
uint8_t g_rhport = 0xff;
std::atomic<bool> g_stop{false};
SemaphoreHandle_t g_taskExited = nullptr;
using espusb::internal::TinyUsbRuntimeJob;
TinyUsbRuntimeJob *g_jobs = nullptr, *g_jobsTail = nullptr;
portMUX_TYPE g_jobsLock = portMUX_INITIALIZER_UNLOCKED;

void runJobs()
{
  portENTER_CRITICAL(&g_jobsLock);
  auto *jobs = g_jobs;
  g_jobs = g_jobsTail = nullptr;
  portEXIT_CRITICAL(&g_jobsLock);
  while (jobs)
  {
    portENTER_CRITICAL(&g_jobsLock);
    auto *next = jobs->next;
    jobs->next = nullptr;
    jobs->queued = false;
    portEXIT_CRITICAL(&g_jobsLock);
    jobs->run(jobs->context);
    jobs = next;
  }
}

void usbDeviceTask(void *)
{
  while (!g_stop.load())
  {
    // Bounded event draining leaves time for producer jobs while quiet USB
    // waits without spinning. Reset events must retire the previous generation
    // before those jobs are allowed to touch endpoints again.
    espusb::internal::runUsbDeviceCore(1);
    if (espusb::internal::usbDeviceCoreReadyForJobs()) runJobs();
  }
  // DMA is stopped before class deinit returns borrowed buffers. Keep those
  // completion callbacks on the USB worker, including the end() path.
  espusb::internal::stopUsbDeviceCore();
  g_rhport = 0xff;
  xSemaphoreGive(g_taskExited);
  vTaskDelete(nullptr);
}

} // namespace

namespace espusb {
namespace internal {

bool postTinyUsbRuntimeJob(TinyUsbRuntimeJob &job)
{
  portENTER_CRITICAL(&g_jobsLock);
  const bool active = g_deviceTask && !g_stop.load();
  if (active && !job.queued)
  {
    job.next = nullptr;
    job.queued = true;
    if (g_jobsTail) g_jobsTail->next = &job;
    else g_jobs = &job;
    g_jobsTail = &job;
  }
  portEXIT_CRITICAL(&g_jobsLock);
  return active;
}

void removeTinyUsbRuntimeJob(TinyUsbRuntimeJob &job)
{
  portENTER_CRITICAL(&g_jobsLock);
  auto **link = &g_jobs;
  TinyUsbRuntimeJob *previous = nullptr;
  while (*link && *link != &job) { previous = *link; link = &(*link)->next; }
  if (*link)
  {
    *link = job.next;
    if (g_jobsTail == &job) g_jobsTail = previous;
    job.next = nullptr;
    job.queued = false;
  }
  portEXIT_CRITICAL(&g_jobsLock);
}

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

  // Configure before starting the independent core: DWC2 reads this once at
  // dfifo_device_init(), and the transmit FIFO sizes are fixed from then on.
  // vbus_sensing keeps the value the default configuration would have used,
  // because this call replaces the whole structure rather than merging into it.
  const tud_configure_dwc2_t dwc2Config = {
      .bm_double_buffered = options.bulkInDoubleBuffered,
      .vbus_sensing = CFG_TUD_VBUS_DETECT_HW,
  };
  tud_configure(g_rhport, TUD_CFGID_DWC2, &dwc2Config);

  const esp_err_t coreError = startUsbDeviceCore(g_rhport, selected == UsbController::HighSpeed, options.eventQueueDepth);
  if (coreError != ESP_OK)
  {
    usb_del_phy(g_phy);
    g_phy = nullptr;
    g_rhport = 0xff;
    return coreError;
  }

  g_taskExited = xSemaphoreCreateBinary();
  if (!g_taskExited)
  {
    espusb::internal::stopUsbDeviceCore();
    usb_del_phy(g_phy);
    g_phy = nullptr;
    g_rhport = 0xff;
    return ESP_ERR_NO_MEM;
  }
  g_stop.store(false);
  const UBaseType_t priority =
      options.taskPriority == 0 ? configMAX_PRIORITIES - 1 : options.taskPriority;
  // xTaskCreate() leaves the task unpinned, which on a dual-core ESP32 means it
  // can run beside whatever else is busy. Pinning it away from the core the
  // sketch produces data on is what keeps the two from taking slices from each
  // other; which core that is belongs to the sketch, so this only does what it
  // was told.
  const BaseType_t created =
      options.taskCoreId < 0
          ? xTaskCreate(usbDeviceTask, "espusb-device", options.taskStackSize,
                        nullptr, priority, &g_deviceTask)
          : xTaskCreatePinnedToCore(usbDeviceTask, "espusb-device",
                                    options.taskStackSize, nullptr, priority,
                                    &g_deviceTask,
                                    static_cast<BaseType_t>(options.taskCoreId));
  if (created != pdPASS)
  {
    vSemaphoreDelete(g_taskExited);
    g_taskExited = nullptr;
    espusb::internal::stopUsbDeviceCore();
    usb_del_phy(g_phy);
    g_phy = nullptr;
    g_rhport = 0xff;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

bool tinyUsbRuntimeInTask()
{
  return g_deviceTask && xTaskGetCurrentTaskHandle() == g_deviceTask;
}

bool stopTinyUsbRuntime()
{
  if (tinyUsbRuntimeInTask()) return false;
  if (g_deviceTask)
  {
    // Finish the current event/callback before deinitializing endpoints or
    // releasing class buffers. Deleting a running task can abandon a lock.
    g_stop.store(true);
    xSemaphoreTake(g_taskExited, portMAX_DELAY);
    g_deviceTask = nullptr;
    vSemaphoreDelete(g_taskExited);
    g_taskExited = nullptr;
  }
  if (g_rhport != 0xff)
  {
    espusb::internal::stopUsbDeviceCore();
    g_rhport = 0xff;
  }
  if (g_phy)
  {
    usb_del_phy(g_phy);
    g_phy = nullptr;
  }
  return true;
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
