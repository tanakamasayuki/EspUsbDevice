# EspUsbDevice VideoCamera

> 日本語版: [README.ja.md](README.ja.md)

A USB Video Class camera with no camera attached: it streams a generated test
pattern, so it runs on any supported board with nothing wired to it.

Windows, macOS and Linux all bind their own UVC driver with nothing to install,
and the device appears wherever webcams do - Camera app, browser
`getUserMedia()`, OBS, `ffmpeg -f dshow`, `v4l2`.

```cpp
EspUsbDevice device;
EspUsbDeviceVideo camera(device, "EspUsbDevice Camera");

EspUsbVideoConfig config;
config.transport = EspUsbVideoTransport::Isochronous;  // or Bulk
config.payloadBytes = 512;
camera.configure(config);
camera.setFormat(EspUsbDeviceVideoFormat::Yuy2);
camera.setFrameSize(160, 120);
camera.setFrameRate(15);
camera.onFrameResult(...);   // the frame is yours again here
device.begin(identity);
```

## Know the speed first

Isochronous bandwidth is the constraint, and it is not generous at full
speed. One packet is sent per 1 ms frame at full speed and per 125 us
microframe at high speed; each payload carries a 2-byte UVC header.

| Part | Link | ISO, default 512-byte packet | ISO, 1023/1024-byte packet | 160x120 YUY2 | 320x240 YUY2 |
|---|---|---|---|---|---|
| ESP32-S2 / S3 | full speed | 510,000 B/s | about 1 MB/s (if the FIFO allows) | about 13 fps | about 3 fps |
| ESP32-P4 HS port | high speed | 4,080,000 B/s | about 8 MB/s | easily 15 fps | about 26 fps |

These are calculated limits, not measurements. This example uses 160x120
uncompressed (YUY2) at an advertised 15 fps: 38,400 bytes per frame, 576,000
B/s. At full speed with the default packet one frame takes about 76 ms, so the
stream runs at about 13 fps; the host still shows a live picture. Use a
smaller frame or MJPEG to reach the full rate there.

Uncompressed is used because it needs no encoder; MJPEG is what makes larger
frames practical, and the library supports it with
`camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg)` plus
`camera.setMaxFrameSize(bytes)`. The library does not encode JPEG: a real
product feeds it frames from a sensor that already produces them, or from an
encoder of its own. For variable-size MJPEG,
`config.transport = EspUsbVideoTransport::Bulk` trades reserved bandwidth for
USB retries.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with native USB
- A USB cable to a host
- A separate debug connection for `Serial` (115200)

Nothing else. The image is generated.

## What to expect

```
UVC camera ready: 160x120 YUY2 at 15 fps, 512-byte payloads
Open it wherever your OS lists webcams.
Host committed: frame 1, 66666600 ns interval, payload 512 bytes
Streaming started
Streaming stopped: 412 frames sent, 1 not completed
```

Colour bars with a white block stepping across the bottom, one step per frame.
The moving block is the point: a still image cannot tell a live stream from a
stuck one. A frame cut off by the host closing the stream is returned with a
non-success status and counted as "not completed".

## The three things worth copying

**Arm frames from one place, paced to the advertised rate.** The frame result
callback fires as soon as the last payload has gone, so arming from inside it
streams as fast as the endpoint drains - measured at 180 fps against an
advertised 15 in v2. `loop()` arms on a deadline instead, and the callback
only counts.

**The frame buffer belongs to the driver until `onFrameResult` returns it.**
The driver copies it payload by payload into its own staging buffer while it
sends, so the next frame is drawn only once `frameInFlight()` is false. The
result arrives once for every way a frame can end: `Success`, `Cancelled`,
`BusReset`, `TransferError` or `Stopped`.

**Check every setup step.** `configure()` returns an `esp_err_t`, the format
setters return `false` for a value they refuse, and `submitFrame()` returns
the reason it did not take a frame. If `device.begin()` fails with
`ESP_ERR_INVALID_SIZE`, the configuration does not fit the controller:
`begin()` checks every IN endpoint of the configuration against the shared
transmit FIFO (1 KB on S2, S3 and the P4 full-speed controller, shared by every
endpoint), as well as the endpoint count and descriptor size. Lower
`payloadBytes`, choose Bulk, or drop another Function. `ESP_ERR_NO_MEM` means
heap. The library refuses to start rather than enumerate a camera that cannot
transmit.

## Related

- [docs/usb-device-guide.md](../../docs/usb-device-guide.md) - speeds, endpoint
  budget and how to observe the device from each host OS
- [v2 tests/manual/windows_uvc](https://github.com/tanakamasayuki/EspUsbDevice/tree/3fed50d0d777f600432b71381faccd93adb9a202/tests/manual/windows_uvc/) -
  the v2 Windows measurements (including the 180 fps figure)

## Walkthrough and checks

### Before running

Select the board's profile (`esp32s3` is the default; `esp32s2`, `esp32p4`
for P4 v1.x and `esp32p4x` for P4 v3.1 or newer; v3.0 is not supported), and locate the
diagnostic Serial connection. [Example workflow](../../docs/example-workflow.md)
explains installation, connectors, USB roles and the difference between
startup and a usable peer.

### Follow the code

The sketch generates images in `frame` rather than reading a sensor. Follow
`streaming()`, `frameInFlight()` and `submitFrame()` in `loop()`: redraw only
after the previous frame has been returned. The commit is noted in
`onCommit` and printed from `loop()`, because both callbacks run on the USB
worker.

### API use and adaptation

Match pixel encoding, dimensions and frame rate on both ends. YUY2 frames must
be exactly `width * height * 2` bytes. Confirm negotiation, frame completion
and consumer speed separately before increasing resolution. Frame counters and
USB send success do not by themselves prove that the PC displayed every
image.

### Verify one change at a time

Run the unchanged example first and compare its log with the host's camera
application. Change one size, rate or transport at a time. The error printing
is not a retry or recovery policy.

## Changes from v2

- Transport and payload size are chosen at run time with `camera.configure()`
  (Isochronous or Bulk) instead of `CFG_TUD_VIDEO_STREAMING_*` build flags.
- `payloadBufferSize()` is per instance and reads 0 before `begin()`; the
  sketch prints it after `device.begin()`.
- `onFrameResult()` replaces `onFrameComplete()` as the place where the frame
  is returned, including aborted frames. `submitFrame()` reports why a frame
  was refused (`EspUsbTransferSubmission`).
- The commit is printed from `loop()` instead of from the USB callback.
- The speed table uses v3's per-packet figures (one ISO packet per
  (micro)frame) and states the full-speed frame rate this example reaches.

Compile-checked with Arduino-ESP32 3.3.12 for every profile in
`sketch.yaml`; this sketch itself has not been run on hardware.

- [Sketch](VideoCamera.ino)
- [All examples](../README.md)
- [Video API](../../docs/classes/video.md)
- [Example workflow](../../docs/example-workflow.md)
