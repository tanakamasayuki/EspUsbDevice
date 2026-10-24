# Working through the examples

[日本語](example-workflow.ja.md)

A practical route from wiring through observation and adaptation for EspUsbDevice. The ESP32 becomes a USB peripheral connected to a PC or another host. It can enumerate as keyboard, CDC, audio and other functions. Two device ports alone do not initiate enumeration.

## Terms used in these guides

| Term | Meaning here |
| --- | --- |
| Enumeration | The host reads device information and chooses an address/configuration |
| Descriptor | Information describing a device's functions and data formats |
| Interface | A function-level grouping within one USB device; composites may have several |
| Endpoint | A data entry/exit with a number, direction and transfer type |
| Report / packet / frame | Class/protocol-defined units, not interchangeable byte counts |
| Callback | A function registered beforehand and invoked by the library for an event/request |
| Buffer / queue | Finite storage for data waiting to be processed |
| Timeout | A limit on waiting for a peer so an absent response can be handled |

## Choose a starting point

1. Use [BringUpCheck](../examples/Info/EspUsbDeviceBringUpCheck/README.md) to distinguish begin() from host configuration.
2. Use [Serial](../examples/Serial/README.md) to distinguish diagnostics from the new CDC port.
3. Try [Keyboard](../examples/Keyboard/README.md) or [Gamepad](../examples/Gamepad/README.md) in the matching host application.
4. Use [DescriptorDump](../examples/Info/EspUsbDeviceDescriptorDump/README.md) to relate function declarations to interfaces and endpoints.
5. Progress to composite functions, audio, storage and updates after one function is understood.

The [complete example index](../examples/README.md) links to each guide's components, operation, APIs and adaptation notes. Keep an example's whole folder when it has adjacent protocol helpers.

## Install and build

1. Follow the [library README](../README.md) to install Arduino-ESP32 and the library. Use its support/compatibility references for target and core requirements, then inspect the example's profile.
2. Open File → Examples → EspUsbDevice in Arduino IDE and select the exact board. From the repository root, a CLI build can use:

```sh
arduino-cli compile --profile esp32s3 examples/Serial
```

3. `sketch.yaml` declares that profile's platform and libraries. Check local-library references; examples with external libraries or generated assets require the adjacent README's preparation. Copying only the .ino can omit helpers.
4. Upload through a known working route and open diagnostics using the sketch's Serial.begin() settings. A successful compile does not verify physical wiring or interoperability.

## Identify connectors and power first

A connector marked USB may lead to OTG data pads, USB Serial/JTAG or a USB-UART bridge. Check the schematic for the controller, D−/D+, PHY, VBUS and USB-C CC/role circuitry. Use a data-capable cable and compatible signal levels.

A host supplies the required VBUS to a peripheral; a device connects to a host. Follow the board's power design rather than tying independent supply outputs together. Selecting a P4 controller or PHY does not automatically change connector wiring or power circuitry.

Serial is the sketch's diagnostic path. Depending on board/settings it may use USB or UART; verify where it can actually be read. Treat it separately from CDC or HID under test. Before moving Serial/JTAG pads, arrange an independent log/upload recovery route such as UART.

## Read the sketch in order

1. Read the overview and pin/format/command constants, including their units.
2. Identify the global USB owner and the functions/helpers registered on it.
3. Follow callback registration, medium/format preparation and begin() in setup().
4. Follow buffered input, state changes, periodic work and pending requests in loop().
5. Distinguish success booleans, actual byte counts and state queries. Failure does not automatically implement a retry policy.

## Interpret state precisely

| Observation | Meaning |
| --- | --- |
| `device.begin(config)` succeeds | Internal startup; host enumeration may still be pending |
| `device.ready()` | The host has selected a configuration |
| CDC `connected()` | Host DTR state, different from physical connection and other classes |
| Audio/video stream starts | The host is using that stream; enumeration alone does not start it |
| send/write succeeds | The API's acceptance/completion condition, not proof of application processing |

USB IN/OUT is always named from the device's viewpoint: IN is device→host, OUT is host→device. A request or response uses whichever direction its protocol defines; the naming does not reverse when application roles change.

## Separate callbacks from loop()

USB callbacks run on a USB processing task separate from loop(). Registering one does not require calling it manually from loop(). Long logging, waiting, decoding or file I/O in a callback delays subsequent USB work.

HID Output notifications, control requests and MSC/APDU response providers also participate in USB processing. Response providers check lengths/capacity and return promptly; move expensive parsing, file work and reboot decisions into the application path shown by the example.

Callback data/report/frame pointers can be recycled on return. Copy what must outlive the callback into owned storage and define its handoff/completion point. An asynchronous/direct transfer buffer must also remain unchanged until completion. Do not confuse a copying write contract with a borrowed direct-buffer contract.

volatile alone does not provide mutual exclusion, ordering or consistency across several fields shared between tasks. Use a synchronized handoff such as a FreeRTOS queue, submit without waiting from callbacks, and decide whether full queues cause dropping, counting or retry. Do not wait in USB/Serial operations while holding a critical section.

## Finite buffers and failures

- Interpret only the actual read length. A requested size need not arrive in full, and one callback need not represent one command.
- A short/failed write needs an explicit policy: retain the unsent suffix, or count a deliberate discard. Logging an error is not recovery.
- Pair HID presses/releases and MIDI Note On/Off; respect frame ownership for video and distinguish samples, frames and bytes for audio.
- Distinguish storage blocks from filesystems. Give a host-mounted medium only one independent writer.
- Keep heavy print output out of callbacks and throughput measurements; terminal speed can limit the experiment.

## Troubleshooting by stage

| Symptom | Next check |
| --- | --- |
| No diagnostics | Board settings, Serial route, terminal parameters and port-routing changes |
| begin() fails | Immediate error name, controller choice, stack conflicts and required resources |
| Startup succeeds but no enumeration | Data connector/cable, VBUS/roles, selected controller and PHY |
| Enumerates but class does not work | Interfaces/endpoints, report descriptor and peer format support |
| Works initially or loses data under load | Actual lengths, accepted counts, full queues, callback duration and buffer lifetime |
| Fails after reconnect | Stale address/session, DTR, held keys and pending-transfer state |

Try peer absent, connected, normal operation, disconnect and reconnect in order, recording each stage. Before settings, printing or Flash updates, read the individual guide to understand the physical operation.

For principles see the [USB development guide](usb-device-guide.md), for execution/transfer contracts the [advanced guide](usb-device-advanced.md), and for public APIs the [library README](../README.md). Keep version-specific support and measurements in their dedicated references.
