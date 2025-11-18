# RP2350_wavPlayer 開発ドキュメント

## プロジェクト概要

RP2350マイコンを使用してSDカードからWAVファイルを読み込み、I2S経由でMAX98357Aアンプに出力するオーディオプレイヤー。

### 開発日
2025-11-12

### 要件定義
1. RP2350Aマイコンを使用
2. I2S出力でMAX98357Aアンプを駆動
3. SDカード（SPI1予定→SPI0使用）からWAVファイル読み込み
4. ofxSerialManagerライブラリを使用したシリアル通信
5. Serial接続を最大5秒待機
6. WAVファイル一覧をシリアル出力
7. 名前順に自動再生

## 技術スタック

### 使用ライブラリ
- **I2S.h**: arduino-picoに含まれるI2S通信ライブラリ
- **SD.h**: Arduino標準SDカードライブラリ
- **SPI.h**: Arduino標準SPIライブラリ
- **ofxSerialManager**: カスタムシリアル通信ライブラリ（../../libraries/ofxSerialManager）

### ハードウェア
- MCU: RP2350A (Raspberry Pi Pico 2)
- アンプ: MAX98357A (I2S Class D Amplifier)
- ストレージ: microSDカード

## 設計判断

### 1. DFRobot_MAX98357Aライブラリの不使用

**問題**: 当初、`../../libraries/DFRobot_MAX98357A`の使用を検討したが、このライブラリはESP32専用であることが判明。

**理由**:
- ESP32固有のBluetooth機能（`esp_bt_main.h`, `esp_a2dp_api.h`）に依存
- ESP32のI2S実装（`driver/i2s.h`）を使用
- FreeRTOSタスク管理に依存

**解決策**: arduino-pico標準の`I2S.h`ライブラリを使用し、独自のWAV再生ロジックを実装。

### 2. SPI設定（SPI0の使用）

**配線**: RP2350の標準SPI0ピン配置を使用
- GP16: MISO (SPI0 RX)
- GP17: CS (SPI0 CS)
- GP18: SCK (SPI0 SCK)
- GP19: MOSI (SPI0 TX)

**選択理由**:
- RP2350の標準SPI0ピン配置に準拠
- GP16-19は連続したピン番号で配線しやすい
- I2Sピン（GP10-12）と競合しない
- Pico 2の物理ピン配置で確実にアクセス可能

**変更履歴**:
- 2025-11-12: 当初はGP20/21/22/23を使用予定だったが、GP23がPico 2で物理ピンとして利用不可の可能性があり、標準SPI0ピン（GP16-19）に変更

### 3. I2Sピン配置

**選択したピン**:
- GP10: DIN (Data)
- GP11: BCLK (Bit Clock)
- GP12: LRC (LR Clock)

**理由**:
- RP2350のPIOブロックと干渉しない位置
- 物理的に近接配置可能（配線長を短く保てる）
- SDカードピンと競合しない

### 4. シリアル通信（ofxSerialManager）

**実装方針**:
- `Serial.print()`や`Serial.println()`を直接使用しない
- すべての出力を`serialManager.send(cmd, payload)`経由で送信
- コマンドベースの通信プロトコル

**メッセージフォーマット**:
```
<command>:<payload>\n
```

**使用コマンド**:
- `info`: 情報メッセージ
- `error`: エラーメッセージ
- `warn`: 警告メッセージ

## 実装詳細

### WAVファイルパーサー

#### 構造体定義

```cpp
struct WavHeader {
  char riffHeader[4];       // "RIFF"
  uint32_t wavSize;         // ファイルサイズ - 8
  char waveHeader[4];       // "WAVE"
  char fmtHeader[4];        // "fmt "
  uint32_t fmtChunkSize;    // fmtチャンクサイズ（通常16）
  uint16_t audioFormat;     // 1=PCM
  uint16_t numChannels;     // 1=モノラル, 2=ステレオ
  uint32_t sampleRate;      // サンプリングレート
  uint32_t byteRate;        // バイトレート
  uint16_t blockAlign;      // ブロックアライン
  uint16_t bitsPerSample;   // 8, 16, 24, 32
};

struct DataChunkHeader {
  char dataHeader[4];       // "data"
  uint32_t dataSize;        // データサイズ
};
```

