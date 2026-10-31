# USB network (CDC-NCM)

> 日本語版: [ncm.ja.md](ncm.ja.md)

`EspUsbDeviceNet` is the Function that makes the board look like a USB-attached network adapter to the host.
It uses the USB CDC-NCM (Network Control Model) class and carries Ethernet frames as they are.
If needed, `beginNetwork()` attaches ESP-IDF networking (esp_netif / lwIP) so the board can talk IP.

## Overview and when to use it

| Mode | What it is |
| --- | --- |
| IP layer | `beginNetwork()` creates a netif. With the DHCP server running, a PC gets an address just by plugging in and reaches a web page or API on the ESP32 (the "USB configuration portal" pattern) |
| Raw frames | `onFrame()` / `sendFrame()` handle Ethernet frames directly, for your own bridge or for tests. The IP layer is not linked |

- PC and ESP32 can talk IP with no Wi-Fi.
- Only CDC-NCM is provided. There is no CDC-ECM or RNDIS.
- A USB link does not give the ESP32 Internet access. That needs bridging or sharing on the PC.

## Basic usage

### With the IP layer

```cpp
#include <EspUsbDevice.h>

EspUsbDevice device;
EspUsbDeviceNet net(device);

void setup() {
  EspUsbNetConfig config;          // configure() can be skipped if the defaults suit
  config.name = "USB network";
  if (net.configure(config) != ESP_OK) return;

  net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1),
               IPAddress(255, 255, 255, 0));
  net.dhcpServer(true);            // hand 192.168.7.x to the PC

  if (!device.begin()) return;     // check device.lastError()
  net.beginNetwork();              // after device.begin(), from an application task
}

void loop() {}
```

### Raw frames

```cpp
net.onFrame([](const uint8_t *frame, size_t bytes) {
  // runs on the USB worker task; frame is valid only inside the callback
  net.sendFrame(frame, bytes);     // copies into the send queue; false when full
});
device.begin();                    // do not call beginNetwork()
```

To change settings: `device.end()`, `configure()`, then `device.begin()`. No rebuild is needed.

## Configuration

### `EspUsbNetConfig`

`configure()` is accepted only while stopped.

| Field | Default | Range and meaning | FS/HS difference |
| --- | --- | --- | --- |
| `ntbInBytes` | 3200 | Largest device-to-host NTB in bytes. 2048 to 65535 | none |
| `ntbOutBytes` | 3200 | Largest host-to-device NTB in bytes. 2048 to 65535 | Effective limit 65472 at FS and 65024 on an HS-capable controller (receive buffers are rounded up to the MPS) |
| `txBuffers` | 3 | Number of transmit NTB buffers. 0 disables sending | none |
| `rxBuffers` | 2 | Number of receive NTB buffers. 0 disables receiving | none |
| `maxDatagrams` | 8 | Most frames in one NTB. 1 to 16377 | none |
| `maxFrameBytes` | 1514 | Largest frame (Ethernet header included, no FCS). At least 14, and at most the NTB size of either direction − 28 | none |
| `txBuffer` / `rxBuffer` | `nullptr` | Buffers supplied by the application. `nullptr` allocates at begin | The receive size required changes |
| `txBufferBytes` / `rxBufferBytes` | 0 | Byte length of external buffers; required with them | none |
| `name` | `nullptr` | Interface name (string descriptor) | none |

An NTB (NCM Transfer Block) packs several Ethernet frames into one USB transfer.
IN is device to host and OUT is host to device.

### IP-layer settings

All return `esp_err_t`; while the IP layer is running they return `ESP_ERR_INVALID_STATE`.

