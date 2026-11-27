# SpectrumDongle ZMK Module

ZMKドングルから外部表示コントローラへ、現在のレイヤーと最大5台の
split peripheralの電池残量をUARTで送信するZMKモジュールです。

設計とZMKイベントの利用方法は
[prospector-zmk-module](https://github.com/carrefinho/prospector-zmk-module/tree/feat/new-status-screens)
を参考にしています。表示処理は含まず、UART送信だけを担当します。

## ハードウェア

- Board: Seeed Studio XIAO nRF52840 (`xiao_ble//zmk`)
- TX: D6 / P1.11
- RX: 未使用
- UART: 115200bps, 8 data bits, no parity, 1 stop bit
- 信号レベル: 3.3V

XIAO nRF52840のGNDと受信側のGNDを必ず共通にしてください。

## ZMKへの追加

キーボード設定リポジトリの `config/west.yml` にremoteとprojectを追加します。

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: kijitora
      url-base: https://github.com/Kijitora114514
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: SpectrumDongle
      remote: kijitora
      revision: main
  self:
    path: config
```

ドングルのbuild entryへ `SpectrumDongle` shieldを追加します。

```yaml
include:
  - board: xiao_ble//zmk
    shield: your_keyboard_dongle SpectrumDongle
```

レイヤー名はkeymapの `display-name` を使用します。空の場合はレイヤー番号を
送信します。

```dts
keymap {
    compatible = "zmk,keymap";

    base_layer {
        display-name = "Base";
        bindings = <
            /* ... */
        >;
    };
};
```

shieldは次の設定を既定で有効にします。

```ini
CONFIG_ZMK_SPLIT=y
CONFIG_ZMK_SPLIT_ROLE_CENTRAL=y
CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=5
CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y
CONFIG_SPECTRUM_DONGLE_UART=y
```

## 送信タイミングとフォーマット

- レイヤー変更時
- peripheralの電池残量更新時
- 起動後500ms
- 以後1秒ごとのフルスナップショット

受信プロトコルの詳細は [docs/protocol.md](docs/protocol.md) を参照してください。

## TXピンの変更

既定値はD6/P1.11です。モジュール本体を変更せずに差し替える場合は、
[docs/pin-override.overlay.example](docs/pin-override.overlay.example)をキーボード設定側へ
コピーし、port/pinと `EXTRA_DTC_OVERLAY_FILE` を指定してください。

P0.08へ変更する場合の値は `NRF_PSEL(UART_TX, 0, 8)` です。

## 設定項目

```ini
# 定期フルスナップショットの周期
CONFIG_SPECTRUM_DONGLE_UART_HEARTBEAT_MS=1000

# 起動後の初回送信待ち時間
CONFIG_SPECTRUM_DONGLE_UART_START_DELAY_MS=500

# レイヤー名として送信する最大バイト数
CONFIG_SPECTRUM_DONGLE_UART_LAYER_NAME_MAX_LEN=32
```

UART送信にはZephyrの非同期APIを使用します。ZMKイベントリスナー内では
最新状態の保存とworkの予約だけを行い、UART完了待ちは行いません。

