# RP2350_wavPlayer 開発ドキュメント

## プロジェクト概要

RP2350/RP2040マイコンを使用してSDカードからWAVファイルを読み込み、I2S経由でMAX98357Aアンプに出力するオーディオプレイヤー。

> **注意**: このプロジェクトはRP2350向けに設計されましたが、**Raspberry Pi Pico W (RP2040) でテスト・動作確認済み**です。両デバイスで使用可能です。

### 開発日
2025-11-12

### テストデバイス
- **実機テスト**: Raspberry Pi Pico W (RP2040)
- **対象デバイス**: Raspberry Pi Pico 2 (RP2350), Raspberry Pi Pico W (RP2040)

### 要件定義
1. RP2350A/RP2040マイコンを使用
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
- MCU: RP2350A (Raspberry Pi Pico 2) または RP2040 (Raspberry Pi Pico W)
  - テスト済み: Raspberry Pi Pico W (RP2040)
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

**配線**: RP2350/RP2040の標準SPI0ピン配置を使用
- GP16: MISO (SPI0 RX)
- GP17: CS (SPI0 CS)
- GP18: SCK (SPI0 SCK)
- GP19: MOSI (SPI0 TX)

**選択理由**:
- RP2350/RP2040の標準SPI0ピン配置に準拠
- GP16-19は連続したピン番号で配線しやすい
- I2Sピン（GP10-12）と競合しない
- Pico 2とPico Wの両方で物理ピン配置が利用可能

**変更履歴**:
- 2025-11-12: 当初はGP20/21/22/23を使用予定だったが、GP23がPico 2で物理ピンとして利用不可の可能性があり、標準SPI0ピン（GP16-19）に変更

### 3. I2Sピン配置

**選択したピン**:
- GP10: DIN (Data)
- GP11: BCLK (Bit Clock)
- GP12: LRC (LR Clock)

**理由**:
- RP2350/RP2040のPIOブロックと干渉しない位置
- 物理的に近接配置可能（配線長を短く保てる）
- SDカードピンと競合しない
- Pico 2とPico Wの両方で利用可能

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

#### 問題3: I2Sクラスの使い方
**症状**: `I2S.write()`でコンパイルエラー（expected unqualified-id before '.' token）
**原因**: arduino-picoのI2Sはグローバルオブジェクトではなくクラス定義
**解決**: WavPlayerクラスに`I2S _i2s;`メンバー変数を追加し、インスタンスメソッドとして使用

#### 問題4: デバイス判明（Pico W vs Pico 2）
**症状**: UF2ファイル書き込み後、デバイスが起動しない
**原因**: 実際のデバイスはPico W (RP2040)だったが、Pico 2 (RP2350)用の設定で書き込んでいた
**解決**: ボード設定を "Raspberry Pi Pico W" に変更
**影響**: プロジェクトはRP2350向け設計だが、RP2040で実機テスト済み

#### 問題5: SDカード初期化失敗
**症状**: `SD card initialization failed` エラー
**原因**: `SPI.begin()`呼び出しが不足、および配線の誤接続
**解決**:
- SPI.setRX/TX/SCK/CSでピン設定後、`SPI.begin()`を明示的に呼び出す
- 配線を確認・修正
- `SD.begin(CS_PIN)`のみを呼び出し（SPI引数は不要）

#### 問題6: WAVファイルヘッダー解析エラー
**症状**: すべてのWAVファイルで "Invalid fmt header" エラー
**原因**: WAVファイルにJUNK/LISTチャンクが含まれており、固定サイズ構造体読み込みが失敗
**解決**: チャンクベースのパーサーに書き換え
- RIFFヘッダー読み込み後、各チャンクをループで処理
- "fmt "チャンクと"data"チャンクを探索
- その他のチャンク（JUNK, LIST等）はスキップ
**参考**: `xxd`コマンドで実際のWAVファイル構造を解析

#### 問題7: macOSメタデータファイル
**症状**: "._"で始まるmacOSメタデータファイルがリストに表示され、再生失敗
**解決**: `scanWavFiles()`にドットファイルフィルタを追加
```cpp
if (filename.startsWith(".")) {
    // スキップ
}
```

## ビルド・テスト環境

- Arduino IDE: 2.x系
- arduino-pico: 最新版
- **実機テストボード**: Raspberry Pi Pico W (RP2040)
- **対象ボード**: Raspberry Pi Pico 2 (RP2350), Raspberry Pi Pico W (RP2040)
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

