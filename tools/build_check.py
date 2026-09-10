#!/usr/bin/env python3
"""Build every example that declares a given sketch.yaml profile.

A thin wrapper around `arduino-cli compile --profile <profile>` that iterates all
`examples/**/sketch.yaml`, compiles each example whose profiles include the
requested one, and prints a PASS/FAIL summary. Exits non-zero if any build fails,
so it is usable as a CI job and as a pre-release gate, and the same command works
locally when you want to check a change before pushing.

This replaced `tests/examples_compile/`, which ran the same compiles inside
pytest. Nothing about a build test needs a board, a serial port or a fixture, and
running it there meant a full test run spent its first quarter of an hour not
touching the hardware it was queued for. Here the profiles run as parallel CI
jobs instead of one sequential sweep.

Examples:
  python tools/build_check.py                  # default profile: esp32s3
  python tools/build_check.py esp32s2          # the RAM-constrained target
  python tools/build_check.py esp32p4 --only Audio
  python tools/build_check.py --list           # which examples declare which profiles

The profile's platform is installed by arduino-cli on first use, so the first run
for a new target downloads a toolchain.
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
EXAMPLES_ROOT = REPO_ROOT / "examples"


def profiles_in(sketch_yaml: pathlib.Path) -> list[str]:
    """Return the profile names declared under `profiles:` in a sketch.yaml."""
    names = []
    in_profiles = False
    for line in sketch_yaml.read_text().splitlines():
        if re.match(r"^profiles:\s*$", line):
            in_profiles = True
            continue
        if in_profiles:
            # A top-level key (no indentation) ends the profiles block.
            if line and not line[0].isspace():
                break
            m = re.match(r"^  ([A-Za-z0-9_.\-]+):\s*$", line)
            if m:
                names.append(m.group(1))
    return names


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Build examples for a sketch.yaml profile."
    )
    parser.add_argument(
        "profile", nargs="?", default="esp32s3", help="profile name (default: esp32s3)"
    )
    parser.add_argument(
        "--only",
        default="",
        help="only build examples whose path contains this substring",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="SUBSTR",
        help="skip examples whose path contains SUBSTR (repeatable)",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="list examples and their profiles, then exit",
    )
    args = parser.parse_args()

    sketches = sorted(EXAMPLES_ROOT.rglob("sketch.yaml"))
    if not sketches:
        print(f"No examples found under {EXAMPLES_ROOT}", file=sys.stderr)
        return 2

    if args.list:
        for sk in sketches:
            rel = sk.parent.relative_to(REPO_ROOT)
            print(f"{rel}: {', '.join(profiles_in(sk))}")
        return 0

    if not shutil.which("arduino-cli"):
        print("arduino-cli not found on PATH", file=sys.stderr)
        return 2

    targets = []
    skipped = []
    for sk in sketches:
        rel = sk.parent.relative_to(REPO_ROOT)
        if args.only and args.only not in str(rel):
            continue
        if any(ex in str(rel) for ex in args.exclude):
            skipped.append(rel)
            continue
        if args.profile in profiles_in(sk):
            targets.append(sk.parent)
        else:
            # An example that does not declare this profile is not applicable,
            # not a failure. Reporting it as one would make the matrix unreadable.
            skipped.append(rel)

    if not targets:
        print(f"No examples declare profile '{args.profile}'.", file=sys.stderr)
        return 2

    print(f"Building {len(targets)} example(s) with profile '{args.profile}'\n")
    results = []
    killed = []
    for sketch_dir in targets:
        rel = sketch_dir.relative_to(REPO_ROOT)
        print(f"==> {rel}")
        proc = subprocess.run(
            ["arduino-cli", "compile", "--profile", args.profile, str(sketch_dir)],
            capture_output=True,
            text=True,
        )
        if proc.returncode < 0:
            # A negative return code is a signal, not a compiler verdict:
            # something outside this run killed arduino-cli. It has happened on a
            # shared machine, where a neighbouring project's `pkill -x
            # arduino-cli` took two of these with it. Report it as its own
            # category so nobody goes looking for a compile error in a sketch
            # that compiles.
            print(f"    KILLED by signal {-proc.returncode} - not a build failure")
            killed.append(rel)
            results.append((rel, False))
            print()
            continue
        ok = proc.returncode == 0
        results.append((rel, ok))
        if ok:
            print("    PASS")
        else:
            print(f"    FAIL (exit {proc.returncode})")
            tail = (proc.stderr or proc.stdout).strip().splitlines()[-8:]
            for line in tail:
                print(f"    | {line}")
        print()

    passed = [r for r, ok in results if ok]
    failed = [r for r, ok in results if not ok]
    print("=" * 60)
    print(f"profile={args.profile}  passed={len(passed)}  failed={len(failed)}")
    if skipped:
        print(f"(skipped {len(skipped)} example(s) without a '{args.profile}' profile)")
    for r in failed:
        print(f"  {'KILLED' if r in killed else 'FAIL'} {r}")
    if killed:
        print(
            f"\n{len(killed)} build(s) were killed by a signal. Re-run before "
            "investigating: this is an environment failure, not a code failure."
        )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
