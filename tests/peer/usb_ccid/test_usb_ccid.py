"""USB CCID smart card reader peer test.

DUT = the USB host (EspUsbHost, ``usb_ccid.ino``); the peer = the EspUsbDevice
CCID reader (``peer_device/``), whose slot holds an emulated contactless card.

The device side is what is under test here: the CCID class descriptor it
declares, the RDR_to_PC answers it builds for each PC_to_RDR message, the ATR it
returns on activation, the APDU exchanges, PC_to_RDR_Escape, and the interrupt
endpoint's slot change notifications. EspUsbHost 2.7.1's ccid* API is the
instrument.
"""

import time

import pexpect

# The ATR the device answers IccPowerOn with: the PC/SC synthetic ATR for a
# MIFARE Classic 1K, so the host can also name the card from it.
CARD_ATR = "3b8f8001804f0ca000000306030001000000006a"
CARD_UID = "04112233"


def _expect_events(dut, pattern, retries=15, timeout=2):
    """Poll the host's slot-change counters until `pattern` matches.

    A notification is not a reply: the device queues it on its interrupt IN
    endpoint and the host picks it up on its next poll, one bInterval (16 ms)
    later plus however long the host takes to re-arm. A single read right after
    the card moved can therefore run ahead of the event, so poll instead of
    assuming the counters are already up to date.
    """
    last = None
    for _ in range(retries):
        dut.write("n")
        try:
            dut.expect(pattern, timeout=timeout)
            return
        except pexpect.TIMEOUT as err:
            last = err
    raise last


def _open(dut, device, *, card: bool):
    """Bring both sides to a known state: interface open, card in or out."""
    device.write("?")
    device.expect_exact("DEVICE_READY")
    device.write("i" if card else "r")
    device.expect(r"DEVICE_CARD inserted=\d present=" + ("1" if card else "0"))

    dut.write("o")
    dut.expect_exact("CCID_OPEN 1")


