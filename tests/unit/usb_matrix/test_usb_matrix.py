"""Check that an all-skip run or wrong wiring cannot satisfy a hardware gate."""

import pytest

from usb_test_matrix import REQUIREMENTS, RequiredRuns, applicability


@pytest.mark.parametrize("pair,allowed", [
    (("s3_peer_host", "s3_peer_device"), True),
    (("s3_peer_host", "s3_peer_local"), False),
    (("s3_peer_local", "s3_peer_device"), False),
    (("s3_peer_local", "s3_peer_local"), False),
    (("new_host", "new_device"), False),
])
def test_only_recorded_default_pair_runs(pair, allowed):
    assert (applicability("peer", *pair) is None) == allowed


def test_build_ignores_wires_but_local_host_is_opt_in():
    assert applicability("peer", "new_host", "new_device", build=True) is None
    assert applicability("peer", "s3_peer_local", "s3_peer_local", build=True)
    assert applicability("peer", "s3_peer_local", "s3_peer_local", local=True) is None
    assert applicability("peer", "s3_peer_host", "s3_peer_local", local=True)


def populate(ledger, *, phase_outcomes=None, pair_override=None):
    for pair, nodeid in REQUIREMENTS["s3-smoke"]:
        for phase, outcome in (phase_outcomes or {
            "setup": "passed", "call": "passed", "teardown": "passed"
        }).items():
            ledger.record(pair_override or pair, nodeid + "[primary=s3_peer_host,device=s3_peer_device]", phase, outcome)


def test_required_cases_need_fresh_complete_run():
    ledger = RequiredRuns(["s3-smoke"])
    assert len(ledger.missing()) == 3
    populate(ledger)
    assert not ledger.missing()
    assert len(RequiredRuns(["s3-smoke"]).missing()) == 3


@pytest.mark.parametrize("phases", [
    {"setup": "skipped", "teardown": "passed"},
    {"setup": "passed", "call": "skipped", "teardown": "passed"},
    {"setup": "passed", "call": "passed"},
    {"setup": "passed", "call": "passed", "teardown": "failed"},
    {"setup": "passed", "call": "failed", "teardown": "passed"},
])
def test_skip_build_failure_and_unfinished_are_not_success(phases):
    ledger = RequiredRuns(["s3-smoke"])
    populate(ledger, phase_outcomes=phases)
    assert len(ledger.missing()) == 3


def test_local_host_cannot_substitute_for_released_host():
    ledger = RequiredRuns(["s3-smoke"])
    populate(ledger, pair_override=("s3_peer_local", "s3_peer_local"))
    assert len(ledger.missing()) == 3


def test_different_parameters_cannot_share_phases():
    ledger = RequiredRuns(["s3-smoke"])
    for pair, nodeid in REQUIREMENTS["s3-smoke"]:
        ledger.record(pair, nodeid + "[a]", "setup", "passed")
        ledger.record(pair, nodeid + "[a]", "call", "passed")
        ledger.record(pair, nodeid + "[b]", "teardown", "passed")
    assert len(ledger.missing()) == 3
