# Single-board tests

> 日本語: [README.ja.md](README.ja.md)

Tests that run on one board on its own, with no USB partner.

| Probe | Combo | Purpose |
| --- | --- | --- |
| `psram` | `p4_psram`, `p4_p4x_hs`, `p4x_p4_hs`, `p4x_pc` | Reads the PSRAM size of each P4 bench board on the board itself, then writes and reads back the whole area. Results go to `.pytest-results/psram-*.json` |

- In the HS combos only the primary board is tested. `peer_device/` is the same measurement built to satisfy the combo; only the primary measures.
- The size the tests rely on (32 MiB on every P4 board) is in [TEST_PLAN.md, PSRAM](../TEST_PLAN.md#psram).

```sh
uv run --locked --env-file .env pytest single/psram --combo p4_psram -q -rs
```
