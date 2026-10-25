import re
from dataclasses import dataclass
from fnmatch import fnmatch
from pathlib import Path

import pytest

from usb_test_matrix import COMBINATION_KEY, LEDGER_KEY, REQUIREMENTS, RequiredRuns


_SERIAL_ERROR_PATTERNS = (
    re.compile(r"(?:^|\s)E \(\d+\)"),
    re.compile(r"ESP_ERR_(?!OK\b)"),
    re.compile(r"ESP_ERROR_CHECK failed", re.IGNORECASE),
    re.compile(r"Guru Meditation"),
    re.compile(r"assert failed", re.IGNORECASE),
    re.compile(r"abort\(\)"),
    re.compile(r"Backtrace:"),
    re.compile(r"(?:task |interrupt )?watchdog", re.IGNORECASE),
    re.compile(r"rst:.*\([^)]*WDT[^)]*\)", re.IGNORECASE),
    re.compile(r"Stack canary watchpoint triggered", re.IGNORECASE),
    re.compile(r"CORRUPT HEAP", re.IGNORECASE),
    re.compile(r"Brownout detector was triggered", re.IGNORECASE),
)


@dataclass(frozen=True)
class _KnownSerialFinding:
    nodeid_pattern: str
    log_name: str
    line_pattern: re.Pattern[str]
    max_count: int
    reason: str
    shutdown_only: bool = False


# These are keyed on pytest node ids, so renaming or merging a test silently
# detaches its entry: the test still passes and the line it allowed comes back
# as an unexpected finding. Grep this tuple when you rename a test.
_KNOWN_SERIAL_FINDINGS = (
    _KnownSerialFinding(
        nodeid_pattern="*loopback/*",
        log_name="dut.log",
        line_pattern=re.compile(r"USB HOST: Enqueue URB error: ESP_ERR_INVALID_STATE$"),
        max_count=1,
        reason="an in-flight host transfer races the deliberate loopback unplug during completed shutdown",
        shutdown_only=True,
    ),
    _KnownSerialFinding(
        nodeid_pattern="*loopback/usb_msc/test_loopback_usb_msc.py::test_loopback_usb_msc",
        log_name="dut.log",
        line_pattern=re.compile(r"USBH: Dev \d+ EP 0 STALL$"),
        max_count=1,
        reason="GET_MAX_LUN fallback for single-LUN MSC",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/usb_msc/test_usb_msc.py::test_usb_msc",
        log_name="dut.log",
        line_pattern=re.compile(r"USBH: Dev \d+ EP 0 STALL$"),
        max_count=1,
        reason="GET_MAX_LUN fallback for single-LUN MSC",
    ),
    # The DFU test refuses two images on purpose - one with a bad magic byte and
    # one that fails verification - and those refusals are what it asserts. Both
    # are logged by esp_ota_ops at error level on the peer, so they arrive here
    # looking like failures. Remove these two entries and the test still passes;
    # the audit is what would start complaining.
    # Same two refusals from the MSC firmware disk, which drives the same writer
    # through its block callbacks. Here they land on the DUT rather than a peer.
    _KnownSerialFinding(
        nodeid_pattern="*single/msc_firmware_disk/test_msc_firmware_disk.py::test_msc_firmware_disk",
        log_name="dut.log",
        line_pattern=re.compile(
            r"(?:boot_comm: mismatch chip ID|esp_ota_ops: New image failed verification)"
        ),
        max_count=4,
        reason="the test image is nonsense on purpose; failing verification is the assertion",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/usb_dfu/test_usb_dfu.py::test_usb_dfu",
        log_name="peer-device.log",
        line_pattern=re.compile(
            r"esp_ota_ops: OTA image has invalid magic byte"
        ),
        max_count=2,
        reason="the bad-magic block the test sends is supposed to be refused",
    ),
    _KnownSerialFinding(
        nodeid_pattern="*peer/usb_dfu/test_usb_dfu.py::test_usb_dfu",
        log_name="peer-device.log",
        line_pattern=re.compile(
            r"(?:boot_comm: mismatch chip ID|esp_ota_ops: New image failed verification)"
        ),
        max_count=4,
        reason="the test image is nonsense on purpose; failing verification is the assertion",
    ),
)
_ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
_AUDIT_RESULTS_KEY = pytest.StashKey[
    list[tuple[str, str, list[str], list[tuple[str, str]]]]
]()
_AUDIT_SECTION_KEY = pytest.StashKey[str]()
_AUDIT_LOG_COUNT_KEY = pytest.StashKey[int]()
_AUDIT_ROOTS_KEY = pytest.StashKey[set[str]]()


def _serial_error_lines(
    nodeid: str, log_path: Path
) -> tuple[list[str], list[tuple[str, str]]]:
    unexpected = []
    known = []
    known_counts = [0] * len(_KNOWN_SERIAL_FINDINGS)
    text = log_path.read_text(encoding="utf-8", errors="replace")
    lines = [_ANSI_ESCAPE_RE.sub("", line) for line in text.splitlines()]
    completed_shutdown_lines = set()
    shutdown_start = None
    for line_number, line in enumerate(lines, start=1):
        if line == "TEST_STOPPING":
            shutdown_start = line_number
        elif line == "TEST_STOPPED" and shutdown_start is not None:
            completed_shutdown_lines.update(range(shutdown_start + 1, line_number))
            shutdown_start = None

    for line_number, line in enumerate(lines, start=1):
        if not any(pattern.search(line) for pattern in _SERIAL_ERROR_PATTERNS):
            continue

        finding = f"{log_path.name}:{line_number}: {line}"
        matched_rule = None
        for index, rule in enumerate(_KNOWN_SERIAL_FINDINGS):
            if (
                known_counts[index] < rule.max_count
                and fnmatch(nodeid.split("[", 1)[0], rule.nodeid_pattern)
                and log_path.name == rule.log_name
                and rule.line_pattern.search(line)
                and (not rule.shutdown_only or line_number in completed_shutdown_lines)
            ):
                known_counts[index] += 1
                matched_rule = rule
                break

        if matched_rule:
            known.append((finding, matched_rule.reason))
        else:
            unexpected.append(finding)

    return unexpected, known


