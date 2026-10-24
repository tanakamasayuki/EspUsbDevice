// en: Request the chip ROM loader from CDC or DFU runtime.
// ja: CDCやDFU runtimeからチップのROM loaderへ移行する。
// en: Wiring, operation and adaptation notes: README.md.
// ja: 配線・操作・改造時の注意: README.ja.md。

#include "EspUsbDevice.h"


// en: Keep the USB owner and its function objects alive for the whole session.
// ja: USB全体と各機能のオブジェクトを、接続の使用中ずっと残す。
EspUsbDevice device;
EspUsbDeviceCdcSerial port(device, "Console");
EspUsbDeviceDfu dfu(device, EspUsbDeviceDfuMode::Runtime, "Bootloader");

// en: Set from USB callbacks, acted on in loop(). Every class callback in this library runs on the usbd task, and restarting the chip from inside one cuts off the transfer the host is still finishing.
// ja: USB callbackでは要求を記録し、loop()で再起動する。callback内で待たない。
static volatile bool bootloaderRequested = false;

void setup()
{
  // en: Serial is the diagnostic path; confirm its board-specific route separately from USB data.
  // ja: Serialは診断用。基板のどの経路から読めるかを、USBデータ接続とは別に確認する。
  Serial.begin(115200);
  delay(1500);

  // en: The 1200-baud touch. A host that merely opens the port at some other speed is not asking for anything, so nothing happens.
  // ja: この例の1200 baud設定をboot移行の要求として扱う。通常のport開始とは分ける。
  port.onLineCoding([](const EspUsbDeviceCdcLineCoding &coding)
                    {
                      if (coding.baud == 1200)
                      {
                        bootloaderRequested = true;
                      }
                    });

  // en: This callback runs on the USB task; keep work bounded and hand larger processing to the application.
  // ja: USBタスクで動くcallback。処理を短く保ち、大きな仕事はアプリ側へ渡す。
  port.onRx([](size_t)
            {
              // en: Read only bytes already buffered; application message boundaries must be tracked separately.
              // ja: bufferに届いたbyteを読む。アプリのメッセージの区切りは別に管理する。
              while (port.available() > 0)
              {
                if (port.read() == 'b')
                {
                  bootloaderRequested = true;
                }
              }
            });

  // en: Without a callback, EspUsbDeviceDfu in Runtime mode restarts into the loader by itself, which is what a host that sent DFU_DETACH asked for. Taking the callback here only adds the log line - and shows where a sketch would refuse, or save state first.
  // ja: Runtime DFUのdetach判断を置き換え、再起動自体はloop()へ渡す。
  dfu.onDetach([]()
               {
                 Serial.println("DFU_DETACH received");
                 bootloaderRequested = true;
               });

  // en: Start all declared functions together; the host configures the device afterwards.
  // ja: 宣言した機能をまとめて開始する。ホストによる認識と設定はその後に進む。
  if (!device.begin())
  {
    Serial.printf("USB_BEGIN_FAILED %s\n", device.lastErrorName());
    return;
  }
  Serial.println("Ready. Touch the CDC port at 1200 baud, run dfu-util -e, or send 'b'.");
}

void loop()
{
  if (bootloaderRequested)
  {
    bootloaderRequested = false;
    Serial.println("Restarting into the ROM download loader");
    Serial.flush();
    // en: Detaches the USB device, sets the target's download-boot flag and restarts. Does not return.
    // ja: USBを切断して次の起動modeを設定する。復旧・再接続はチップのloader経路で行う。
    device.rebootToBootloader();
    // en: Only reached on a target with no such register, which none of the chips this library supports is.
    // ja: 移行できないtargetのための到達経路。失敗時もメイン処理を止め続けない。
    Serial.printf("REBOOT_UNSUPPORTED %s\n", device.lastErrorName());
  }
  delay(20);
}
