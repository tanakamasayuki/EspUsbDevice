"""BOT (USB Mass Storage Bulk-Only Transport) client for the PC-side MSC tests."""
import struct

import usb.core
import usb.util

BLOCK = 512
TUR = bytes(6)
SENSE = bytes([0x03, 0, 0, 0, 18, 0])


def rw10(op, lba, blocks):
    return bytes([op, 0]) + lba.to_bytes(4, "big") + b"\0" + blocks.to_bytes(2, "big") + b"\0"


class Bot:
    def __init__(self, dev):
        self.dev, self.tag = dev, 0
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)  # usb-storage would issue its own commands
        usb.util.claim_interface(dev, 0)
        intf = dev.get_active_configuration()[(0, 0)]
        self.ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: e.bEndpointAddress & 0x80)
        self.ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: not e.bEndpointAddress & 0x80)
        assert self.ep_in.wMaxPacketSize == 512 and self.ep_out.wMaxPacketSize == 512

    def command(self, cdb, length=0, data_in=True, payload=b""):
        self.tag += 1
        self.ep_out.write(struct.pack("<IIIBBB16s", 0x43425355, self.tag, length,
                                      0x80 if data_in else 0, 0, len(cdb), cdb), timeout=1000)
        data, stalled = b"", False
        try:
            # One block per call, so the bytes delivered before a STALL are counted.
            while len(data) < length if data_in else len(data) < len(payload):
                if data_in:
                    data += bytes(self.ep_in.read(BLOCK, timeout=1000))
                else:
                    self.ep_out.write(payload[len(data):len(data) + BLOCK], timeout=1000)
                    data += payload[len(data):len(data) + BLOCK]
        except usb.core.USBError as exc:
            assert exc.errno == 32, exc
            stalled = True
            self.dev.clear_halt(self.ep_in if data_in else self.ep_out)
        try:
            csw = bytes(self.ep_in.read(13, timeout=1000))
        except usb.core.USBError as exc:
            assert exc.errno == 32, exc
            self.dev.clear_halt(self.ep_in)
            csw = bytes(self.ep_in.read(13, timeout=1000))
        signature, tag, residue, status = struct.unpack("<IIIB", csw)
        assert signature == 0x53425355 and tag == self.tag
        return {"bytes": len(data), "stalled": stalled, "status": status, "residue": residue, "data": data}

    def sense(self):
        r = self.command(SENSE, 18)
        assert r["status"] == 0
        d = r["data"]
        return d[2] & 0x0F, d[12], d[13]

    def tur(self):
        return self.command(TUR)["status"]

    def reset_recovery(self):
        self.dev.ctrl_transfer(0x21, 0xFF, 0, 0, None, timeout=1000)
        self.dev.clear_halt(self.ep_in)
        self.dev.clear_halt(self.ep_out)
