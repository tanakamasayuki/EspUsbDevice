"""The serial-log allowlist must keep matching real tests.

`tests/conftest.py` carries a tuple of `_KnownSerialFinding` rules that stop
expected ESP-IDF error lines from being reported as unexpected. Each rule is
keyed on a pytest node id, and nothing connects that key to the test it names:
rename or merge a test and the rule silently stops matching. The test still
passes - it is only the audit that changes - so the first sign is a line that
was expected for years being reported as a new finding.

That happened here when the peer suite went from 110 tests to 29: the rule
naming `test_usb_msc_block_device_info` no longer matched anything, and the
GET_MAX_LUN STALL it covered came back as unexpected. This module is the check
that would have caught it, and it needs no hardware.

Node ids are read from the test files rather than from a pytest collection,
because the allowlist also covers `manual/`, which the default run does not
collect.
"""

import ast
from fnmatch import fnmatch
from pathlib import Path

TESTS_ROOT = Path(__file__).resolve().parents[2]
CONFTEST = TESTS_ROOT / "conftest.py"


def _known_findings():
    """The `_KNOWN_SERIAL_FINDINGS` rules, read without importing conftest.

    Parsed rather than imported so this test says something about the file the
    suite actually loads, and does not depend on import side effects.
    """
    tree = ast.parse(CONFTEST.read_text())
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(
            isinstance(t, ast.Name) and t.id == "_KNOWN_SERIAL_FINDINGS"
            for t in node.targets
        ):
            continue
        rules = []
        for element in node.value.elts:
            fields = {kw.arg: kw for kw in element.keywords}
            rules.append(
                {
                    "nodeid_pattern": ast.literal_eval(fields["nodeid_pattern"].value),
                    "log_name": ast.literal_eval(fields["log_name"].value),
                    # re.compile(r"...") - the pattern is the first argument.
                    "line_pattern": ast.literal_eval(fields["line_pattern"].value.args[0]),
                }
            )
        return rules
    raise AssertionError("_KNOWN_SERIAL_FINDINGS not found in tests/conftest.py")


def _node_ids():
    """Every `test_*` function in the tree, as `<path relative to tests/>::<name>`.

    That is the tail of a pytest node id, and the patterns all start with `*`,
    so fnmatch against it behaves the way conftest's own matching does.
    """
    ids = []
    for path in sorted(TESTS_ROOT.rglob("test_*.py")):
        if "build" in path.parts or "__pycache__" in path.parts:
            continue
        relative = path.relative_to(TESTS_ROOT).as_posix()
        for node in ast.parse(path.read_text()).body:
            if isinstance(node, ast.FunctionDef) and node.name.startswith("test_"):
                ids.append(f"{relative}::{node.name}")
    return ids


def test_every_allowlist_entry_matches_a_real_test():
    """A rule that matches nothing is a rule that has come unstuck from its test."""
    node_ids = _node_ids()
    assert node_ids, "no tests found; the node id scan is broken, not the allowlist"

    for rule in _known_findings():
        pattern = rule["nodeid_pattern"]
        matched = [n for n in node_ids if fnmatch(n, pattern)]
        assert matched, (
            f"allowlist entry {pattern!r} matches no test. If a test was renamed "
            f"or merged, repoint the entry in tests/conftest.py; if the case it "
            f"covered is gone, delete the entry."
        )


def test_specific_allowlist_entries_come_before_general_ones():
    """conftest stops at the first matching rule, so order is behaviour.

    A broad rule placed ahead of a narrow one absorbs the narrow one's line and
    spends its own budget doing it, which reports the wrong reason and can push
    a genuinely unexpected line past `max_count`.
    """
    rules = _known_findings()
    node_ids = _node_ids()

    for index, rule in enumerate(rules):
        covered = {n for n in node_ids if fnmatch(n, rule["nodeid_pattern"])}
        if not covered:
            # A rule that matches nothing is vacuously narrower than every other
            # rule, which would report an ordering problem for what is really a
            # detached entry. The test above already says so, precisely.
            continue
        for earlier in rules[:index]:
            earlier_covered = {
                n for n in node_ids if fnmatch(n, earlier["nodeid_pattern"])
            }
            if not covered <= earlier_covered or covered == earlier_covered:
                continue
            # The earlier rule is strictly broader. That only shadows this one
            # when it would also match the same lines in the same log.
            if earlier["log_name"] != rule["log_name"]:
                continue
            assert not _patterns_overlap(earlier["line_pattern"], rule["line_pattern"]), (
                f"allowlist entry {earlier['nodeid_pattern']!r} is broader than "
                f"{rule['nodeid_pattern']!r} and matches the same lines, but comes "
                f"first. conftest stops at the first match, so move the specific "
                f"entry above the general one."
            )


def _patterns_overlap(broad: str, narrow: str) -> bool:
    """True when the broader rule's line pattern would also match the narrow one's.

    A deliberate approximation: the two regex sources are compared as text,
    because there is no corpus of log lines to test them against here. It
    catches the case the allowlist actually has - the same pattern, or one
    contained in the other - and says nothing about regexes that overlap only
    in what they match. A missed pair shows up in the audit output instead,
    which is where it showed up before this test existed.
    """
    return broad in narrow or narrow in broad
