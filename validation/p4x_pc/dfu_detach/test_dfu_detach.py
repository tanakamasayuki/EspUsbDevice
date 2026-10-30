"""Measure DFU DETACH timing on the P4X HS controller with the Linux PC as Host."""

import json
import pathlib
import time

import usb.core
import usb.util

SERIAL = "espusb-dfu-detach"
RESULTS = pathlib.Path(__file__).resolve().parents[2] / ".pytest-results" / "dfu-detach-measure.json"


def find(timeout=10.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        for d in usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x0001):
            try:
                if usb.util.get_string(d, d.iSerialNumber) == SERIAL:
                    return d
            except (usb.core.USBError, ValueError):
                pass
        time.sleep(0.005)
    return None


def present(dev):
    return any(d.bus == dev.bus and d.address == dev.address
               for d in usb.core.find(find_all=True, idVendor=0x1209, idProduct=0x0001))


def wait_gone(dev, t0, timeout=3.0):
    while time.monotonic() - t0 < timeout:
        if not present(dev):
            return round((time.monotonic() - t0) * 1000, 1)
        time.sleep(0.001)
    return None


busy_retries = 0


def detach(dev, w_value):
    """EBUSY is the PC failing to claim the interface (another process probing
    the new device); the request never reached the bus, so send it again."""
    global busy_retries
    for _ in range(100):
        t0 = time.monotonic()
        try:
            dev.ctrl_transfer(0x21, 0, w_value, 0, None, timeout=1000)
            result = "ok"
        except usb.core.USBError as exc:
            if exc.errno == 16:
                busy_retries += 1
                time.sleep(0.01)
                continue
            result = "pipe" if exc.errno == 32 else f"error{exc.errno}"
        return result, t0, round((time.monotonic() - t0) * 1000, 2)
    return "busy", t0, 0


def get_state(dev):
    return dev.ctrl_transfer(0xa1, 5, 0, 0, 1, timeout=1000)[0]


def query_boot(dut, timeout=10):
    end = time.monotonic() + timeout
    while True:
        dut.write("boot")
        try:
            m = dut.expect(r"BOOT count=(\d+) reason=(\d+)\r\n", timeout=0.5)
            return int(m.group(1)), int(m.group(2))
        except Exception:
            if time.monotonic() > end:
                raise


def boot(dut, timeout=10):
    """Wait for the next boot; returns its reset reason."""
    global last_count
    end = time.monotonic() + timeout
    while True:
        count, reason = query_boot(dut, max(1, end - time.monotonic()))
        if count != last_count:
            last_count = count
            return reason
        if time.monotonic() > end:
            raise TimeoutError(f"no new boot after count {last_count}")
        time.sleep(0.05)


last_count = None


def start(dut, mode, delay, timeout=1000, end_after=-1, refuse=0):
    dut.write(f"start {mode} {delay} {timeout} {end_after} {refuse}")
    dut.expect_exact("STARTED 1\r\n", timeout=10)
    dev = find()
    assert dev is not None, "DFU runtime device did not enumerate on the PC"
    assert dev.speed == usb.util.SPEED_HIGH
    # The serial string is readable before Linux sends SET_CONFIGURATION.
    end = time.monotonic() + 3
    while time.monotonic() < end:
        try:
            if dev.ctrl_transfer(0x80, 8, 0, 0, 1, timeout=200)[0] == 1:
                return dev
        except usb.core.USBError:
            pass
        time.sleep(0.005)
    raise AssertionError("DFU runtime device was not configured")


def test_dfu_detach(dut):
    results = {"restart_delay": [], "timer": [], "end_race": [], "reset_race": []}
    reasons = []
    try:
        global last_count
        last_count, _ = query_boot(dut, 30)
        # A: restart delay after the status stage of an accepted DETACH.
        for delay in (0, 1, 5, 20, 100):
            for _ in range(10):
                dev = start(dut, 0, delay)
                result, t0, took = detach(dev, 1000)
                gone = wait_gone(dev, t0)
                trial = {"delay": delay, "result": result, "ctrl_ms": took, "gone_ms": gone}
                results["restart_delay"].append(trial)
                trial["reason"] = boot(dut)
                reasons.append(trial["reason"])
        # B: appDETACH timer with a handler that does not leave.
        dev = start(dut, 1, 0, 1000)
        for w_value in (0, 30, 300, 3000):
            result, t0, took = detach(dev, w_value)
            dut.expect(r"DETACHED n=\d+ at=\d+\r\n", timeout=5)
            again, _, _ = detach(dev, w_value)
            first = get_state(dev)
            while get_state(dev) == 1 and time.monotonic() - t0 < 5:
                time.sleep(0.002)
            results["timer"].append({
                "wValue": w_value, "result": result, "second_detach": again, "state_after": first,
                "appdetach_ms": round((time.monotonic() - t0) * 1000, 1), "state_end": get_state(dev)})
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=10)
        # E: onDetach refusing at SETUP stalls the request and changes nothing.
        dev = start(dut, 1, 0, 1000, -1, 2)
        refusals = [detach(dev, 1000)[0] for _ in range(2)]
        states = [get_state(dev)]
        accepted = detach(dev, 1000)[0]
        states.append(get_state(dev))
        dut.write("counts")
        counts = dut.expect(r"COUNTS asked=(\d+) detached=(\d+)\r\n", timeout=5)
        results["refusal"] = {"refusals": refusals, "accepted": accepted, "states": states,
                              "asked": int(counts.group(1)), "detached": int(counts.group(2))}
        assert refusals == ["pipe", "pipe"] and accepted == "ok" and states == [0, 1]
        assert (int(counts.group(1)), int(counts.group(2))) == (3, 1)
        dut.write("stop")
        dut.expect_exact("STOPPED\r\n", timeout=10)
        # C: device.end() racing a restart scheduled 100 ms after the status stage.
        for end_after in (0, 20, 50, 90):
            for _ in range(3):
                dev = start(dut, 0, 100, 1000, end_after)
                result, t0, _ = detach(dev, 1000)
                m = dut.expect(r"ENDED err=(-?\d+) after=(\d+) took=(\d+)\r\n", timeout=5)
                reason = boot(dut)
                reasons.append(reason)
                results["end_race"].append({"end_after": end_after, "result": result, "end_err": int(m.group(1)),
                                            "end_at": int(m.group(2)), "end_took": int(m.group(3)), "reason": reason})
        # D: Host port reset right after the accepted DETACH.
        for _ in range(5):
            dev = start(dut, 0, 100)
            result, t0, _ = detach(dev, 1000)
            try:
                dev.reset()
                reset = "ok"
            except usb.core.USBError as exc:
                reset = f"error{exc.errno}"
            reset_ms = round((time.monotonic() - t0) * 1000, 1)
            reason = boot(dut)
            reasons.append(reason)
            results["reset_race"].append({"result": result, "reset": reset, "reset_ms": reset_ms, "reason": reason})
    finally:
        RESULTS.parent.mkdir(exist_ok=True)
        results["busy_retries"] = busy_retries
        RESULTS.write_text(json.dumps(results, indent=1))
        dut.write("stop")
    # esp_restart() is ESP_RST_SW (3); a panic would be 4.
    assert all(r == 3 for r in reasons), reasons
