# Unit テスト

> English: [README.md](README.md)

ボードもシリアルも Arduino CLI も使いません。ここのモジュールはすべて、純粋な Python か、
`src/` から出荷される C++ を抽出してシステムの g++ でコンパイルするかのどちらかです。
層全体で開発マシン上 5 秒ほどで終わるので、CI が push ごとに `tests/.env` なしで
回しています。`.github/workflows/unit-tests.yml` を見てください。

実機上で本物の API を呼ぶテストは `../single/` にあります。見た目は unit テストで、
TinyUSB を起動しないものもありますが、ボードが必要です。ここに置いていたために
`unit/` が CI で回せず、その事実がアップロードエラーの裏に隠れていました。

## `ccid_descriptor`

CCID の interface / class descriptor を host g++ で検証します。descriptor 生成は
Arduino / TinyUSB に依存しない純粋な byte 組み立てなので、`keymap` と同様に
`src/EspUsbDeviceCcid.cpp` から実行時に抜き出して host でコンパイルします（出荷コード
そのものを検証します）。CCID class descriptor の全フィールド——特に Host が TPDU と
APDU のどちらを送るか決める exchange level——と、単体構成・複合構成のいずれでも
interrupt endpoint が bulk pair の 1 つ上に来ることを確認します。

## `descriptor_model`

v2のdescriptor基盤をhost g++だけで検証します。Arduino/TinyUSB headerには依存せず、
buffer境界、interface/string採番、direction別endpoint採番、duplex endpoint、重複・上限検出、
FS/HS configuration descriptorのMPS切替、other-speed configuration、device qualifier、
HID function writerを確認します。Arduino sketch全体が一時的にcompile不能でも単独実行できます。

## `tinyusb_config`

ライブラリ所有のTinyUSB設定をS2/S3/P4の各target macroでhost compileし、Arduino Coreの
Kconfigに依存せず全device classが有効になること、S2/S3はFS、P4はFS/HS対応能力として
compileされること、Audioのcompile-time上限を確認します。controller/root-hub portと
実際のbus speedはこの設定では固定せず、runtime初期化で選択します。

## `tinyusb_vendor`

`src/` に取り込んだ TinyUSB の pin metadata、header、選択した device source が、
`third_party/tinyusb/UPSTREAM.json` が指す upstream commit と byte-identical であること、
および意図しない `.c` が build 対象へ増えていないことを確認します。キャッシュが無ければ
upstream の tarball を取得します。この層でネットワークに触るのはここだけです。

## `audio_model`

旧Audio実装に依存しないv2のPCM format/bandwidth modelをhost上で検証します。
mono/stereo、16/24/32 bit、subslot、FS/HS frame rate、clock tolerance、
isochronous packet上限、software buffer上限、entity graph、UAC2 descriptor、
Clock/Feature control stateとCUR/RANGE wire formatを確認します。

## `keymap`

board 不要の純粋な host g++ テストです。実行時に layout enum、
`ESP_USB_DEVICE_MOD_*` 定数、keymap include 群、そして純粋な逆引き関数
`espUsbDeviceAsciiToUsage` を実ソース `src/EspUsbDevice.{h,cpp}` からそのまま抽出し、
`keymap_test.cpp` と一緒にコンパイルして、文字 -> HID usage + modifier の
round-trip を検証します。base / Shift 段、AltGr（Right Alt）フォールバック
（de_DE の `@`、`{ [ ] }` 等）、および pt_BR の 0x90 tableSize 修正
（International1 の `/` `?` = usage 0x87）を確認します。`src/keymap/*.h` の
テーブルは EspUsbHost と byte-identical で、順方向は EspUsbHost 側の keymap テストが
カバーしています。

## `nkro_report`

board 不要の host g++ テストです。NKRO の保持キー状態 `EspUsbDeviceNkroKeyboardReport`
（header-only）を実ソース `src/EspUsbDevice.h` から実行時に抽出してコンパイルし、
bitmap レイアウト（bit `usage & 7` / byte `usage >> 3`)、modifier usage `0xE0`-`0xE7` の
`modifiers` への振り分け、`MaxBitmapUsage`（`0xDF`）境界と `0xE8` 以上の拒否、
10キー同時押下、`clear()`、コピー意味論を確認します。struct が Arduino / TinyUSB へ
依存し始めた場合はテスト側の抽出が成立しなくなるため、抽出時に検出して失敗させます。
`keymap` と同じ方式で、テストとライブラリが乖離しないようにしています。
boot protocol への畳み込み、`enableNkro()` 未実行時の失敗、実際に Host へ届くバイト列は
host コンパイルできない `EspUsbDeviceHidKeyboard` 側の挙動なので、実機の
`tests/peer/hid_keyboard_nkro` でカバーします。

## `midi_descriptor`

複数 cable の USB MIDI configuration descriptor をホスト g++ で検証します。builder は
TinyUSB の 1 cable 用テンプレート `TUD_MIDI_DESCRIPTOR()` を使わず、head と cable ごとの
jack descriptor と endpoint ブロックを自前で組み立てています。この組み立ては黙って壊れます。
`wTotalLength` が違っても jack ID が重複しても、ホストは列挙してポート数が違って見えるだけなので、
実機の往復テストは descriptor が壊れたまま通ります。builder はテスト時に
`src/EspUsbDevice.cpp` から抽出し、本物の TinyUSB マクロと enum と一緒にコンパイルするので、
検証対象は出荷されるコードそのものです。

## `dependency_boundary`

依存してはいけない Arduino-ESP32 の USB core ヘッダとシンボルが出荷ソースに現れていないかを
検査します。一度決めた境界で、放っておくと事故で越えます。`USB.h` を include しても
コンパイルは通ってしまい、実行時の衝突としてしか現れないからです。

## `known_findings`

`tests/conftest.py` の serial log 許可リストが、実在するテストに一致しているかを検査します。
ルールは pytest の node id で引いているだけで、名指ししたテストとの結び付きが何もないため、
リネームやマージで黙って外れます。テストは通ったままで、許可していたはずの行が
「未知の異常」として再出現します。`peer/` を 110 テストから 29 に統合したときに実際に起きました。
