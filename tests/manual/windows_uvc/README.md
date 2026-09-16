# windows_uvc

> 日本語版: [README.ja.md](README.ja.md)

**Does a real host accept the UVC descriptor this library builds, bind its own
camera driver with nothing to install, and receive frames?**

`tests/single/video_descriptor` proves the bytes are right without a host. Only
a host can say whether it streams, and on this one it did not at first - the
descriptor was valid and the device reported sending frames while the host
received none. That failure and its cause are below.

## What it needs

- An ESP32-S3 whose native USB goes to a Windows PC, **not attached to WSL**
  (`usbipd.exe detach --busid <n>`)
- A separate UART for flashing. Opening it resets the board on this rig, which
  is a re-plug: read the host side before opening it
- `ffmpeg` on the Windows side for the capture step

## Running it

```sh
cd tests/manual/windows_uvc
uv run --with pillow python make_frames.py      # only when frames.h needs regenerating
printf -- '' > build_opt.h                       # MJPEG 320x240 at 15 fps
arduino-cli compile --profile esp32s3 --clean . && arduino-cli upload --profile esp32s3 --port /dev/ttyACM3 .
```

Then capture, from WSL, with the device NOT attached to WSL:

```sh
ffmpeg.exe -f dshow -vcodec mjpeg -video_size 320x240 -framerate 15 \
  -i 'video=EspUsbDevice Camera' -t 12 -c copy -f mjpeg -y 'C:\Users\Public\uvc.mjpg'
```

`-c copy -f mjpeg` keeps the JPEGs exactly as the device sent them, which is
what makes the byte-for-byte comparison below possible. Muxing into a container
(`.mkv`) does not: a failed capture still produces a plausible-looking file.

Variants come from `build_opt.h`: `-DVAR_FORMAT=1` for generated YUY2,
`-DVAR_WIDTH=`/`-DVAR_HEIGHT=`/`-DVAR_FPS=`, `-DVAR_PID=` for a fresh identity,
and `-DCFG_TUD_VIDEO_STREAMING_EP_BUFSIZE=` to change the payload size. That
last one is library-wide, so **`--clean` every time**.

## What was measured

Windows 11 25H2, build 26200.9457, on 2026-09-16. ESP32-S3, full speed.

**Binding needs nothing installed.** `usbvideo` bound to the `&MI_00` child
73 ms after arrival, class `Camera`, friendly name taken from the function name
(`EspUsbDevice Camera`), compatible ID `USB\COMPAT_VID_303a&Class_0e&SubClass_03`.
The parent bound `usbccgp` from the IAD. DirectShow reported the format the
device advertises and nothing else:

```
vcodec=mjpeg  min s=320x240 fps=15 max s=320x240 fps=15
```

**The bytes arrive.** 181 frames captured in 12 s with `-c copy`, **every one
byte-for-byte identical to the frame the device sent**, in consecutive order
with no gaps or repeats. Device side: 15.16 fps measured against 15 advertised,
51 KB/s, zero failed transfers, clean stop when the capture ended.

**The host decodes them as video.** That is a separate question, and `-c copy`
does not answer it - it never decodes. `verify_frames.py` captures again
through ffmpeg's MJPEG decoder (or the uncompressed path), writes PNGs, and
checks the picture:

```sh
uv run --with pillow python verify_frames.py --format mjpeg
uv run --with pillow python verify_frames.py --format yuy2    # -DVAR_FORMAT=1 build
```

Both formats decode to 30 frames with the eight colour bars correct in every
frame and the block stepping one cell per source frame, never stalling:

| | MJPEG 320x240 | uncompressed YUY2 160x120 |
|---|---|---|
| frames decoded | 30 | 30 |
| colour bars correct | every frame | every frame |
| block motion | 3 cells per sample, no stalls | 3 cells per sample, no stalls |

(Three cells because the script samples at a third of the frame rate.)

### Why the decode check exists

The byte-for-byte comparison passed while the uncompressed pattern was wrong.
The sketch wrote YUY2 as `Y0 U Y1 V` with **V held at 128**, which leaves the
luma ramp exactly right and every colour wrong - and the first check written
here only looked at luma, so it passed too. The picture was white, yellow-green
and lavender instead of the standard bars. Both this sketch and
`examples/VideoCamera` had it; both are fixed, and `verify_frames.py` now
compares actual colours so the same mistake cannot pass again.

The lesson generalises: a transport check and a content check are different
tests, and passing the first says nothing about the second.

### The failure that came first, and why it was invisible

The first build advertised a 1023-byte isochronous endpoint - the full-speed
maximum. It enumerated, `usbvideo` bound, the host committed its streaming
parameters, the device reported 15 fps of frames going out with zero failures,
and **not one byte reached the host**. `ffmpeg` read buffers containing no
JPEG. Some tens of seconds later the device crashed in an unrelated EP0 path
(`dcd_event_setup_received` reading a garbage `DOEPDMA0`).

The DWC2 FIFO registers, read from the device after the host configured it,
said why:

| `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` | endpoint | `DIEPTXF1` | result |
|---|---|---|---|
| 1024 | 1023 bytes | 256 words at offset **512** | host cannot start the stream at all |
| 1023 | 1023 bytes | 256 words at offset **512** | streams "successfully", host receives nothing, later crash |
| 512 | 512 bytes | 128 words at offset 98 | works |

The S3's DWC2 has a 256-word (1 KB) SPRAM shared between the receive FIFO,
every transmit FIFO and the DMA endpoint-info area. After the endpoint-info
area (14 words), EP0's transmit FIFO (16) and the receive FIFO (62), about 164
words remain - and a 1023-byte endpoint needs 256. The allocation was not
refused: `DIEPTXF1` was programmed at offset 512, past the end of the 242-word
usable area, and the endpoint then transmitted into nothing and eventually
corrupted memory that EP0 was using.

Two changes came out of this. `CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE` now defaults
to 512 on the S2 and S3 (1023 on the P4), and `EspUsbDevice::begin()` checks
the whole configuration against the same FIFO arithmetic and returns
`ESP_ERR_NO_MEM` rather than enumerating a camera that cannot transmit. The
sketch prints the FIFO registers at commit and at stream start, which is how to
check this on another part:

```
UVC fifo@commit depth=200 rx=62 ep0in=16@226 ep1in=128@98 epinfo=242
```

`ep1in=<words>@<offset>`: the offset plus the size must stay below `epinfo`.

### Pacing

An early build armed the next frame from the completion callback, which is the
right shape for a bulk stream and the wrong one for a camera: it sent as fast
as the endpoint drained, 180 fps against an advertised 15. Frames are armed
from `loop()` on a deadline instead, and the completion callback only counts.
The example and this sketch both do it that way.

### Two things the host decides, not the device

`dwMaxVideoFrameSize` came back as 153600 - the *uncompressed* size of a
320x240 frame - even though the descriptor advertises 3451 for MJPEG. TinyUSB
computes that number itself when the host probes with zero, from width x height
x 16 bits, regardless of the frame descriptor. It is only a buffer hint and
costs nothing here, but it is not what the descriptor says.

`dwMaxPayloadTransferSize` is likewise TinyUSB's, capped at
`CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE`. It must not exceed the endpoint's
`wMaxPacketSize` or the host cannot open the streaming alternate setting, which
Windows reports as a failure to build its capture graph and says nothing about
descriptors.
