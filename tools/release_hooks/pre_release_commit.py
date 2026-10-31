"""Release-branch hook: keep the hardware test bench out of the library release.

The shared release workflow removes tests/ itself; validation/ (the physical
USB test bench and its probes) is not part of the Arduino library either.
"""
import subprocess

subprocess.run(["git", "rm", "-r", "-q", "--ignore-unmatch", "validation"], check=True)
