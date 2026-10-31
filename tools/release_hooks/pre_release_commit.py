"""Release-branch hook: keep development-only trees out of the library release.

The shared release workflow removes tests/ itself; legacy/ (the archived 2.x
examples and tests) and validation/ (the hardware test bench) are not part of
the Arduino library either.
"""
import subprocess

subprocess.run(["git", "rm", "-r", "-q", "--ignore-unmatch", "legacy", "validation"], check=True)
