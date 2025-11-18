# RP2350_wavPlayer ハードウェア構成

> **注意**: このドキュメントはRP2350向けに記載されていますが、**Raspberry Pi Pico W (RP2040) でテスト・動作確認済み**です。

## 主要コンポーネント

| コンポーネント | 型番 | 説明 |
|------------|------|------|
| MCU | RP2350A または RP2040 | メインコントローラー<br>テスト済み: Pico W (RP2040) |
| アンプ | MAX98357A | I2S入力対応 Class Dアンプ |
| ストレージ | microSDカード | WAVファイル保存用 |

## ピン配置

### I2S接続（MAX98357A）

| ピン番号 | 機能 | 説明 |
|---------|------|------|
| GP10 | DIN | I2S Data（シリアルデータ） |
| GP11 | BCLK | Bit Clock（ビットクロック） |
| GP12 | LRC | LR Clock / Word Select（左右チャンネル選択） |
| VBUS (5V) | VIN | MAX98357A電源入力 |
| GND | GND | グランド |

**MAX98357Aピン接続:**
```
MAX98357A    →  Pico 2/Pico W
---------------------------------
VIN          →  VBUS (5V)
GND          →  GND
DIN          →  GP10
BCLK         →  GP11
LRC          →  GP12
SD           →  (未接続、内部プルダウン = Left channel優先)
GAIN         →  (未接続、デフォルトゲイン = 9dB)
```

### SDカード接続

| ピン番号 | 機能 | 説明 |
|---------|------|------|
| GP17 | CS | チップセレクト (SPI0 CS) |
| GP18 | SCK | シリアルクロック (SPI0 SCK) |
| GP19 | MOSI | マスター出力・スレーブ入力 (SPI0 TX) |
| GP16 | MISO | マスター入力・スレーブ出力 (SPI0 RX) |

**注意**:
- SPI0を使用（RP2350/RP2040の標準SPI0ピン配置）
- GP16-19は連続したピン番号で配線しやすい
- I2Sピン（GP10-12）と競合しない

**SDカードモジュール接続:**
```
SD Card Module    →  Pico 2/Pico W
---------------------------------
VCC (3.3V)        →  3V3(OUT)
GND               →  GND
CS                →  GP17
SCK               →  GP18
MOSI              →  GP19
MISO              →  GP16
```

## 対応フォーマット

### WAVファイル仕様
- フォーマット: PCM (非圧縮)
- サンプリングレート: 8kHz〜48kHz（推奨: 44.1kHz）
- ビット深度: 8bit, 16bit
- チャンネル: モノラル、ステレオ

### ファイル命名規則
- ファイル名は英数字推奨
- 拡張子: `.wav` または `.WAV`
- ファイルはアルファベット順に再生されます

## 電源要件

| 項目 | 仕様 |
|------|------|
| MCU動作電圧 | RP2350/RP2040: 1.8V〜5.5V (USBから給電時は5V) |
| MAX98357A電源 | 2.5V〜5.5V (推奨5V) |
| 消費電流 | MCU: 〜100mA, MAX98357A: 最大3W出力時〜500mA |
| 推奨電源 | USB 5V 1A以上 |

## 回路図メモ

### I2S信号レベル
- RP2350/RP2040のI2S出力は3.3Vロジック
- MAX98357Aは2.5V〜5.5Vのロジック入力に対応（3.3V互換）

### オーディオ出力
- MAX98357Aはスピーカー直接駆動可能
- 推奨スピーカー: 4Ω or 8Ω, 3W
- フィルターレスClass D出力

## 書き込み方法

### Pico 2 (RP2350) の場合

推奨設定:
- Board: "Raspberry Pi Pico 2"
- Flash Size: "4MB (Sketch: 3.5MB, FS: 512KB)"
- CPU Speed: "150 MHz"
- Optimize: "Small (-Os) (standard)"
- RTTI: "Disabled"
- Stack Protector: "Disabled"
- C++ Exceptions: "Disabled"
- Debug Port: "Disabled"
- Debug Level: "None"
- USB Stack: "Pico SDK"
- IP/Bluetooth Stack: "IPv4 Only"

### Pico W (RP2040) の場合

推奨設定:
- Board: "Raspberry Pi Pico W"
- Flash Size: "2MB (Sketch: 1.5MB, FS: 512KB)"
- CPU Speed: "133 MHz"
- Optimize: "Small (-Os) (standard)"
- RTTI: "Disabled"
- Stack Protector: "Disabled"
- C++ Exceptions: "Disabled"
- Debug Port: "Disabled"
- Debug Level: "None"
- USB Stack: "Pico SDK"

## トラブルシューティング

### SDカードが認識されない
- カードが正しく挿入されているか確認
- ファイルシステムがFAT32であることを確認
- SPI配線を確認（特にMISOとMOSI）

### 音が出ない
- MAX98357AのVIN電源が5Vで供給されているか確認
- I2S配線（DIN, BCLK, LRC）を確認
- スピーカーが正しく接続されているか確認
- WAVファイルが対応フォーマットか確認（PCM形式のみ対応）

### ノイズが出る
- グランド配線を確認
- 電源ラインにデカップリングコンデンサを追加（10μF + 0.1μF）
- I2S信号線をできるだけ短く配線

### 再生が遅い・途切れる
- SDカードのClass 10以上を使用
- サンプリングレートが高すぎる場合は44.1kHz以下に
- ファイルサイズが大きすぎないか確認
