# Loopback テスト

> English: [README.md](README.md)

`tests/loopback` には、ESP32-P4 1台で EspUsbHost と EspUsbDevice を同時に動かす
テストを置きます。

最初の対象は HID keyboard loopback です。より広い class coverage に進む前に、
descriptor ログで P4 の port / speed 挙動を確認します。

## Matrix

| Device | Host | 期待 |
|--------|------|------|
| FS device | FS host | SDK が許すなら最重要ターゲット。 |
| HS device | HS host | P4 high-speed 経路の baseline。 |
| FS device | HS host | probe / diagnostic 用。 |
| HS device | FS host | 失敗または未対応想定。明示的に記録する。 |
