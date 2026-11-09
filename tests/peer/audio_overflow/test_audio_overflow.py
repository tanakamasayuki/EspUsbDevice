"""Playback overflow policies compared frame by frame with the specified model."""

NEWEST, OLDEST, DEFAULT = 0, 1, 2


class Model:
    def __init__(self, policy, capacity, frame):
        self.policy, self.frame = policy, frame
        self.slots = capacity // frame
        self.queue, self.next = [], 0
        self.transferred = self.overruns = self.dropped = 0

    def packet(self, length):
        whole = length // self.frame
        frames = list(range(self.next, self.next + whole))
        self.next += whole
        if self.policy == NEWEST:
            kept = min(whole, self.slots - len(self.queue))
            self.queue += frames[:kept]
            lost = length - kept * self.frame
        else:
            kept = min(whole, self.slots)
            pushed = max(0, len(self.queue) + kept - self.slots)
            self.queue = self.queue[pushed:] + frames[whole - kept:]
            lost = pushed * self.frame + length - kept * self.frame
        self.transferred += length
        if lost:
            self.overruns += 1
            self.dropped += lost

    def read(self, count):
        out, self.queue = self.queue[:count], self.queue[count:]
        return out


def runs(frames):
    if not frames:
        return "none"
    out, start = [], frames[0]
    for prev, cur in zip(frames, frames[1:] + [None]):
        if cur != prev + 1 if cur is not None else True:
            out.append(f"{start}-{prev}")
            start = cur
    return ",".join(out)


# (frames in the ring, alternate, operations). Ring bytes are frames x 8, the
# largest registered PCM frame. "full" is the largest whole-frame packet and
# "tail" ends with two bytes of an incomplete frame.
SCENARIOS = (
    (65, 1, (("send", 2, "full"), ("read", 10), ("send", 6, "full"), ("send", 1, "tail"), ("dump",))),
    (4, 1, (("send", 1, "full"), ("send", 1, "tail"), ("dump",))),
    (65, 2, (("send", 8, "full"), ("dump",))),
    (5, 3, (("send", 2, "tail"), ("read", 2), ("send", 1, "full"), ("dump",))),
    (0, 1, (("send", 3, "full"), ("dump",))),
)


def test_audio_overflow(dut, peers):
    device = peers["device"]

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def check(tag, model, frames):
        expected = model.read(frames)
        device.write(f"read {frames}" if tag == "READ" else "dump")
        r = device.expect(
            tag + r" valid=1 frames=(\d+) seq=(\S+) left=(\d+) "
            r"transferred=(\d+) overruns=(\d+) dropped=(\d+)\r\n", timeout=10)
        got = tuple(v.decode() if i == 1 else int(v) for i, v in enumerate(r.groups()))
        want = (len(expected), runs(expected), len(model.queue) * model.frame,
                model.transferred, model.overruns, model.dropped)
        assert got == want, f"{tag}: device {got} != model {want}"

    def run(policy, frames, alternate, operations):
        command(device, f"start {policy} {frames}",
                f"STARTED 1 policy={OLDEST if policy == DEFAULT else policy} capacity={frames * 8}", 30)
        dut.write("start")
        packets = {int(m.group(1)): (int(m.group(2)), int(m.group(3)))
                   for m in (dut.expect(r"CHOICE alt=(\d) ep=\d+ bytes=(\d) packet=(\d+)\r\n", timeout=30)
                             for _ in range(4))}
        dut.expect_exact("HOST_READY 1\r\n", timeout=30)
        bytes_, packet = packets[alternate]
        frame = 2 * bytes_
        full = packet // frame * frame
        lengths = {"full": full, "tail": full - frame + 2}
        model = Model(OLDEST if policy == DEFAULT else policy, frames * 8, frame)
        command(dut, f"alt {alternate}", "SELECTED 1")
        for op in operations:
            if op[0] == "send":
                for _ in range(op[1]):
                    model.packet(lengths[op[2]])
                command(dut, f"send {op[1]} {lengths[op[2]]}", f"SENT ok=1 next={model.next} lost=0", 20)
            else:
                check("READ" if op[0] == "read" else "DUMP", model, op[1] if op[0] == "read" else 1 << 30)
        # Leaving the alternate discards what is still queued.
        command(dut, "send 2 " + str(full), f"SENT ok=1 next={model.next + 2 * (full // frame)} lost=0", 20)
        command(dut, "pause", "PAUSED 1")
        device.write("empty")
        device.expect(r"EMPTY left=0 transferred=\d+\r\n", timeout=10)
        for board in (dut, device):
            command(board, "stop", "STOPPED", 30)
        return model

    try:
        for policy in (NEWEST, OLDEST):
            for frames, alternate, operations in SCENARIOS:
                run(policy, frames, alternate, operations)
        run(DEFAULT, *SCENARIOS[0])
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
