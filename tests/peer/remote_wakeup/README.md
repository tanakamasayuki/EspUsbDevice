# Remote wakeup

> 日本語: [README.ja.md](README.ja.md)

Checks that the Host observes the Device's remote wakeup request as a real wake, and the enable, clear and reset boundaries.
Runs at FS on S3 / S2 (combos `s3`, `s3_s2`, `s2_s3`) and on the P4 / P4X HS link (`p4_p4x_hs`, `p4x_p4_hs`).

## Purpose

- A wake signal appears on the Device's request only when the Host enabled it with SET_FEATURE(DEVICE_REMOTE_WAKEUP).
- Without that enable, after CLEAR_FEATURE and after a bus reset, the request is refused and no signal appears.
- After the wake, a payload makes a round trip.
- Requests and queries racing `end()` are refused from the moment the stop begins.

## Steps

- The Host suspends the bus, detects the real wake status raised by the Device's request, then resumes.
- The ESP-IDF HCD in use does not handle the remote wake event.
  So the probe holds the USB IRQ only while no transfer is in flight, restores the root port including clock gating and ungating, clears the pending wake, and returns the IRQ to its original setting.
- No SDK or pytest patch and no conftest is used.
- Requests pending across a reset, duplicate requests and calls racing `end()` are also tried.
  With the worker held, the runtime stop begins; queries and requests from another task must be refused before the worker resumes. Queries and requests after `end()` returns must be refused too.
- The Device-side register observations and event hooks are for diagnosis and are not part of the public API.

References: [USB 2.0](https://www.usb.org/document-library/usb-20-specification) §7.1.7.7 (Resume), §9.4.1 / §9.4.9 (Clear / Set Feature)