| Function | Default | Description |
| --- | --- | --- |
| `ipConfig(local, gateway, subnet)` | 192.168.7.1 / 192.168.7.1 / 255.255.255.0 | Own address; also the base for the DHCP server's pool |
| `dhcpServer(enable)` | off | Runs a DHCP server. Exclusive with `dhcpClient`; the one enabled last wins |
| `dhcpClient(enable)` | off | Gets an address by DHCP (for example when the PC bridges the USB NIC onto a LAN) |
| `dhcpAdvertiseGateway(enable)` | off | Advertises a gateway (option 3). Enable only if the device really forwards traffic |
| `dhcpDns(dns)` | 0.0.0.0 (not advertised) | DNS server advertised by DHCP (option 6) |
| `defaultRoute(enable)` | off | Makes this interface the ESP32's default route |

- By default no gateway or DNS is advertised. Making a non-forwarding device the PC's default route would cut off the PC's outbound traffic.
- With `defaultRoute` off, the USB netif gets a low route priority, so a Wi-Fi STA stays the default route.
- With neither DHCP mode, the static `ipConfig()` address is used.

### FS and HS

"HS-capable controller" means an ESP32-P4 whose `EspUsbDeviceConfig::controller` is not `FullSpeed`.
The buffers are then laid out for a 512-byte bulk MPS, and the same layout is used if the host actually connects at full speed.

| Item | FS | HS |
| --- | --- | --- |
| Bulk MPS (USB 2.0 §5.8.3) | 64 bytes | 512 bytes |
| Effective `ntbOutBytes` limit | 65472 | 65024 (above it, begin fails with `ESP_ERR_INVALID_SIZE`) |
| One receive buffer | `ntbOutBytes` rounded up to 64 | `ntbOutBytes` rounded up to 512 |
| ZLP after an NTB when | its length is a multiple of 64 and below the negotiated maximum | its length is a multiple of 512 and below the negotiated maximum |
| Notification endpoint | 16 bytes, bInterval 16 (16 ms) | 16 bytes, bInterval 8 (2^(8−1) × 125 µs = 16 ms) |
| Reported link speed | 12 Mbps | 480 Mbps |

## API

### Configuring and starting

| Function | Returns and constraints |
| --- | --- |
| `esp_err_t configure(config)` | Running: `ESP_ERR_INVALID_STATE`. Size or count out of range: `ESP_ERR_INVALID_SIZE`. External buffer with a buffer count or byte length of 0: `ESP_ERR_INVALID_ARG` |
| `configuration()` | The settings as given |
| `esp_err_t macAddress(mac)` | Stopped only (`ESP_ERR_INVALID_STATE` while running). A multicast or all-zero MAC gives `ESP_ERR_INVALID_ARG` |
| `const uint8_t *macAddress()` | The current MAC (6 bytes) |
| `esp_err_t onFrame(callback)` | Stopped only; `ESP_ERR_INVALID_STATE` while running |
| `startError()` | Why the last begin failed |

`device.lastError()` when `device.begin()` fails:

| Value | Cause |
| --- | --- |
| `ESP_ERR_INVALID_SIZE` | One receive buffer would exceed 65535 bytes, or too few endpoints or too little descriptor space |
| `ESP_ERR_INVALID_ARG` | An external buffer that is too short, not 64-byte aligned, not DMA-capable internal RAM, or overlapping the other direction's buffer |
| `ESP_ERR_NO_MEM` | Allocation failed |
| `ESP_ERR_INVALID_STATE` | Not registered with a Device, a second begin, and so on |

### Sending and receiving

| Function | Description |
| --- | --- |
| `bool sendFrame(data, length)` | `true` when the frame was copied into a transmit NTB. `false` when the link is not up, the queue is full, or the length is below 14 or above `maxFrameBytes` |
| `onFrame` callback | Called on the USB worker task for each received frame. The pointer is valid only inside the callback |
| `bool linkUp()` | `true` when the host has enabled the data interface and traffic can flow |
| `ntbInCapacity()` / `ntbOutCapacity()` | Allocated NTB bytes. 0 before begin, after end, or for a direction with no buffers |
| `txBufferCount()` / `rxBufferCount()` | Allocated buffers. 0 before begin and after end |
| `stats()` | `txFrames`, `rxFrames`, `invalidNtbs`, counted from begin to end |