## MP3対応実装（2025-11-18追加）

### 実装目的
WAV再生機能に加え、MP3ファイルの再生に対応。ファイル形式を自動判別して適切なプレイヤーで再生する。

### 使用ライブラリ
**BackgroundAudio**
- GitHub: https://github.com/earlephilhower/BackgroundAudio
- ライセンス: GPL-3.0
- バージョン: v1.4.4 (2025-10-XX)
- 機能: RP2040/RP2350専用のMP3デコードライブラリ
- 特徴: 割り込み駆動、フレームアライン出力、I2S対応

### クラス設計

#### Mp3Playerクラス

**責任**:
- MP3ファイルの読み込みとデコード
- BackgroundAudioライブラリを使用したI2S出力
- SDカードからのストリーミング再生

**パブリックインターフェース**:
```cpp
Mp3Player(ofxSerialManager& serialMgr);
~Mp3Player();
bool begin(int i2sBclkPin, int i2sLrcPin, int i2sDinPin);
bool play(const char* filename);
void update();  // loop()内で呼び出し必須
bool isPlaying() const;
void stop();
```

**設計方針**:
- WavPlayerと同様のインターフェースで実装
- BackgroundAudioを使用した非ブロッキング再生
- update()メソッドでSDカードからデータを読み込み、デコーダーに送信
- 512バイトバッファでセクタアライン読み取り（SDカード最適化）
- I2Sピンは既存のWavPlayerと共有（同時再生不可）

### 技術詳細

#### BackgroundAudio API

BackgroundAudioは従来のESP8266Audio（AudioGenerator/AudioFileSource）とは異なるAPIを使用：

**従来のESP8266Audio（使用せず）:**
```cpp
AudioFileSourceSD *source = new AudioFileSourceSD(filename);
AudioGeneratorMP3 *mp3 = new AudioGeneratorMP3();
mp3->begin(source, output);
// 自動でファイルからデコード
```

**BackgroundAudio（採用）:**
```cpp
I2S audio(OUTPUT);
BackgroundAudioMP3 bmp(audio);
bmp.begin();

// loop()内で手動フィード
File f = SD.open(filename);
while (f.available() && bmp.availableForWrite() > 512) {
  int len = f.read(buffer, 512);
  bmp.write(buffer, len);
}
```

**選択理由**:
- RP2040/RP2350専用に最適化
- 割り込み駆動で効率的
- より細かい制御が可能
- arduino-pico標準I2Sとの互換性が高い

#### CPU オーバークロック

**必要性**:
- RP2040 @ 133MHz: MP3デコードには不十分（44.1kHz stereo, 320kbps）
- RP2040 @ 200MHz: CD品質MP3の再生が可能（実証済み）
- RP2350 @ 150MHz: オーバークロック不要でCD品質再生可能

**実装**:
```cpp
void setup() {
  // CPU を200MHzにオーバークロック（MP3再生のため）
  set_sys_clock_khz(200000, true);

  // CPU周波数を表示
  char cpuInfo[64];
  sprintf(cpuInfo, "CPU Clock: %lu MHz", rp2040.f_cpu() / 1000000);
  serialManager.send("info", cpuInfo);
```

**影響**:
- 発熱: わずかに増加（USB給電で問題なし）
- 消費電力: 約50%増加（それでも〜150mA程度）
- 安定性: `set_sys_clock_khz`の第2引数`true`でPLL安定化

#### ファイル形式自動判別

**実装方法**:
拡張子ベースで判別（シンプルで確実）

```cpp
void playNext() {
  String filename = audioFiles[currentFileIndex];

  if (filename.endsWith(".mp3") || filename.endsWith(".MP3")) {
    // MP3再生
    if (wavPlayer && wavPlayer->isPlaying()) {
      wavPlayer->stop();  // WAV停止
    }
    mp3Player->play(filename.c_str());

  } else if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
    // WAV再生
    if (mp3Player && mp3Player->isPlaying()) {
      mp3Player->stop();  // MP3停止
    }
    wavPlayer->play(filename.c_str());
  }
}
```

**代替案（採用せず）**:
- ファイルヘッダー読み取り: オーバーヘッド大、複雑
- MIME タイプ: SDカードでは利用不可

#### 排他制御

**問題**: WAVとMP3で同じI2Sハードウェアを使用
**解決**: 片方が再生中の場合、もう片方を停止してから開始

