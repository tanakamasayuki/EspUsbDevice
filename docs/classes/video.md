# USB Video (UVC)

> 日本語版: [video.ja.md](video.ja.md)

`EspUsbDeviceVideo` is the Function for a USB Video Class (UVC 1.5) camera.
It is device-to-host only: it sends frames (MJPEG or YUY2) that the application prepares.
The library borrows a frame until it has been sent and returns it once through a callback, however the transfer ends.
The library uses its own driver, not TinyUSB's Video class.

## Overview and when to use it

- To show ESP32 video as a camera in PC video-call applications or a browser (`getUserMedia()`).
- To stream a test pattern or screen contents in place of a camera.

What it does and does not do:

| Item | Behaviour |
| --- | --- |
| Formats | MJPEG or YUY2 (uncompressed); one format per Function |
| Size and rate | One frame size and one fixed frame rate are offered to the host |
| Transport | Isochronous (default) or Bulk, chosen per Function at run time |
| JPEG encoding | None. Pass JPEG produced by an encoder or sensor |
| Still images, camera controls (exposure and so on) | Not declared |
| Frames borrowed at once | One per Function |

## Basic usage

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceVideo camera(device, "Camera"); // the name is shown by the host

void setup() {
  EspUsbVideoConfig config;                 // default: Isochronous, 512-byte payloads
  if (camera.configure(config) != ESP_OK) return;
  camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg);
  camera.setFrameSize(320, 240);
  camera.setFrameRate(15);
  camera.setMaxFrameSize(32000);            // upper bound of one MJPEG frame (bytes)
  camera.onFrameResult([](const EspUsbTransferResult &result) {
    // result.buffer comes back here however the frame ended; reuse it from now on
  });
  if (!device.begin()) { /* check device.lastError() */ }
}

