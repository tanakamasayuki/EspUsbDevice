"""Two borrowed sends outstanding: one on the wire, one queued behind it.

A third submission is Busy. The queued send goes out after the first without a
task in between, and both come back exactly once and in order: sent, cancelled
(by the current id or by the queued id) or stopped with the device.
"""

SUCCESS, STOPPED, CANCELLED = 0, 2, 4
BUSY = 6


def test_direct_queue(dut, peers, request):
    device = peers["device"]
    mps = 512 if "_hs_" in request.node.name else 64  # bulk MPS of the link
    first, second = mps - 1, mps - 2  # short packets: each ends one host read

    def command(board, text, reply, timeout=10):
        board.write(text)
        board.expect_exact(reply + "\r\n", timeout=timeout)

    def post_two():
        command(device, "clear", "CLEARED")
        command(device, f"post {first} 0", "POSTED 1 error=0")
        command(device, f"post {second} 1", "POSTED 1 error=0")
        command(device, f"post {first} 2", f"POSTED 0 error={BUSY}")

    try:
        command(device, "start", "DEVICE_STARTED 1", 30)
        command(dut, "start", f"HOST_READY mps_in={mps} mps_out={mps}", 30)

        # Sent in order; the host reads the first block, then the second.
        post_two()
        command(dut, f"receive {first}", f"READ_OK {first}")
        command(dut, f"receive {second}", f"READ_OK {second}")
        command(device, "wait 2", f"DONE n=2 0:{SUCCESS}:{first}:1 1:{SUCCESS}:{second}:1")

        # Cancelling the current send returns the queued one too, after it.
        post_two()
        command(device, "cancel", "CANCEL 0")
        command(device, "wait 2", f"DONE n=2 0:{CANCELLED}:0:1 1:{CANCELLED}:0:1")

        # Cancelling by the queued id withdraws the send ahead of it as well.
        post_two()
        command(device, "cancel_queued", "CANCEL 0")
        command(device, "wait 2", f"DONE n=2 0:{CANCELLED}:0:1 1:{CANCELLED}:0:1")

        # The bus still works after both cancellations.
        command(device, "clear", "CLEARED")
        command(device, f"post {first} 0", "POSTED 1 error=0")
        command(dut, f"receive {first}", f"READ_OK {first}")
        command(device, "wait 1", f"DONE n=1 0:{SUCCESS}:{first}:1")

        # Stopping the device returns both, first then queued.
        post_two()
        command(device, "stop", "STOPPED 0", 30)
        command(device, "wait 2", f"DONE n=2 0:{STOPPED}:0:1 1:{STOPPED}:0:1")
    finally:
        dut.write("stop")
        device.write("stop")
