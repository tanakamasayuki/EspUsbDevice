"""Bench wiring and fresh-run requirements; no port access during collection."""

from dataclasses import dataclass, field

import pytest


# Profiles identify roles, not interchangeable physical boards. Add S2/S31/P4
# peer rows only after their sketches and fixed wiring have been brought up.
PEER_WIRING = {
    ("s3_peer_host", "s3_peer_device"),
    ("s3_peer_local", "s3_peer_local"),
}
COMBINATION_KEY = pytest.StashKey[tuple[str | None, str | None]]()
LEDGER_KEY = pytest.StashKey["RequiredRuns"]()


def applicability(group, primary, device=None, *, build=False, local=False):
    if not local and any(p and p.endswith("_local") for p in (primary, device)):
        return "local Host profiles require --usb-local-host"
    if build:
        return None
    if group == "peer" and (primary, device) not in PEER_WIRING:
        return "no recorded USB wiring for this Host/Device profile pair"
    if group == "loopback" and primary not in ("p4_loopback", "p4_loopback_local"):
        return "loopback wiring is recorded only for P4"
    return None


def check_item(item, combination, group):
    if combination is None:
        return None
    pair = (combination.primary, combination.peers.get("device"))
    item.stash[COMBINATION_KEY] = pair
    return applicability(
        group, *pair,
        build=item.config.getoption("run_mode") == "build",
        local=item.config.getoption("usb_local_host"),
    )


REQUIREMENTS = {
    "s3-smoke": (
        (("s3_peer_host", "s3_peer_device"), "peer/hid_keyboard/test_hid_keyboard.py::test_hid_keyboard"),
        (("s3_peer_host", "s3_peer_device"), "peer/usb_serial/test_usb_serial.py::test_usb_serial"),
        (("s3_peer_host", "s3_peer_device"), "peer/composite_hid_cdc/test_composite_hid_cdc.py::test_composite_hid_cdc"),
    ),
    "p4-smoke": (
        (("p4_loopback", None), "loopback/usb_serial/test_loopback_usb_serial.py::test_loopback_usb_serial"),
    ),
}


@dataclass
class RequiredRuns:
    suites: list[str]
    # Key includes the full parameterized item: phases from different items
    # must never combine into one apparent success.
    phases: dict = field(default_factory=dict)

    def record(self, pair, nodeid, phase, outcome):
        self.phases.setdefault((pair, nodeid), {})[phase] = outcome

    def missing(self):
        passed = {
            (pair, nodeid.split("[", 1)[0])
            for (pair, nodeid), phases in self.phases.items()
            if all(phases.get(p) == "passed" for p in ("setup", "call", "teardown"))
        }
        return [
            f"{suite}: {nodeid} {pair}"
            for suite in self.suites for pair, nodeid in REQUIREMENTS[suite]
            if (pair, nodeid) not in passed
        ]
