# EspUsbDevice VideoCamera

> 日本語版: [README.ja.md](README.ja.md)

A USB Video Class camera with no camera attached: it streams a generated test
pattern, so it runs on any supported board with nothing wired to it.

Windows, macOS and Linux all bind their own UVC driver with nothing to install,
and the device appears wherever webcams do - Camera app, browser
`getUserMedia()`, OBS, `ffmpeg -f dshow`, `v4l2`.

## Know the speed first

Isochronous bandwidth is the constraint, and it is not generous at full speed:

| Part | Link | Isochronous bandwidth | 160x120 YUY2 | 320x240 YUY2 | 320x240 MJPEG |
|---|---|---|---|---|---|
| ESP32-S2 / S3 | full speed | ~0.5 MB/s at the default payload | 15 fps | ~6 fps | comfortable |
| ESP32-P4 | high speed | ~24 MB/s | easily | easily | easily |

This example uses 160x120 uncompressed (YUY2) at 15 fps, which is 38 KB per
frame and 576 KB/s - about what a full-speed part can carry. Uncompressed is
used here because it needs no encoder; MJPEG is what makes larger frames
practical, and the library supports it with
`Camera.setFormat(EspUsbDeviceVideoFormat::Mjpeg)` plus
`Camera.setMaxFrameSize(bytes)`. The library does not encode JPEG: a real
product feeds it frames from a sensor that already produces them, or from an
encoder of its own.

## Hardware

- An ESP32-S2, ESP32-S3 or ESP32-P4 board with native USB
- A USB cable to a host

Nothing else. The image is generated.

## What to expect

```
UVC camera ready: 160x120 YUY2 at 15 fps, 512-byte payloads
Open it wherever your OS lists webcams.
Host committed: frame 1, 66666600 ns interval, payload 512 bytes
Streaming started
```

Colour bars with a white block stepping across the bottom, one step per frame.
The moving block is the point: a still image cannot tell a live stream from a
stuck one.

## The three things worth copying

**Arm frames from one place, paced to the advertised rate.** The completion
callback fires as soon as the last payload has gone, so arming from inside it
streams as fast as the endpoint drains - measured at 180 fps against an
advertised 15. `loop()` arms on a deadline instead, and the callback only
counts.

**The frame buffer belongs to the driver until the callback runs.** It is read
payload by payload, not copied, so the next frame is drawn only once
`frameInFlight()` is false.

**Check `begin()`.** `ESP_ERR_NO_MEM` means the controller's transmit FIFO
cannot back the isochronous endpoint alongside everything else in the
configuration - the S2 and S3 share one 1 KB FIFO between every endpoint. The
library refuses to start rather than enumerate a camera that cannot transmit,
which is what happens if the endpoint is advertised anyway.

## Related

- [docs/usb-device-guide.md](../../docs/usb-device-guide.md) - speeds, endpoint
  budget and how to observe the device from each host OS
- [tests/manual/windows_uvc](../../tests/manual/windows_uvc/) - the measured
  Windows results behind the numbers above
