"""Standard interface and endpoint changes return only affected stream loans."""


def test_bulk_standard(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64
    full, short, over, wide = mps, mps - 1, mps + 1, 2 * mps + 1

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def queue(i, direction="both"):
        command(device, f"queue {i} {direction}", "QUEUED 1")

    try:
        for kind in (0, 1):
            counts = [[0, 0], [0, 0]]
            statuses = [["Stopped", "Stopped"], ["Stopped", "Stopped"]]

            def returned(i, tx=None, rx=None):
                for direction, value in enumerate((tx, rx)):
                    if value is not None:
                        counts[i][direction] += 1
                        statuses[i][direction] = value

            def stats(i, tx_pending=0, rx_pending=0):
                tx, rx = counts[i]
                ts, rs = statuses[i]
                command(device, f"stats {i} {tx} {rx}",
                        f"STATS {i} tx={tx} rx={rx} txStatus={ts} rxStatus={rs} "
                        f"txPending={tx_pending} rxPending={rx_pending} bad=0")

            command(device, f"start {kind} 0", "STARTED 1", 30)
            command(dut, "start", "HOST_READY 1", 30)
            if kind:
                command(dut, "coding 7 0", "CODING 1")
                for short_length in range(7):
                    command(dut, f"coding {short_length} 0", "CODING 1")
                for invalid in range(1, 6):
                    command(dut, f"coding 7 {invalid}", "CODING 1")
                command(device, "coding_count", "CODING_COUNT 1")
            # Reset each data interface independently, including CDC's data
            # interface, while both Functions have partial TX and RX loans.
            for target in (0, 1):
                for i in (0, 1):
                    command(dut, f"alternate {i}", "ALTERNATE 1")
                    queue(i)
                    command(dut, f"read {i} {full}", "BULK 1")
                    command(dut, f"write {i} {full}", "BULK 1")
                if kind:
                    command(dut, "control", "CONTROL 1")
                    command(device, "cdc", "CDC 1")
                    stats(1, 1, 1)
                command(dut, f"alternate {target}", "ALTERNATE 1")
                returned(target, "Cancelled", "Cancelled")
                stats(target)
                stats(1 - target, 1, 1)
                queue(target)
                for i in (target, 1 - target):
                    command(dut, f"read {i} {wide if i == target else over}", "BULK 1")
                    command(dut, f"write {i} {short}", "BULK 1")
                    returned(i, "Success", "Success")
                    stats(i)
            # Start the affected host pipe at DATA0. Keep the other direction
            # at DATA1 with a partial transfer to catch accidental toggle resets.
            for i in (0, 1):
                for halt in (True, False):
                    for input_direction in (1, 0):
                        command(dut, f"alternate {i}", "ALTERNATE 1")
                        queue(i)
                        command(dut, f"{'write' if input_direction else 'read'} {i} {full}", "BULK 1")
                        command(dut, f"{'halt' if halt else 'clear'} {i} {input_direction}", "FEATURE 1")
                        status = "TransferError" if halt else "Cancelled"
                        returned(i, status if input_direction else None, None if input_direction else status)
                        stats(i, 0 if input_direction else 1, 1 if input_direction else 0)
                        if halt:
                            command(dut, f"clear {i} {input_direction}", "FEATURE 1")
                        queue(i, "tx" if input_direction else "rx")
                        command(dut, f"read {i} {wide if input_direction else over}", "BULK 1")
                        command(dut, f"write {i} {short}", "BULK 1")
                        returned(i, "Success", "Success")
                        stats(i)
                # Also halt after partial data in both directions. Recreate
                # the host's pipes only after both affected loans are returned;
                # endpoint_clear resumes IDF's pipe without resetting its PID.
                command(dut, f"alternate {i}", "ALTERNATE 1")
                queue(i)
                command(dut, f"read {i} {full}", "BULK 1")
                command(dut, f"write {i} {full}", "BULK 1")
                for direction in (1, 0):
                    command(dut, f"halt {i} {direction}", "FEATURE 1")
                returned(i, "TransferError", "TransferError")
                stats(i)
                for direction in (1, 0):
                    command(dut, f"clear {i} {direction}", "FEATURE 1")
                command(dut, f"reclaim {i}", "RECLAIMED 1")
                queue(i)
                command(dut, f"read {i} {wide}", "BULK 1")
                command(dut, f"write {i} {short}", "BULK 1")
                returned(i, "Success", "Success")
                stats(i)
            if kind:
                command(device, "cdc", "CDC 1")
            for board in (dut, device):
                command(board, "stop", "STOPPED", 30)
        # Copy queues recover without retaining old DMA packets or RX bytes.
        command(device, "start 0 1", "STARTED 1", 30)
        command(dut, "start", "HOST_READY 1", 30)
        command(device, f"copytx 0 {wide}", "COPIED 1")
        command(dut, f"read 0 {full}", "BULK 1")
        command(dut, f"write 0 {full}", "BULK 1")
        command(dut, "alternate 0", "ALTERNATE 1")
        command(device, "copyrx 0 0", "COPY_RX 1")
        command(device, f"copytx 0 {short}", "COPIED 1")
        command(dut, f"read 0 {short}", "BULK 1")
        command(dut, f"write 0 {short}", "BULK 1")
        command(device, f"copyrx 0 {short}", "COPY_RX 1")
        command(dut, "alternate 0", "ALTERNATE 1")
        command(device, f"copytx 0 {wide}", "COPIED 1")
        command(dut, f"read 0 {full}", "BULK 1")
        command(dut, f"write 0 {full}", "BULK 1")
        for direction in (1, 0):
            command(dut, f"halt 0 {direction}", "FEATURE 1")
        command(device, "blocked", "BLOCKED 1")
        for direction in (1, 0):
            command(dut, f"clear 0 {direction}", "FEATURE 1")
        command(dut, "reclaim 0", "RECLAIMED 1")
        command(device, "copyrx 0 0", "COPY_RX 1")
        command(device, f"copytx 0 {short}", "COPIED 1")
        command(dut, f"read 0 {short}", "BULK 1")
        command(dut, f"write 0 {short}", "BULK 1")
        command(device, f"copyrx 0 {short}", "COPY_RX 1")
    finally:
        errors = []
        for name, board in (("host", dut), ("device", device)):
            try:
                command(board, "stop", "STOPPED", 30)
            except Exception as exc:
                errors.append(f"{name}: {exc}")
        assert not errors, "USB stop failed: " + "; ".join(errors)
