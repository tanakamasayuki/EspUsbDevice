# v3の独立したS3–S3試験

このディレクトリだけをv3開発の試験入口とする。`tests/`と`examples/`はv2の参照資産で、
読み込み・収集・helperのimport・sketchの流用をしない。両者との整合性を新APIの制約にしない。
前回作成した独自matrix/結果gateは撤去した。ここにはconftestも独自pytest pluginもない。
`--noconftest`によりconftest読込みを無効にしている。

## 実行

S3 HostとS3 DeviceのOTGを結線し、それぞれ独立した書き込み・ログportを使う。
`.env.example`を`.env`へコピーして実際のportを設定する。P4/S2/S31は現在の対象に含めない。

```sh
cd /home/mt/dev/EspUsbDevice-v3/validation
uv sync --locked
uv run --env-file .env pytest --collect-only -q
uv run --env-file .env pytest -q -rs --junitxml=.pytest-results/s3-vendor.xml
```

profileはHostとDeviceに一つずつ。複数profileを除外するためのhookは不要。
将来`all`を使う場合もplugin標準の候補選択から始め、結線を増やした時点で必要性を判断する。
実機pytestは一つずつ実行する。build確認だけなら`--run-mode=build`を使う。

`s3/vendor_roundtrip`は新規sketchと普通のtest関数一本で、1/63/64/65/127/128/129 byteの
Vendor往復を検証する。両側のpayloadと受信長を確認し、終了時は両boardを停止する。
USB開始・停止はtest本文に見える通常のsketchコマンドで、fixture差替えや自動hookではない。
現在は製品コードがv2なので短い送信に`flush()`を使う。この一行を不要にする変更の
利点・不利益から検討する。build_opt.hとTinyUSB内部APIは使わない。

## plugin側で解決する候補

以下は必要性が出た時の追加依頼候補で、依頼や実装はまだ行っていない。
conftestを別helperや別pluginへ隠して持ち込まない。

- 指定した実機試験が全件skipまたは未実行の場合に失敗終了する標準option。
  現在はpytest終了値0だけを合格にせず、今回の結果が`1 passed`であることを確認する。
- test本文で明示したUSB停止の失敗後、後続の実機caseを進めない標準的なcleanup契約。
  現在は一本だけなので後続caseはない。複数caseへ増やす前に検討する。
- 必要になればserialログの監査とartifact保存の標準機能。nodeid依存の許可リストは先に作らない。

pluginのprofile/port解決、device lock、dut/peers、compile/upload、JUnitをそのまま使う。
機材不足のskipは未検証であり、通信不一致・接続失敗をskipへ変えない。
API判断は[変更方針](../docs/V3_API_DECISIONS.ja.md)、次の作業は[引き渡し](../docs/V3_HANDOFF.ja.md)を参照。
