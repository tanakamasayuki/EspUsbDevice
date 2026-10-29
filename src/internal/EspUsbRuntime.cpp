#include "EspUsbRuntime.h"
#include "EspUsbDeviceCore.h"

#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <atomic>
#include "EspUsbController.h"
#include "EspUsbDwc2Platform.h"

namespace {

usb_phy_handle_t g_phy = nullptr;
TaskHandle_t g_deviceTask = nullptr;
uint8_t g_rhport = 0xff;
std::atomic<bool> g_stop{false};
SemaphoreHandle_t g_taskExited = nullptr;
using espusb::internal::UsbRuntimeJob;
UsbRuntimeJob *g_jobs = nullptr, *g_jobsTail = nullptr;
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

bool postUsbRuntimeJob(UsbRuntimeJob &job)
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

void removeUsbRuntimeJob(UsbRuntimeJob &job)
{
  portENTER_CRITICAL(&g_jobsLock);
  auto **link = &g_jobs;
  UsbRuntimeJob *previous = nullptr;
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

esp_err_t startUsbRuntime(const UsbRuntimeOptions &options)
{
  if (g_phy || g_deviceTask)
  {
    return ESP_ERR_INVALID_STATE;
  }

  // Reject unusable worker settings before acquiring a PHY or controller.
  if (options.taskStackBytes < uint32_t(configMINIMAL_STACK_SIZE) || options.taskStackBytes > uint32_t(PTRDIFF_MAX)) return ESP_ERR_INVALID_SIZE;
  if (options.taskPriority < -1 || (options.taskPriority >= 0 && unsigned(options.taskPriority) >= configMAX_PRIORITIES) ||
      options.taskCoreId < -1 || (options.taskCoreId >= 0 && unsigned(options.taskCoreId) >= configNUMBER_OF_CORES)) return ESP_ERR_INVALID_ARG;

  uint8_t port = 255;
  if (options.controller == UsbController::Auto) {
    for (uint8_t i=0; i<ESPUSB_DWC2_PORT_COUNT; ++i) if (espusb_dwc2_ports[i].highSpeed) { port=i; break; }
    if (port==255) port=0;
  } else if (options.controller == UsbController::FullSpeed || options.controller == UsbController::HighSpeed) {
    const bool high = options.controller == UsbController::HighSpeed;
    for (uint8_t i=0; i<ESPUSB_DWC2_PORT_COUNT; ++i) if (espusb_dwc2_ports[i].highSpeed==high) { port=i; break; }
  }
  if (options.controller != UsbController::Auto && options.controller != UsbController::FullSpeed && options.controller != UsbController::HighSpeed) return ESP_ERR_INVALID_ARG;
  const auto *capabilities = espusb_dwc2_port(port);
  if (!capabilities) return ESP_ERR_NOT_SUPPORTED;
  const bool highSpeed = capabilities->highSpeed;

  usb_phy_config_t phyConfig = {
      .controller = USB_PHY_CTRL_OTG,
      .target = highSpeed ? USB_PHY_TARGET_UTMI : USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = highSpeed ? USB_PHY_SPEED_HIGH : USB_PHY_SPEED_FULL,
      .ext_io_conf = nullptr,
      .otg_io_conf = nullptr,
  };

  esp_err_t err = usb_new_phy(&phyConfig, &g_phy);
  if (err != ESP_OK)
  {
    g_phy = nullptr;
    return err;
  }

  g_rhport = port;

  // The native controller snapshots runtime FIFO/VBUS policy before init.
  const EspUsbControllerConfig dwc2Config = {
      .bulkInDoubleBuffered = options.bulkInDoubleBuffered,
      .vbusSensing = options.vbusSensing,
  };
  if (!espusb_controller_configure(g_rhport, &dwc2Config))
  {
    usb_del_phy(g_phy);
    g_phy = nullptr;
    g_rhport = 0xff;
    return ESP_ERR_INVALID_STATE;
  }

  const esp_err_t coreError = startUsbDeviceCore(g_rhport, highSpeed, options.eventQueueDepth);
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
      options.taskPriority < 0 ? configMAX_PRIORITIES - 1 : static_cast<UBaseType_t>(options.taskPriority);
  // xTaskCreate() leaves the task unpinned, which on a dual-core ESP32 means it
  // can run beside whatever else is busy. Pinning it away from the core the
  // sketch produces data on is what keeps the two from taking slices from each
  // other; which core that is belongs to the sketch, so this only does what it
  // was told.
  const BaseType_t created =
      options.taskCoreId < 0
          ? xTaskCreate(usbDeviceTask, "espusb-device", options.taskStackBytes,
                        nullptr, priority, &g_deviceTask)
          : xTaskCreatePinnedToCore(usbDeviceTask, "espusb-device",
                                    options.taskStackBytes, nullptr, priority,
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

bool usbRuntimeInTask()
{
  return g_deviceTask && xTaskGetCurrentTaskHandle() == g_deviceTask;
}

bool stopUsbRuntime()
{
  if (usbRuntimeInTask()) return false;
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

bool usbRuntimeStopping() { return g_stop.load(); }

bool usbRuntimeStarted()
{
  return g_phy != nullptr && g_deviceTask != nullptr;
}

uint8_t usbRuntimeRhport()
{
  return g_rhport;
}

} // namespace internal
} // namespace espusb