def test_usb_ccid_enumeration(dut, peers):
    """The reader's descriptors are what a CCID host expects to find.

    Interface class 0x0b with bulk IN / bulk OUT / interrupt IN, and a CCID class
    descriptor the host can parse: one slot, T=1, short APDU level exchange. The
    exchange level is the load-bearing one - it is what tells a host it may send
    whole APDUs instead of TPDUs.
    """
    device = peers["device"]

    dut.expect_exact("HOST_CONNECTED")
    device.write("?")
    device.expect_exact("DEVICE_READY")

    dut.write("i")
    dut.expect_exact("INTERFACE number=0 class=0x0b subclass=0x00 protocol=0x00 endpoints=3")
    dut.expect_exact("ENDPOINT iface=0 ep=0x01 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("ENDPOINT iface=0 ep=0x81 attrs=0x02 mps=64 interval=0")
    dut.expect_exact("ENDPOINT iface=0 ep=0x82 attrs=0x03 mps=8 interval=16")
    dut.expect_exact("CCID_ENUM interface=1 bulk_in=1 bulk_out=1 interrupt_in=1")

    dut.write("o")
    dut.expect_exact("CCID_OPEN 1")

    dut.write("d")
    dut.expect_exact(
        "CCID_INTERFACE iface=0 in=0x81 out=0x01 interrupt=0x82 classDesc=1 bcd=0110 "
        "slots=1 voltage=0x07 protocols=0x00000002 features=0x000204fe "
        "maxMessage=271 exchange=2"
    )


def test_usb_ccid_slot_status_follows_the_card(dut, peers):
    """GetSlotStatus reports what the sketch put in the slot.

    Absent, then present but not activated, then active after IccPowerOn: the
    three ICC states a reader can report, driven from the device side.
    """
    device = peers["device"]
    _open(dut, device, card=False)

    dut.write("s")
    dut.expect_exact("CCID_STATUS ok=1 icc=absent present=0 active=0 command=0 error=0x00")

    # No card: activation must fail, and with ICC_MUTE rather than a bus error.
    dut.write("p")
    dut.expect_exact("CCID_POWER_ON ok=0 len=0 error=0xfe")

    device.write("i")
    device.expect_exact("DEVICE_CARD inserted=1 present=1")

    dut.write("s")
    dut.expect_exact("CCID_STATUS ok=1 icc=inactive present=1 active=0 command=0 error=0x00")

    dut.write("p")
    dut.expect_exact("CCID_POWER_ON ok=1 len=20 error=0x00")
    dut.expect_exact(f"CCID_ATR data={CARD_ATR}")

    dut.write("s")
    dut.expect_exact("CCID_STATUS ok=1 icc=active present=1 active=1 command=0 error=0x00")

    device.write("p")
    device.expect(r"DEVICE_POWER on=[1-9][0-9]* off=\d+ escape=\d+")

    dut.write("f")
    dut.expect_exact("CCID_POWER_OFF 1")
    dut.write("s")
    dut.expect_exact("CCID_STATUS ok=1 icc=inactive present=1 active=0 command=0 error=0x00")


def test_usb_ccid_apdu_exchange(dut, peers):
    """XfrBlock carries whole APDUs to the sketch and its answer back.

    Three cases: the PC/SC Get UID pseudo APDU, an echo instruction that proves
    the request payload arrives intact, and an instruction the card does not
    implement - which must come back as a successful exchange carrying the card's
    own 6D00, not as a failed CCID command.
    """
    device = peers["device"]
    _open(dut, device, card=True)

    dut.write("p")
    dut.expect_exact("CCID_POWER_ON ok=1 len=20 error=0x00")

    dut.write("a")
    dut.expect_exact("CCID_APDU ok=1 sw=9000 len=4")
    dut.expect_exact(f"CCID_APDU data={CARD_UID}")

    dut.write("e")
    dut.expect_exact("CCID_ECHO ok=1 sw=9000 len=4")
    dut.expect_exact("CCID_ECHO data=deadbeef")

    dut.write("x")
    dut.expect_exact("CCID_UNKNOWN ok=1 sw=6d00 len=0")

    # The ATR identifies the card, which is the point of answering with a PC/SC
    # synthetic one rather than an arbitrary byte string.
    dut.write("g")
    dut.expect_exact(
        'CCID_CARD ok=1 standard="ISO 14443 A" code=0x03 level=3 '
        'name="MIFARE Classic 1K" nameCode=0x0001 pcsc=1'
    )

    # bSeq stayed in step across all of the above: a raw GetSlotStatus still gets
    # its own answer back (the host drops responses whose bSeq does not match).
    dut.write("m")
    dut.expect_exact("CCID_MESSAGE ok=1 type=0x81 status=0x00 error=0x00 len=0")

    device.write("s")
    device.expect(r"DEVICE_STATUS mounted=1 present=1 powered=1 commands=\d+ apdus=\d+ last=0x65")


def test_usb_ccid_escape_and_parameters(dut, peers):
    """The messages either side of the APDU path: Escape and GetParameters.

    Escape is the vendor-specific channel, answered by the sketch's onEscape();
    GetParameters is answered by the class itself and must come back as
    RDR_to_PC_Parameters for T=1.
    """
    device = peers["device"]
    _open(dut, device, card=True)

    dut.write("c")
    dut.expect_exact("CCID_ESCAPE ok=1 len=3")
    dut.expect_exact("CCID_ESCAPE data=020304")

    dut.write("P")
    dut.expect_exact("CCID_PARAMETERS ok=1 type=0x82 protocol=1 len=7")

    # ABORT is a class request followed by PC_to_RDR_Abort; after it the slot has
    # to be usable again rather than stuck refusing commands.
    dut.write("A")
    dut.expect_exact("CCID_ABORT 1")
    dut.write("s")
    dut.expect("CCID_STATUS ok=1 icc=(inactive|active) present=1")

    device.write("p")
    device.expect(r"DEVICE_POWER on=\d+ off=\d+ escape=[1-9]\d*")


def test_usb_ccid_slot_change_notifications(dut, peers):
    """Taking the card out and putting it back reaches the host as events.

    RDR_to_PC_NotifySlotChange on the interrupt endpoint is the only part of the
    device that speaks without being asked, so nothing else covers that endpoint.

    Two things shape how this has to be written. The host reports a *change*
    against what it believes the slot holds, and it updates that belief from
    every RDR_to_PC response as well as from notifications - so a GetSlotStatus
    between the card moving and the notification arriving would bring the host up
    to date silently and the notification would no longer be a change to report.
    Hence no bulk command runs inside the loop below. And because the starting
    belief depends on what earlier tests left behind, the card is toggled twice:
    that yields at least one insertion and one removal event either way.
    """
    device = peers["device"]
    _open(dut, device, card=True)
    # _open() moved the card too; let its notification land before counting.
    time.sleep(0.3)

    dut.write("z")
    dut.expect_exact("CCID_EVENTS_RESET")

    for command in ("r", "i", "r", "i"):
        device.write(command)
        device.expect(r"DEVICE_CARD inserted=\d present=\d")
        # Let the notification be polled (bInterval is 16 ms) before moving the
        # card again: the endpoint holds one notification at a time, so a second
        # change arriving first would replace the state in flight rather than
        # queue behind it.
        time.sleep(0.3)

    _expect_events(dut, r"CCID_EVENTS inserted=[1-9]\d* removed=[1-9]\d* present=1")

    # Now that the events have been observed, the bulk path must agree that the
    # card ended up back in the slot.
    dut.write("s")
    dut.expect_exact("CCID_STATUS ok=1 icc=inactive present=1 active=0 command=0 error=0x00")
