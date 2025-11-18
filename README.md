# RP2350_wavPlayer

RP2350マイコンを使用したSDカードWAVファイル再生プレイヤー。I2S出力でMAX98357Aアンプを駆動します。

## 特徴

- ✅ SDカードからWAVファイル自動再生
- ✅ I2S出力（MAX98357A対応）
- ✅ シリアルコマンドで制御可能
- ✅ 複数フォーマット対応（8/16bit, mono/stereo）
- ✅ ファイル名順の自動再生

## 必要なもの

### ハードウェア
- Raspberry Pi Pico 2 (RP2350A)
- MAX98357A I2S Class Dアンプモジュール
- microSDカードモジュール
- microSDカード (Class 10推奨、FAT32フォーマット)
- スピーカー (4Ω or 8Ω, 3W)
- USBケーブル（給電・書き込み用）

### ソフトウェア
- Arduino IDE 2.x
- arduino-pico ボードライブラリ
- ofxSerialManager ライブラリ（`../../libraries/ofxSerialManager`）

## 配線

### I2S - MAX98357A

```
RP2350 (Pico 2)  →  MAX98357A
----------------------------------
GP10             →  DIN
GP11             →  BCLK
GP12             →  LRC
VBUS (5V)        →  VIN
GND              →  GND
                    SD → (未接続)
                    GAIN → (未接続)
```

### SPI - SDカード

```
RP2350 (Pico 2)  →  SDカードモジュール
----------------------------------
GP17             →  CS
GP18             →  SCK
GP19             →  MOSI
GP16             →  MISO
3V3              →  VCC
GND              →  GND
```

詳細は [HARDWARE.md](HARDWARE.md) を参照してください。

## セットアップ

### 1. Arduino IDEの準備

1. Arduino IDE 2.xをインストール
2. ボードマネージャーから「Raspberry Pi Pico/RP2040/RP2350」をインストール
3. ボード設定:
   - Board: "Raspberry Pi Pico 2"
   - USB Stack: "Pico SDK"

### 2. ライブラリのインストール

ofxSerialManagerライブラリが`../../libraries/ofxSerialManager`に配置されていることを確認してください。

### 3. SDカードの準備

1. microSDカードをFAT32でフォーマット
2. WAVファイルをルートディレクトリにコピー
   - 対応フォーマット: PCM（非圧縮）
   - サンプリングレート: 8kHz〜48kHz（推奨: 44.1kHz）
   - ビット深度: 8bit, 16bit
   - チャンネル: モノラル、ステレオ

### 4. 書き込み

1. RP2350をUSBで接続
2. Arduino IDEで`RP2350_wavPlayer.ino`を開く
3. ボードとポートを選択
4. 「アップロード」をクリック

## 使い方

### 基本動作

1. 電源投入後、自動的にSDカード内のWAVファイルをスキャン
2. ファイル一覧がシリアルモニタに表示される
3. 名前順に全ファイルを自動再生
4. 再生完了後、コマンド待機状態に

### シリアルコマンド

シリアルモニタ（115200 baud）でコマンド入力:

```
list:              # ファイル一覧を表示
play:              # 次のファイルを再生
play:/song.wav     # 指定ファイルを再生
playall:           # 全ファイルを順番に再生
stop:              # 再生を停止
scan:              # SDカードを再スキャン
help:              # ヘルプを表示
```

**注意**: コマンドは `<command>:<payload>` の形式で、末尾に改行が必要です。

### シリアルモニタの設定

- ボーレート: 115200
- 改行コード: LF (Newline)

## 対応フォーマット

### 対応
- ✅ WAV (PCM, 非圧縮)
- ✅ 8bit / 16bit
- ✅ モノラル / ステレオ
- ✅ 8kHz〜48kHz

### 非対応
- ❌ MP3, AAC等の圧縮フォーマット
- ❌ 24bit / 32bit PCM
- ❌ 48kHz超のサンプリングレート

## トラブルシューティング

### SDカードが認識されない
- カードが正しく挿入されているか確認
- FAT32でフォーマットされているか確認
- 配線を確認（特にMISO/MOSI）

### 音が出ない
- スピーカーの接続を確認
- MAX98357Aの電源（5V）を確認
- I2S配線を確認
- WAVファイルがPCM形式か確認

### シリアル通信ができない
- ボーレート115200に設定
- 改行コードをLFに設定
- USBケーブルがデータ転送対応か確認

詳細は [HARDWARE.md](HARDWARE.md) のトラブルシューティングセクションを参照してください。

## 開発情報

技術的な詳細は以下のドキュメントを参照:

- [HARDWARE.md](HARDWARE.md) - ハードウェア構成と配線
- [CLAUDE.md](CLAUDE.md) - 開発ドキュメント、技術詳細

## ライセンス

本プロジェクトは参考実装として提供されます。使用は自己責任でお願いします。

## 連絡先・サポート

問題や質問がある場合は、プロジェクトのissueトラッカーをご利用ください。