void loop() {
  // Sending is possible after the host commits and, for Isochronous, selects alternate 1
  if (camera.streaming() && !camera.frameInFlight()) {
    // const auto submission = camera.submitFrame(jpeg, jpegBytes);
    // if (!submission) { /* see submission.error; the frame was not borrowed */ }
  }
}
```

- Every setting (`configure()`, format, size, rate, maximum frame, callback registration) is made while stopped and takes effect at `device.begin()`.
- To change them: `device.end()`, set, then `device.begin()`. No rebuild is needed.
- Submit frames from `loop()` at the frame rate. Submitting the next frame from inside the callback sends as fast as the endpoint drains, which exceeds the advertised rate.

## Configuration

### Constructor

| Argument | Default | Meaning |
| --- | --- | --- |
| `device` | — | The `EspUsbDevice` to register with |
| `name` | `nullptr` | Function name (iFunction of the IAD and iInterface of both interfaces). Windows uses it as the camera name |

### `EspUsbVideoConfig`

| Field | Default | Range and meaning | FS/HS difference |
| --- | --- | --- | --- |
| `transport` | `Isochronous` | `Isochronous` or `Bulk` | none |
| `payloadBytes` | 512 | Size of one payload, including the 2-byte UVC header. 3 to 65535; at most 1024 for Isochronous | Bulk splits it into 64-byte (FS) or 512-byte (HS) packets |
| `isochronousPacketBytes` | 0 | ISO endpoint packet size. 0 derives it from `payloadBytes`. When given, 3 to 1024 | Limit 1023 at FS, 1024 at HS |
| `payloadBuffer` | `nullptr` | External payload staging. `nullptr` allocates at begin | none |
| `payloadBufferBytes` | 0 | Length of the external staging | none |

### Format settings (functions)

| Function | Default | Range |
| --- | --- | --- |
| `bool setFormat(format)` | `Mjpeg` | `Mjpeg` or `Yuy2` |
| `bool setFrameSize(width, height)` | 320×240 | 1 to 4096. YUY2 needs an even width |
| `bool setFrameRate(fps)` | 15 | 1 to 255 |
| `bool setMaxFrameSize(bytes)` | 0 (automatic) | Upper bound of one frame. 0 means `width × height × 2` for YUY2 and one eighth of that for MJPEG |

- While running, or for an invalid value, they return `false` and keep the previous settings.
- Invalid values include an unknown format, a dimension of 0 or above 4096, a rate of 0, and a bit rate (maximum frame × 8 × fps) that does not fit 32 bits.
- Each setter checks against the current values of the others. A YUY2 frame is always `width × height × 2` bytes, so `setFormat(Yuy2)` returns `false` while `setMaxFrameSize()` holds a different value. Call `setMaxFrameSize(0)` before switching to YUY2.
- The default MJPEG maximum (one eighth of uncompressed) is not a promise about encoder output. Raise it if real frames are larger: the host sizes its receive buffers from it, and a value that is too small shows up as torn images.

### FS and HS

| Item | FS | HS |
| --- | --- | --- |
| ISO interval | 1 ms (bInterval 1) | 125 µs (bInterval 1) |
| ISO packet limit (USB 2.0 §5.6.3) | 1023 bytes | 1024 bytes (one packet per microframe) |
| ISO theoretical limit (default 512 bytes, minus the 2-byte header) | 510,000 bytes/s | 4,080,000 bytes/s |
| ISO theoretical limit (1023 / 1024 bytes) | about 1 MB/s | about 8 MB/s |
| Bulk packet | 64 bytes | 512 bytes |

- One ISO payload is the smaller of `payloadBytes` and the packet size.
- The limits are calculated, not a promise that a given bandwidth or rate can be sustained.
- 160×120 YUY2 (38,400 bytes) with the default FS packet takes about 76 ms per frame, so about 13 fps. Use MJPEG or high speed for larger images.
- High-bandwidth high speed (several packets per microframe) is not used.
- `isochronousPacketSize(highSpeed)` returns the packet size the descriptor declares for each speed.

## API

### Settings and state

| Function | Returns / errors |
| --- | --- |
| `esp_err_t configure(config)` | Running: `ESP_ERR_INVALID_STATE`. Unknown `transport`: `ESP_ERR_INVALID_ARG`. `payloadBytes` outside 3 to 65535, above 1024 for Isochronous, or a nonzero `isochronousPacketBytes` outside 3 to 1024: `ESP_ERR_INVALID_SIZE` |
| `configuration()` | The settings as given |
| `format()` / `width()` / `height()` / `frameRate()` / `maxFrameSize()` | Current settings |
| `bulkStreaming()` | `true` when Bulk is selected |
| `descriptorLength(highSpeed)` | Descriptor bytes for that speed |
| `isochronousPacketSize(highSpeed)` | ISO packet size for that speed |
| `payloadBufferSize()` | Staging capacity in use. 0 before begin and after end |
| `commit()` | What the host committed (`EspUsbDeviceVideoCommit`); all zero before a commit |
| `startError()` | Why the last begin failed |

`device.lastError()` when `device.begin()` fails:

| Value | Cause |
| --- | --- |
| `ESP_ERR_INVALID_ARG` | An invalid combination of format, size, rate and maximum frame, or external staging that does not meet the conditions |
| `ESP_ERR_INVALID_SIZE` | Too few endpoints or too little descriptor space, or the IN endpoints of the whole configuration do not fit the controller's transmit FIFO |
| `ESP_ERR_NO_MEM` | Allocation failed |
| `ESP_ERR_INVALID_STATE` | Not registered with a Device, a second begin, and so on |

The S2 / S3 controller has a small transmit FIFO shared by every endpoint. An ISO camera plus an Audio Capture stream may not fit. `device.begin()` checks every IN endpoint of the configuration and refuses one that does not fit, instead of a device that enumerates and then delivers nothing.
Lower `payloadBytes`, choose Bulk, or drop another Function.

### Sending frames

| Function | Returns |
| --- | --- |
| `EspUsbTransferSubmission submitFrame(frame, length)` | Converts to `true` and has a nonzero `id` when accepted |
| `bool sendFrame(frame, length)` | `true` when accepted (short form of `submitFrame`) |
| `esp_err_t cancelFrame(id)` | Result of the cancellation request |
| `bool streaming()` | Whether sending is possible (committed and, for Isochronous, alternate 1 selected) |
| `bool frameInFlight()` | Whether a frame is borrowed (including during cancellation) |

A refused submission has `id` 0, borrows nothing and calls no callback. `error` means:

| `error` | Meaning |
| --- | --- |
| `BadArgument` | NULL, length 0, above the maximum frame, wrong YUY2 length, or address-range overflow |
| `NotMounted` | Not started, not committed, ISO alternate not selected, stopping, reconfiguring, halted, and so on |
| `Busy` | The previous frame is still borrowed |
| `TransferFailed` | The internal post or the id could not be issued |

- Do not free or modify the frame until it is returned.
- The frame itself has no placement or alignment requirement (each payload is copied into the staging buffer).
- `cancelFrame(id)` only asks the USB worker to cancel. `ESP_OK` does not mean the frame is back yet; the return arrives through `onFrameResult`. A stale id or 0 gives `ESP_ERR_NOT_FOUND`; during shutdown and similar states it gives `ESP_ERR_INVALID_STATE`.

### Returns and notifications

| Function | Description |
| --- | --- |
| `esp_err_t onFrameResult(callback)` | The frame return, called once for `Success`, `Cancelled`, `BusReset`, `TransferError` or `Stopped` |
| `esp_err_t onFrameComplete(callback)` | Called on success only. With both registered: result, then complete |
| `esp_err_t onCommit(callback)` | The host committed parameters. Not an ownership return |

- `EspUsbTransferResult::actualBytes` is the confirmed number of frame bytes sent, excluding UVC headers. When an abort leaves it unknown, `actualBytesKnown` is `false`.
- Callbacks can be registered only while stopped; while running the call returns `ESP_ERR_INVALID_STATE`.
- An application that handles interruptions uses `onFrameResult`. `onFrameComplete` reports success only.

### Bulk HALT and CLEAR

When the host sends SET_FEATURE(ENDPOINT_HALT) on Bulk:

- DMA stops and a borrowed frame is returned once with `TransferError`.
- While halted, `streaming()` is `false` and `submitFrame()` returns `NotMounted`.

On CLEAR_FEATURE(ENDPOINT_HALT):

- Only that endpoint returns to DATA0 and streaming resumes under the current commit. A CLEAR without a HALT returns a borrowed frame as `Cancelled`.
- Submit a new frame after resuming. The interrupted frame is not continued.

Isochronous has no HALT; alternates 0 and 1 stop and resume the stream.

### Calling context

| Operation | Context |
| --- | --- |
| `configure`, the `set*` functions, callback registration | An application task, while stopped |
| `submitFrame`, `sendFrame`, `cancelFrame`, state queries | An application task, or inside `onFrameResult` |
| `onFrameResult`, `onFrameComplete`, `onCommit` | Called on the USB worker task. Keep them short |

Do not call `device.end()`, destroy the Function, or replace callbacks from inside a callback.
Concurrent `submitFrame` calls are serialized; the second one gets `Busy`.

## Buffers and memory

| Area | Placement | Allocated |
| --- | --- | --- |
| Payload staging (one) | DMA-capable internal RAM, 64-byte aligned | At begin; `payloadBytes` rounded up to 64 bytes |
| Function state | Ordinary heap | At begin |
| Frame | Application memory (borrowed) | By the application |

- External staging (`payloadBuffer`) must meet every condition below, or begin fails with `ESP_ERR_INVALID_ARG`:
  - a 64-byte aligned address;
  - a length (`payloadBufferBytes`) of at least `payloadBytes` rounded up to 64 bytes;
  - DMA-capable internal RAM from start to end (**not PSRAM**).
- Keep external staging valid from begin to end and do not touch it meanwhile.
- The frame is copied into staging by the CPU, so it has no DMA placement requirement. **A frame in PSRAM works in principle**; it has not been verified on hardware.
- No heap is used while sending ordinary frames, and there is no static buffer for an unused Video.

## Host OS

| OS | Built-in driver | Verified with this library (v3) |
| --- | --- | --- |
| Windows | `usbvideo.sys` | not yet (v2 was confirmed binding and receiving on Windows 11) |
| Linux | `uvcvideo` (V4L2) | not yet |
| macOS | Built-in UVC support | not yet |

- Operation has been confirmed with ESP32 hosts (two S3 boards at full speed, one P4 in loopback, and P4 to P4X high-speed links). JPEG decoding and display on a PC have not yet been confirmed with v3.
- The host can only choose the one format, size and rate in the descriptor. Keep the application's settings in step with it.
- Windows shows the Function name (`name`) as the camera name.
- Sending a frame and the PC displaying it are separate facts.

## Standards and design rationale

### UVC 1.5 mapping

| Item | Implementation |
| --- | --- |
| Class version | bcdUVC 0x0150, interface protocol PC_PROTOCOL_15 |
| Interfaces | Video Control and Video Streaming, grouped by an IAD. No VC interrupt endpoint |
| Entities | Camera Terminal → Output Terminal (USB Streaming) |
| Format descriptors | MJPEG or Uncompressed (YUY2 GUID), plus one Frame descriptor and Color Matching |
| Frame rate | One frame interval (100 ns units) in the Frame descriptor |
| Probe / Commit (§4.3.1.1) | Accepts the 48-byte UVC 1.5 block and short 26 / 34-byte SETs. Probe is answered with supported values; Commit refuses unsupported ones |
| Payload header (§2.4.3.3) | 2 bytes: EOH, FID, and EOF on the last payload of a frame. No PTS / SCR |
| Isochronous | Alternate 0 has no endpoint (zero bandwidth, USB 2.0 §5.6.3); alternate 1 has an asynchronous ISO IN |
| Bulk | Bulk IN in alternate 0. A ZLP is sent when a payload ends on a packet multiple |

### Rationale

- **Transport is chosen at run time.** v2's `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` and `CFG_TUD_VIDEO_STREAMING_BULK` are gone; one binary chooses per Function. The old build flag that enabled TinyUSB Video is a compile error, to prevent two drivers.
- **Frames are borrowed, payloads are copied.** Frames carry no DMA requirement. The cost is one staging buffer of internal RAM and the copy.
- **Every ending returns the frame.** Cancellation, bus reset, HALT and stop all return the frame exactly once, so the application can safely reuse it afterwards.
- **No single global.** State lives in each Function, so several Video Functions can coexist.
- **Submission result matches Vendor / CDC `writeDirect`.** `EspUsbTransferSubmission` tells you why a frame was refused.

### Main changes from v2

- Transport and payload size are set with `configure()` (the build flags are gone).
- Queries such as `payloadBufferSize()` are per instance and read 0 before begin.
- Frames come back through `onFrameResult()`, including interrupted frames.
- `submitFrame()` returns `EspUsbTransferSubmission`; the v2 three-argument form is gone.
- Callback registration returns `esp_err_t` and is refused while running.

## Related examples

| Example | What it shows |
| --- | --- |
| [VideoCamera](../../examples/VideoCamera/) | A camera sending a test pattern (160×120 YUY2, 15 fps). Submits from `loop()` at the frame rate and receives returns in `onFrameResult` |

Related documents: [USB device development guide](../usb-device-guide.md), [troubleshooting](../troubleshooting.md), [migrating from v2](../migrating-from-v2.md)