@pytest.fixture(autouse=True)
def serial_log_audit(request, test_case_tempdir):
    """Collect suspicious DUT and peer serial output without failing the test."""
    yield

    # Do not request dut/peers: their serial listeners must finish writing before
    # this fixture reads the logs. Unit tests simply have no *.log files.
    log_dir = Path(test_case_tempdir)
    log_paths = sorted(log_dir.glob("*.log"))
    request.config.stash[_AUDIT_LOG_COUNT_KEY] += len(log_paths)
    if log_paths:
        request.config.stash[_AUDIT_ROOTS_KEY].add(str(log_dir.parent))

    unexpected = []
    known = []
    for log_path in log_paths:
        log_unexpected, log_known = _serial_error_lines(request.node.nodeid, log_path)
        unexpected.extend(log_unexpected)
        known.extend(log_known)

    if not unexpected and not known:
        return

    section_lines = [f"Log directory: {log_dir}"]
    if unexpected:
        section_lines.append("Unexpected suspicious lines:")
        section_lines.extend(unexpected)
    if known:
        section_lines.append("Known allowed lines:")
        section_lines.extend(
            f"{finding} [reason: {reason}]" for finding, reason in known
        )
    section = "\n".join(section_lines)
    request.node.stash[_AUDIT_SECTION_KEY] = section
    request.config.stash[_AUDIT_RESULTS_KEY].append(
        (request.node.nodeid, str(log_dir), unexpected, known)
    )


def pytest_addoption(parser):
    group = parser.getgroup("USB bench")
    group.addoption("--usb-local-host", action="store_true", help="Include local Host profiles")
    group.addoption("--usb-require", action="append", default=[], choices=sorted(REQUIREMENTS),
                    help="Fail unless this suite passes on its required wiring in this run")


def pytest_configure(config):
    config.stash[_AUDIT_RESULTS_KEY] = []
    config.stash[_AUDIT_LOG_COUNT_KEY] = 0
    config.stash[_AUDIT_ROOTS_KEY] = set()
    suites = config.getoption("usb_require")
    if suites and config.getoption("run_mode") == "build":
        raise pytest.UsageError("--usb-require needs hardware execution, not --run-mode=build")
    config.stash[LEDGER_KEY] = RequiredRuns(suites)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    pair = item.stash.get(COMBINATION_KEY, None)
    if pair is not None:
        item.config.stash[LEDGER_KEY].record(pair, item.nodeid, report.when, report.outcome)
    if report.when != "teardown":
        return

    if report.failed and pair is not None:
        item.session.shouldstop = "USB teardown failed; bench state is not confirmed safe for the next case"

    section = item.stash.get(_AUDIT_SECTION_KEY, "")
    if section:
        # pytest-html appends teardown sections to the test's expandable log.
        report.sections.append(("serial error audit teardown", section))


def pytest_sessionfinish(session, exitstatus):
    config = session.config
    if config.getoption("collectonly"):
        return
    ledger = config.stash[LEDGER_KEY]
    unexpected = any(row[2] for row in config.stash[_AUDIT_RESULTS_KEY])
    if ledger.suites and (ledger.missing() or unexpected) and session.exitstatus == 0:
        session.exitstatus = pytest.ExitCode.TESTS_FAILED


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    ledger = config.stash[LEDGER_KEY]
    if ledger.suites and not config.getoption("collectonly"):
        terminalreporter.section("required USB runs", sep="=")
        missing = ledger.missing()
        for entry in missing:
            terminalreporter.write_line(f"NOT PASSED: {entry}", red=True)
        if not missing:
            terminalreporter.write_line("All required cases completed setup, call, and teardown.")
    results = config.stash[_AUDIT_RESULTS_KEY]
    log_count = config.stash[_AUDIT_LOG_COUNT_KEY]
    log_roots = sorted(config.stash[_AUDIT_ROOTS_KEY])
    terminalreporter.section("serial log audit", sep="=")
    for log_root in log_roots:
        terminalreporter.write_line(f"Log root: {log_root}")
    if not results:
        terminalreporter.write_line(
            f"No suspicious serial output found in {log_count} DUT/peer log(s)."
        )
        return

    unexpected_count = sum(len(unexpected) for _, _, unexpected, _ in results)
    unexpected_test_count = sum(bool(unexpected) for _, _, unexpected, _ in results)
    known_count = sum(len(known) for _, _, _, known in results)
    known_test_count = sum(bool(known) for _, _, _, known in results)
    if unexpected_count:
        terminalreporter.write_line(
            f"Found {unexpected_count} unexpected suspicious line(s) in "
            f"{unexpected_test_count} test(s); --usb-require treats these as a run failure."
        )
    else:
        terminalreporter.write_line(
            f"No unexpected suspicious serial output found in {log_count} DUT/peer log(s)."
        )
    if known_count:
        terminalreporter.write_line(
            f"Found {known_count} known allowed line(s) in {known_test_count} test(s)."
        )

    for nodeid, log_dir, unexpected, known in results:
        terminalreporter.write_line(nodeid)
        terminalreporter.write_line(f"  Log directory: {log_dir}")
        for finding in unexpected:
            terminalreporter.write_line(f"  {finding}")
        for finding, reason in known:
            terminalreporter.write_line(f"  KNOWN: {finding}")
            terminalreporter.write_line(f"    Reason: {reason}")
