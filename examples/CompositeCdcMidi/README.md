# EspUsbDevice CompositeCdcMidi

> 日本語版: [README.ja.md](README.ja.md)

One device with two Functions: a CDC ACM serial port and a USB MIDI 1.0 port.
Both echo what the host sends.

The example shows that Functions in one device are independent. Each one has
its own `configure()`, its own rings and its own backpressure: a full MIDI
send ring does not stop the serial echo, and the other way round.

## Hardware

- ESP32-S2 / S3 / P4 board with a native USB OTG port
- A USB host with a serial terminal and MIDI software (a PC, or another ESP32
  running EspUsbHost)
- No Serial monitor output is used

## What It Does

- Configures the CDC port (interface name `Console`) with 128-byte receive and
  send rings and `transferBytes = 64`
- Configures MIDI with one cable each way, 16-packet rings and
  `transferPackets = 4`
- Starts `EspUsbDevice` with the product string `CDC and MIDI echo`
- In `loop()`: echoes CDC bytes while there is room to send, and echoes MIDI
  packets with the hold-and-retry pattern of [MidiEcho](../MidiEcho/)

## Checking From the Host

The host sees one composite device with four interfaces: CDC communication and
data, and MIDI audio control and streaming. The CDC pair is grouped by an Interface
Association Descriptor. The MIDI pair is tied together by the class-specific
Audio Control header, which lists its MIDI Streaming interface (USB MIDI 1.0
follows Audio 1.0 here).

On Linux:

```sh
lsusb -v -d 303a:4000 | grep -E 'iProduct|iInterface|bInterfaceClass'
picocom /dev/ttyACM0                       # type: characters come back
amidi -l                                   # find the MIDI port
amidi -p hw:1,0,0 -S '90 3C 64' -d -t 1    # prints 90 3C 64
```

Run the serial and MIDI checks at the same time to see that they do not block
each other. Windows and macOS have not been checked with this example.

## Key APIs

- `EspUsbDeviceCdcSerial console(device, "Console")` and
  `EspUsbDeviceMidi midi(device)` register the two Functions on the same
  `EspUsbDevice`. Interface and endpoint numbers are assigned at `begin()` in
  registration order.
- `console.configure(EspUsbBulkStreamConfig)` and
  `midi.configure(EspUsbMidiConfig)` are separate. Each is accepted only while
  stopped.
- `EspUsbDeviceConfig::product` sets the product string; the other identity
  fields keep their defaults.
- `console.writeAvailable()` is the free space in the CDC send ring.
- `midi.readPacket()` / `writePacket()` never wait.

## Notes

- `device.begin()` checks the whole configuration: endpoint numbers and the
  transmit FIFO for every IN endpoint. CDC uses a notification IN and a bulk
  IN; MIDI uses one bulk IN.
- The MIDI echo stops while the host is not configured (`device.ready()` false)
  and drops the held packet. The CDC echo needs no check: `available()` is 0
  and nothing is read while stopped.
- A failed `configure()` or `begin()` is printed on `Serial` (115200) of the
  separate debug connection, with the error name.

## See Also

- [MidiEcho](../MidiEcho/) - MIDI echo alone
- [CdcConsole](../CdcConsole/) - CDC echo through `Stream &`
- [SerialMulti](../SerialMulti/) - several CDC ports
- [CompositeHidCdcMsc](../CompositeHidCdcMsc/) - HID, CDC and MSC in one device
- [CDC API](../../docs/classes/cdc.md), [MIDI API](../../docs/classes/midi.md)

## Walkthrough and checks

### Before running

Follow the hardware requirements above and select the board and the example's
profile. [Example workflow](../../docs/example-workflow.md) explains
installation, connectors, USB roles and the difference between startup and a
usable peer.

### Follow the code

`setup()` prepares both configurations before it touches the device, then
starts once. `loop()` serves the two Functions one after the other and never
waits on either.

### API use and adaptation

When you add a Function, give it its own configuration and check its result.
Changing one Function's settings later means `device.end()` and a new
`device.begin()` for the whole device ([ReconfigureVendor](../ReconfigureVendor/)).

### Verify one change at a time

Check each echo alone first, then both together. Add a third Function only
after both pass.

## Changes from v2

This example is new in v3. v2 has no corresponding example.

- [Sketch](CompositeCdcMidi.ino)
- [All examples](../README.md)
- [Example workflow](../../docs/example-workflow.md)