#### 対応フォーマット

実装では以下の4パターンを対応:
1. **16bit stereo**: 最も一般的、左右サンプルをそのまま出力
2. **16bit mono**: 単一サンプルを両チャンネルに複製
3. **8bit stereo**: 符号なし8bit (0-255) を符号付き16bit (-32768〜32767) に変換
4. **8bit mono**: 符号なし8bitを変換して両チャンネルに出力

#### 8bit→16bit変換ロジック

```cpp
int16_t sample = ((int16_t)buffer[i] - 128) << 8;
```

- 8bit unsigned (0-255) から128を引いて符号付き (-128〜127) に変換
- 8bit左シフトして16bit範囲に拡張

### I2S出力

#### 初期化

```cpp
I2S.setBCLK(I2S_BCLK_PIN);    // GP11
I2S.setDOUT(I2S_DIN_PIN);     // GP10
I2S.setBitsPerSample(bitsPerSample);
I2S.begin(sampleRate);
```

#### データ送信

```cpp
I2S.write(leftSample, rightSample);
```

- 左右チャンネルのサンプルを送信
- モノラルの場合は同じサンプルを両チャンネルに送信

### ファイル管理

#### スキャン処理

```cpp
void scanWavFiles() {
  // ルートディレクトリを開く
  File root = SD.open("/");

  // .wavファイルを抽出
  while (entry && wavFileCount < 100) {
    if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
      wavFiles[wavFileCount++] = filename;
    }
  }

  // バブルソートで名前順にソート
  for (int i = 0; i < wavFileCount - 1; i++) {
    for (int j = 0; j < wavFileCount - i - 1; j++) {
      if (wavFiles[j] > wavFiles[j + 1]) {
        // swap
      }
    }
  }
}
```

#### 自動再生

```cpp
void setup() {
  // ... 初期化処理 ...

  // 自動再生開始
  if (wavFileCount > 0) {
    currentFileIndex = 0;
    while (currentFileIndex < wavFileCount) {
      playNext();
      delay(500);  // ファイル間に0.5秒の間隔
    }
  }
}
```

## コマンドインターフェース

### 利用可能なコマンド

| コマンド | 引数 | 説明 |
|---------|------|------|
| `list` | なし | WAVファイル一覧を表示 |
| `play` | なし | 次のファイルを再生 |
| `play` | filename | 指定ファイルを再生 |
| `playall` | なし | 全ファイルを順番に再生 |
| `scan` | なし | SDカードを再スキャン |
| `help` | なし | ヘルプを表示 |

### 使用例

```
list:
play:
play:/music/song.wav
playall:
scan:
help:
```

## パフォーマンス最適化

### バッファサイズ
- 512バイトのバッファを使用
- SDカードからの読み込みとI2S出力のバランスを考慮

### yield()呼び出し
- 再生ループ内で`yield()`を呼び出し、ウォッチドッグタイマーリセット
- 長時間再生でもシステムが安定動作

### シリアル通信の並行処理
- 再生中も`serialManager.update()`を定期的に呼び出し
- コマンド受信を妨げない設計

## 既知の制限事項

1. **圧縮フォーマット非対応**: PCM形式のWAVファイルのみ対応（MP3, AAC等は非対応）
2. **24bit/32bit PCM**: 現在未実装（必要に応じて追加可能）
3. **サンプリングレート**: 高すぎるレート（96kHz等）では再生が追いつかない可能性
4. **ファイル数上限**: 100ファイルまで（配列サイズの制限）
5. **ディレクトリ未対応**: ルートディレクトリのファイルのみをスキャン

## 将来の拡張案

### 機能追加候補
- [ ] サブディレクトリのスキャン対応
- [ ] 24bit/32bit PCM対応
- [ ] プレイリスト機能
- [ ] ランダム再生
- [ ] リピート再生
- [ ] ボリューム制御（I2SレベルまたはPWM制御）
- [ ] 一時停止/再開機能
- [ ] シーク機能（早送り/巻き戻し）
- [ ] メタデータ表示（ID3タグ等）
- [ ] OLED/LCD表示対応

