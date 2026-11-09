# single テスト

> English: [README.md](README.md)

board 1 枚だけで確かめる試験です。USB の相手は使いません。

| probe | combo | 目的 |
| --- | --- | --- |
| `psram` | `p4_psram`、`p4_p4x_hs`、`p4x_p4_hs`、`p4x_pc` | bench の P4 系 board の PSRAM 容量を、その board 自身で読み、全域を書いて読み戻す。結果は `.pytest-results/psram-*.json` |

- HS 直結の combo では primary の board だけを試験します。`peer_device/` は combo を満たすために同じ測定を build したもので、測定は primary でだけ行います。
- 試験の前提にしている容量（P4 系はすべて 32 MiB）は [TEST_PLAN.ja.md の PSRAM](../TEST_PLAN.ja.md#psram) にあります。

```sh
uv run --locked --env-file .env pytest single/psram --combo p4_psram -q -rs
```