- `true` from `sendFrame()` means "copied and queued", not "delivered". `txFrames` increases once the USB transfer completes.
- While the queued NTB has room, following frames are packed into the same NTB.
- Frames sent before begin are not carried into a later connection.
- With two or more receive buffers, the next receive starts before the callback runs. With one, it starts after the callback returns.
- A received NTB is fully checked (header, tables, every frame's range and overlap) before frames are delivered in order. A broken NTB is dropped whole and `invalidNtbs` increases; a partial set of frames is never delivered.

### IP layer

| Function | Description |
| --- | --- |
| `bool beginNetwork()` | Starts the IP layer |
| `bool endNetwork()` | Stops only the IP layer and returns to raw `onFrame` frames |
| `bool networkUp()` | State of the esp_netif interface |
| `IPAddress localIP()` | The current IP address |

- `beginNetwork()` returns `false` before `device.begin()`. It also needs `maxFrameBytes` of at least 1514 and at least one buffer in each direction (for the 1500-byte IP MTU).
- Call `beginNetwork()` / `endNetwork()` from an application task. From the USB worker (inside `onFrame`, for example) they return `false`.
- `device.end()` also tears down the IP layer.
- A netif is created only for the Function that called `beginNetwork()`. Several NCM Functions never share a netif or IP settings.

### MAC address

- The default MAC is a locally administered unicast address derived from the ESP32's MAC. Several NCM Functions on one Device each get a different one.
- Each Function carries its own iMACAddress string descriptor.
- The ESP32-side netif MAC is the USB-advertised MAC with the lowest bit of its last byte inverted (the host and the device each need their own MAC).

### Endpoint HALT

IN, OUT and the notification endpoint handle HALT and CLEAR_FEATURE independently.

| Endpoint | Dropped on HALT / CLEAR | Kept |
| --- | --- | --- |
| IN (send) | NTBs being sent or queued. `sendFrame()` returns `false` while halted | Receiving |
| OUT (receive) | A partly received NTB | Sending and the send queue |
| Notification | Nothing. After CLEAR the latest speed and connection state are sent again | Bulk state |

- CLEAR resets only that endpoint to DATA0. The opposite direction and other NCM Functions are untouched.
- The MAC, NTB limits, requested alternate and callbacks stay. A HALT never rebuilds the IP layer.
- Retransmitting lost frames is the upper layer's job (TCP, for example).

### Calling context

| Operation | Context |
| --- | --- |
| `configure`, `macAddress(mac)`, `onFrame`, IP-layer settings | An application task, while stopped (IP settings: while the IP layer is stopped) |
| `sendFrame` | An application task, or inside `onFrame` |
| `beginNetwork`, `endNetwork` | An application task only |
| The `onFrame` callback | The USB worker task. Return promptly without waiting |

## Buffers and memory

| Area | Placement | Allocated |
| --- | --- | --- |
| Transmit NTBs (`txBuffers`) | DMA-capable internal RAM, 64-byte aligned | At begin; each is `ntbInBytes` rounded up to 64 |
| Receive NTBs (`rxBuffers`) | DMA-capable internal RAM, 64-byte aligned | At begin; each is `ntbOutBytes` rounded up to the MPS |
| Notification buffer (16 bytes) | DMA-capable internal RAM | At begin |
| Bookkeeping | Ordinary heap | At begin |
| Copies of received frames for lwIP | lwIP's heap | Per received frame, when the IP layer is used |

- With external buffers (`txBuffer` / `rxBuffer`), begin allocates no NTB storage.
- External buffers must be DMA-capable internal RAM (**not PSRAM**), start on a 64-byte boundary, be at least "one buffer × count" long, and not overlap each other.
- Example: `ntbInBytes = ntbOutBytes = 3200`, two buffers each:

| | FS (MPS 64) | HS-capable controller (MPS 512) |
| --- | --- | --- |
| Transmit | 3200 × 2 = 6400 bytes | 3200 × 2 = 6400 bytes |
| Receive | 3200 × 2 = 6400 bytes | 3584 × 2 = 7168 bytes |

- Keep external buffers valid from begin to end and do not write to them from the application.
- NCM itself uses no heap while running. With the IP layer, lwIP allocates its own copy of each received frame.
- A direction with 0 buffers allocates none. The interface, endpoints, notification buffer and bookkeeping remain.

## Host OS

| OS | Built-in driver | Verified with this library |
| --- | --- | --- |
| Linux | `cdc_ncm` | not yet |
| Windows | `UsbNcm` (whether it is included depends on the Windows version) | not yet |
| macOS | Built-in NCM support | not yet |

- Operation has been confirmed with ESP32 hosts (two S3 boards at full speed, one P4 in loopback, and P4 to P4X high-speed links). Interoperation with PC NCM drivers, DHCP option handling and route selection have not yet been confirmed.
- Checking on a PC, in order: USB enumeration, a new network interface, its address, route and DNS, then the application connection.
- Do not change the PC's default route; reaching the directly attached `192.168.7.0/24` is enough.
- On Linux, `dmesg` shows whether `cdc_ncm` bound, and `ip addr` shows the interface and address.

## Standards and design rationale

### CDC-NCM 1.0 mapping

The reference is the [NCM 1.0 specification set, including Errata 1](https://www.usb.org/sites/default/files/NCM10_012011.zip).

| Item | Implementation |
| --- | --- |
| Interfaces | Communication (subclass 0x0d) and Data (protocol 0x01), grouped by an IAD |
| Functional descriptors | Header (CDC 1.10), Union, Ethernet Networking (iMACAddress, wMaxSegmentSize = `maxFrameBytes`, no filters), NCM (bcdNcmVersion 1.00) |
| bmNetworkCapabilities (§5.2.1) | D5 only (8-byte GET / SET_NTB_INPUT_SIZE). No packet filter or other capability is declared |
| NTB format (§3) | NTB16 only, no CRC. NTB32 is not handled |
| Class requests (§6.2) | GET_NTB_PARAMETERS, GET / SET_NTB_INPUT_SIZE, GET_NTB_FORMAT, SET_NTB_FORMAT (NTB16 only), GET_MAX_DATAGRAM_SIZE, GET / SET_CRC_MODE (no CRC only), SET_ETHERNET_PACKET_FILTER (accepted only) |
| Notifications (§6.3) | NETWORK_CONNECTION and CONNECTION_SPEED_CHANGE |
| Data interface alternates (§7.1) | Alternate 0 has no endpoints (stop and reset); alternate 1 has bulk IN / OUT |

- When the host renegotiates the NTB input size, the new size and frame-count limit are honoured.
- SET_INTERFACE and NTB-limit changes switch the whole Function: old DMA stops before the buffers are reused and queued frames are dropped. SET_INTERFACE also resets the data toggles.

### Rationale

- **An own driver per Function.** v2's fixed NTB pool, separate send queue and global MAC / queue variables are gone. Unused directions cost no fixed pool, one binary can change its configuration, and each Function owns its MAC and netif.
- **Frames are copied straight into a transmit NTB.** No intermediate queue, so the cost follows the direction and size. In exchange, queued frames are not kept across a size renegotiation.
- **Per-endpoint HALT recovery.** Only the affected pipe is recovered; unrelated traffic and notifications keep flowing.
- **The IP layer is linked only when used.** With only `onFrame` / `sendFrame`, the esp_netif glue is not linked.

### Main changes from v2

- NTB sizes and buffer counts are set per Function with `configure()` and allocated at begin (the fixed pool and send queue are gone).
- Setters such as `macAddress()`, `onFrame()` and `ipConfig()` return `esp_err_t`.
- Sending before begin, and changing the MAC or settings while running, are not possible.

## Related examples

| Example | What it shows |
| --- | --- |
| [UsbNetwork](../../examples/UsbNetwork/) | DHCP server and a small web page; open `http://192.168.7.1/` on the PC |

Related documents: [USB device development guide](../usb-device-guide.md), [troubleshooting](../troubleshooting.md), [migrating from v2](../migrating-from-v2.md)