### パフォーマンス改善
- [ ] DMA転送の活用
- [ ] マルチコア処理（Core0: ファイルI/O, Core1: I2S出力）
- [ ] より大きなバッファリング

## 参考プロジェクト

### NuiController
- パス: `../NuiController`
- 参考にした点:
  - SPI配線（SDカード）
  - ofxSerialManagerの使用方法
  - プロジェクト構成

### DFRobot_MAX98357A (ESP32用)
- パス: `../../libraries/DFRobot_MAX98357A`
- 参考にした点:
  - WAVファイルパーサーの構造
  - ファイルスキャンロジック
- 注意: ESP32専用のためRP2350では使用不可

## トラブルシューティングログ

### 開発中に遭遇した問題

#### 問題1: DFRobot_MAX98357AライブラリがRP2350で使えない
**症状**: コンパイルエラー（ESP32固有ヘッダーが見つからない）
**原因**: ライブラリがESP32専用設計
**解決**: arduino-pico標準のI2S.hを使用

#### 問題2: SDカード配線の決定
**検討事項**: SPI0とSPI1のどちらを使用するか
**決定**: NuiControllerと同じSPI0配線を採用
**理由**: 実績ある安定した構成

## ビルド・テスト環境

- Arduino IDE: 2.x系
- arduino-pico: 最新版
- ボード: Raspberry Pi Pico 2
- テスト用SDカード: Class 10, 32GB, FAT32
- テスト用WAVファイル:
  - 44.1kHz, 16bit, stereo
  - 22.05kHz, 16bit, mono
  - 8kHz, 8bit, mono

## コード構成

### ファイル構成

```
RP2350_wavPlayer/
├── RP2350_wavPlayer.ino   # メインプログラム
├── WavPlayer.h             # WAVプレイヤークラス（ヘッダー）
├── WavPlayer.cpp           # WAVプレイヤークラス（実装）
├── HARDWARE.md             # ハードウェア構成ドキュメント
├── CLAUDE.md               # 開発ドキュメント（本ファイル）
└── README.md               # ユーザーガイド
```

### リファクタリング履歴

#### 2025-11-12: WavPlayerクラスの分離

**理由**:
- 可読性向上（.inoファイルが500行超で読みにくかった）
- 責任の分離（WAV再生ロジックとUI/ファイル管理の分離）
- 再利用性の向上（他のプロジェクトでWavPlayerを使用可能）

**変更内容**:

**WavPlayer.h / WavPlayer.cpp**:
- WAVヘッダーパーサー
- I2S初期化・制御
- フォーマット別再生ロジック（8/16bit, mono/stereo）
- 再生制御（play, stop）

**RP2350_wavPlayer.ino**:
- SDカード初期化
- ファイルスキャン・一覧表示
- シリアルコマンドハンドラ
- 自動再生シーケンス

**メリット**:
- 各ファイルが200-300行程度に収まり、読みやすい
- WavPlayerクラスは他のプロジェクトでも利用可能
- テストが容易（WavPlayerを単独でテスト可能）

### クラス設計

#### WavPlayerクラス

**責任**:
- WAVファイルの読み込みと検証
- I2S経由でのオーディオ出力
- フォーマット変換（8bit→16bit等）

**パブリックインターフェース**:
```cpp
WavPlayer(ofxSerialManager& serialMgr);
bool begin(int i2sBclkPin, int i2sLrcPin, int i2sDinPin);
bool play(const char* filename);
bool isPlaying() const;
void stop();
```

**設計方針**:
- シリアル出力はofxSerialManagerに委譲（依存性注入）
- ピン番号は初期化時に指定（ハードコーディングを避ける）
- フォーマット別の再生処理は内部メソッドに分離
- バッファはクラスメンバーとして保持（スタックオーバーフロー防止）

## ライセンスと著作権

本プロジェクトは参考実装として提供されます。使用は自己責任でお願いします。

### 使用ライブラリ
- ofxSerialManager: カスタムライブラリ
- I2S.h: arduino-pico (LGPL)
- SD.h: Arduino (LGPL)
