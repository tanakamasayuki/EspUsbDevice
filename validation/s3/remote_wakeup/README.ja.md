# Remote wakeup

Device の remote wakeup の要求を、Host が実際の wake として観測できることと、許可・解除・reset の境界を確かめます。
S3 の FS（combo `s3`）と、P4 / P4X の HS 直結（`p4_p4x_hs`、`p4x_p4_hs`）で動きます。

## 目的

- Host が SET_FEATURE(DEVICE_REMOTE_WAKEUP) で許可したときだけ、Device の要求で wake signal が出ること。
- 許可していないとき、CLEAR_FEATURE の後、bus reset の後は、要求を拒否して signal を出さないこと。
- wake の後に、payload が往復すること。
- `end()` と並行した要求や照会を、停止の開始から拒否すること。

## 手順

- Host が bus を suspend し、Device の要求で実際の wake status を検出してから resume します。
- 使っている ESP-IDF の HCD は remote wake の event に対応していません。
  そこで probe の中で、転送していない間だけ USB IRQ を保留し、clock の gate / ungate を含めて root port を復帰させ、保留中の wake を clear してから元の IRQ の設定に戻します。
- SDK や pytest への patch、conftest は使いません。
- reset の保留中の要求、重複した要求、`end()` との並行呼出しも試します。
  worker を保留したまま runtime の停止を始め、別の task からの照会と要求を拒否することを確かめてから worker を再開します。`end()` が戻った後の照会と要求も拒否されることを確かめます。
- Device 側の register の観測と event hook は診断用で、公開 API には含めません。

参考：[USB 2.0](https://www.usb.org/document-library/usb-20-specification) §7.1.7.7（Resume）、§9.4.1 / §9.4.9（Clear / Set Feature）