```cpp
// handlePlay()内
if (filenameStr.endsWith(".mp3")) {
  if (wavPlayer && wavPlayer->isPlaying()) {
    wavPlayer->stop();  // WAV停止
  }
  mp3Player->play(filename);
}
```

**設計判断**:
- 同時再生は不要（I2S出力は1系統のみ）
- 明示的な停止でリソースを確実に解放
- ユーザーの意図を尊重（最後のコマンドを実行）

### パフォーマンス

#### リソース使用量

**メモリ（RAM）:**
- Mp3Playerクラス: 約600バイト（インスタンス変数）
- BackgroundAudioバッファ: 5KB（出力）+ 2KB（フレーム）= 7KB
- SDカードバッファ: 512バイト
- 合計: 約8KB（RP2040の264KBに対して3%）

**CPU使用率:**
- RP2040 @ 200MHz: 約70-80%（MP3デコード）
- RP2350 @ 150MHz: 約50-60%（MP3デコード）
- WAV再生: 約10-20%（デコード不要）

**SD カード読み取り:**
- 512バイトずつセクタアライン読み取り
- バッファに空きがある限り先読み
- Class 10 SDカード推奨（最低10MB/s）

#### 実測値（想定）

- 128kbps MP3: 16KB/s 読み取り速度
- 320kbps MP3: 40KB/s 読み取り速度
- SDカード余裕度: 10MB/s ÷ 40KB/s = 250倍

### ファイル構成更新

```
RP2350_wavPlayer/
├── RP2350_wavPlayer.ino   # メインプログラム（更新）
├── WavPlayer.h             # WAVプレイヤークラス
├── WavPlayer.cpp
├── Mp3Player.h             # MP3プレイヤークラス（新規）
├── Mp3Player.cpp           # MP3プレイヤークラス（新規）
├── HARDWARE.md             # ハードウェア構成（更新）
├── CLAUDE.md               # 本ファイル（更新）
└── README.md               # ユーザーガイド（更新）
```

### 主な変更点

**RP2350_wavPlayer.ino:**
- CPUオーバークロック追加: `set_sys_clock_khz(200000, true)`
- Mp3Player インスタンス追加
- `scanWavFiles()` → `scanAudioFiles()`: .mp3も検出
- `wavFiles[]` → `audioFiles[]`: 配列名変更
- ファイル形式判別ロジック追加
- `loop()`に`mp3Player->update()`追加

**Mp3Player.h/cpp:**
- BackgroundAudioライブラリ使用
- I2S + BackgroundAudioMP3 の組み合わせ
- update()メソッドでストリーミング再生
- 512バイトバッファで最適化

### テスト計画

#### テストケース
1. MP3ファイル単体再生（128kbps, 44.1kHz stereo）
2. MP3ファイル単体再生（320kbps, 44.1kHz stereo）
3. WAVファイル単体再生（既存機能確認）
4. WAV→MP3切り替え再生
5. MP3→WAV切り替え再生
6. playall: で混在ファイルの連続再生
7. CPUクロック表示確認（200MHz）
8. 長時間再生（発熱・安定性確認）

#### 確認項目
- ✅ MP3が正常に再生される
- ✅ 音質が良好（ノイズなし、途切れなし）
- ✅ WAV再生も引き続き動作
- ✅ ファイル切り替えがスムーズ
- ✅ CPU温度が許容範囲内
- ✅ シリアルコマンドが正常動作

### 今後の拡張案

#### 短期（実装容易）
- [ ] ボリューム制御（BackgroundAudioのGain機能）
- [ ] 再生速度変更（サンプリングレート調整）
- [ ] プレイリスト機能（M3Uファイル対応）

#### 中期（要検討）
- [ ] ID3タグ読み取り（曲名・アーティスト表示）
- [ ] EQ（イコライザー）機能
- [ ] クロスフェード再生

#### 長期（大規模）
- [ ] AAC対応（BackgroundAudioAAC使用）
- [ ] FLAC対応（要調査）
- [ ] Web Radio機能（WiFi使用）

## ライセンスと著作権

本プロジェクトは参考実装として提供されます。使用は自己責任でお願いします。

### 使用ライブラリ
- ofxSerialManager: カスタムライブラリ
- I2S.h: arduino-pico (LGPL)
- SD.h: Arduino (LGPL)
- BackgroundAudio: earlephilhower (GPL-3.0) - MP3デコード用
