/**
 * RP2350_wavPlayer
 *
 * 注意：
 * このプロジェクトはRP2350とRP2040の両方で動作します。
 * Raspberry Pi Pico W (RP2040) でテスト・動作確認済み。
 *
 * 目的：
 * SDカードからWAVファイルを読み込み、I2S経由でMAX98357Aアンプに出力する
 * ofxSerialManagerでシリアル通信を行う
 *
 * 機能：
 * - Serial接続待機（最大5秒）
 * - SDカード内のWAVファイル一覧表示
 * - 名前順に順番に再生
 *
 * ハードウェア構成：
 * [I2S - MAX98357A]
 * - GP10: DIN (I2S Data)
 * - GP11: BCLK (Bit Clock)
 * - GP12: LRC (LR Clock / Word Select)
 *
 * [SPI - SDカード]
 * - GP17: CS (Chip Select) - SPI0
 * - GP18: SCK (Serial Clock) - SPI0
 * - GP19: MOSI (Master Out Slave In) - SPI0
 * - GP16: MISO (Master In Slave Out) - SPI0
 *
 * 対応デバイス：
 * - Raspberry Pi Pico 2 (RP2350)
 * - Raspberry Pi Pico W (RP2040)
 */

#include <SD.h>
#include <SPI.h>
#include "ofxSerialManager.h"
#include "WavPlayer.h"
#include "Mp3Player.h"

// I2Sピン定義
#define I2S_BCLK_PIN  11   // Bit Clock
#define I2S_LRC_PIN   12   // LR Clock (Word Select)
#define I2S_DIN_PIN   10   // Data Input

// SDカードピン定義（RP2350標準SPI0ピン）
#define SD_CS_PIN     17   // Chip Select (SPI0 CS)
#define SD_SCK_PIN    18   // Serial Clock (SPI0 SCK)
#define SD_MOSI_PIN   19   // Master Out Slave In (SPI0 TX)
#define SD_MISO_PIN   16   // Master In Slave Out (SPI0 RX)

// シリアル通信マネージャー
ofxSerialManager serialManager;

// WAVプレイヤー
WavPlayer* wavPlayer = nullptr;

// MP3プレイヤー
Mp3Player* mp3Player = nullptr;

// オーディオファイルリスト（WAV + MP3）
String audioFiles[100];
int audioFileCount = 0;
int currentFileIndex = 0;

// ===== マルチコア用共有変数 =====
// Core0（メイン）がコマンドを受け付け、Core1（サブ）が再生処理を行う
volatile bool core1_shouldStop = false;      // Core0がtrueにセット → Core1が停止
volatile bool core1_isPlaying = false;       // Core1が再生中かどうか
String core1_nextFile = "";                  // 次に再生するファイル名
volatile bool core1_hasRequest = false;      // 新しい再生リクエストがあるか

/**
 * SDカード内のオーディオファイルをスキャン（WAV + MP3）
 */
void scanAudioFiles() {
  audioFileCount = 0;

  File root = SD.open("/");
  if (!root) {
    serialManager.send("error", "Failed to open root directory");
    return;
  }

  if (!root.isDirectory()) {
    serialManager.send("error", "Root is not a directory");
    root.close();
    return;
  }

  serialManager.send("info", "Scanning audio files...");

  File entry = root.openNextFile();
  while (entry && audioFileCount < 100) {
    if (!entry.isDirectory()) {
      String filename = String(entry.name());

      // ドットで始まるファイル（隠しファイル、macOSのメタデータ）を除外
      if (filename.startsWith(".")) {
        entry.close();
        entry = root.openNextFile();
        continue;
      }

      // .wav または .mp3 ファイルをリストに追加
      if (filename.endsWith(".wav") || filename.endsWith(".WAV") ||
          filename.endsWith(".mp3") || filename.endsWith(".MP3")) {
        audioFiles[audioFileCount] = filename;
        audioFileCount++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();

  // ファイル名でソート（バブルソート）
  for (int i = 0; i < audioFileCount - 1; i++) {
    for (int j = 0; j < audioFileCount - i - 1; j++) {
      if (audioFiles[j] > audioFiles[j + 1]) {
        String temp = audioFiles[j];
        audioFiles[j] = audioFiles[j + 1];
        audioFiles[j + 1] = temp;
      }
    }
  }

  char msg[64];
  sprintf(msg, "Found %d audio files (WAV + MP3)", audioFileCount);
  serialManager.send("info", msg);
}

/**
 * オーディオファイル一覧を表示
 */
void printAudioFileList() {
  serialManager.send("info", "=== Audio Files on SD Card (WAV + MP3) ===");

  for (int i = 0; i < audioFileCount; i++) {
    char msg[128];
    sprintf(msg, "[%d] %s", i, audioFiles[i].c_str());
    serialManager.send("info", msg);
  }

  char summary[64];
  sprintf(summary, "Total: %d files", audioFileCount);
  serialManager.send("info", summary);
}

/**
 * リストコマンドハンドラ
 */
void handleList(const char* payload, int length) {
  printAudioFileList();
}

/**
 * 再生コマンドハンドラ（マルチコア対応）
 */
void handlePlay(const char* payload, int length) {
  // 既存の再生を停止
  if (core1_isPlaying) {
    core1_shouldStop = true;
    // Core1が停止するまで待つ
    unsigned long startTime = millis();
    while (core1_isPlaying && (millis() - startTime < 1000)) {
      yield();
    }
  }

  if (length > 0) {
    // ファイル名指定
    char filename[128];
    memcpy(filename, payload, length);
    filename[length] = '\0';
    core1_nextFile = String(filename);
  } else {
    // 次のファイルを再生
    if (audioFileCount == 0) {
      serialManager.send("warn", "No audio files found");
      return;
    }
    if (currentFileIndex >= audioFileCount) {
      currentFileIndex = 0;
    }
    core1_nextFile = audioFiles[currentFileIndex];
    currentFileIndex++;
  }

  // Core1に再生リクエスト
  core1_hasRequest = true;
}

/**
 * 全ファイル順次再生コマンドハンドラ（マルチコア対応）
 */
void handlePlayAll(const char* payload, int length) {
  currentFileIndex = 0;

  while (currentFileIndex < audioFileCount && !core1_shouldStop) {
    // 次のファイルをCore1に送信
    core1_nextFile = audioFiles[currentFileIndex];
    core1_hasRequest = true;

    // Core1が再生を開始するまで待つ
    delay(100);

    // 再生が終わるまで待つ（停止フラグもチェック）
    while (core1_isPlaying && !core1_shouldStop) {
      serialManager.update();
      yield();
    }

    // 停止フラグがセットされていたらループを抜ける
    if (core1_shouldStop) {
      break;
    }

    currentFileIndex++;

    // ファイル間に少し間隔を開ける
    delay(500);
  }

  if (core1_shouldStop) {
    serialManager.send("info", "Playback stopped by user");
  } else {
    serialManager.send("info", "All files played");
  }
}

/**
 * 停止コマンドハンドラ（マルチコア対応）
 */
void handleStop(const char* payload, int length) {
  // Core1に停止を指示
  core1_shouldStop = true;
  serialManager.send("info", "Stop command sent");
}

/**
 * 再スキャンコマンドハンドラ
 */
void handleScan(const char* payload, int length) {
  scanAudioFiles();
  printAudioFileList();
}

/**
 * ヘルプコマンドハンドラ
 */
void handleHelp(const char* payload, int length) {
  serialManager.send("info", "=== RP2350_wavPlayer Commands ===");
  serialManager.send("info", "");
  serialManager.send("info", "list - Show WAV file list");
  serialManager.send("info", "play - Play next file");
  serialManager.send("info", "play <filename> - Play specific file");
  serialManager.send("info", "playall - Play all files in order");
  serialManager.send("info", "stop - Stop playback");
  serialManager.send("info", "scan - Rescan SD card");
  serialManager.send("info", "help - Show this help");
}

// ===== Core1 関数（サブコア：再生処理専用）=====

/**
 * Core1のセットアップ（必要なし、setup()で初期化済み）
 */
void setup1() {
  // 何もしない（プレイヤーはCore0のsetup()で初期化される）
}

/**
 * Core1のループ（再生処理専用）
 */
void loop1() {
  // 新しい再生リクエストがあるかチェック
  if (core1_hasRequest) {
    core1_hasRequest = false;  // リクエストをクリア
    core1_shouldStop = false;  // 停止フラグをクリア
    core1_isPlaying = true;    // 再生中フラグをセット

    String filename = core1_nextFile;

    // ファイル形式を判別して適切なプレイヤーで再生
    if (filename.endsWith(".mp3") || filename.endsWith(".MP3")) {
      // MP3ファイルの場合（非ブロッキング再生）
      mp3Player->play(filename.c_str(), &core1_shouldStop);

      // MP3デコードループ（stopFlagをチェックしながら）
      while (mp3Player->isPlaying() && !core1_shouldStop) {
        mp3Player->update(&core1_shouldStop);
        yield();
      }

      // 再生終了処理
      if (mp3Player->isPlaying()) {
        mp3Player->stop();
      }

    } else if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
      // WAVファイルの場合（ブロッキング再生）
      wavPlayer->play(filename.c_str(), &core1_shouldStop);
      // play()がブロッキングで完了するまで待つ
    }

    // 再生完了
    core1_isPlaying = false;
    core1_shouldStop = false;
  }

  // アイドル時は譲る
  yield();
}

// ===== Core0 関数（メインコア：シリアル通信・コマンド処理）=====

void setup() {
  // CPU を200MHzにオーバークロック（MP3再生のため）
  set_sys_clock_khz(200000, true);

  // シリアル通信初期化
  Serial.begin(115200);

  // シリアル接続待ち（最大5秒）
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart < 5000)) {
    delay(10);
  }

  // ofxSerialManagerセットアップ
  serialManager.setup(&Serial);

  // CPU周波数を表示
  char cpuInfo[64];
  sprintf(cpuInfo, "CPU Clock: %lu MHz", rp2040.f_cpu() / 1000000);
  serialManager.send("info", cpuInfo);

  // コマンドハンドラ登録
  serialManager.addListener("list", handleList);
  serialManager.addListener("play", handlePlay);
  serialManager.addListener("playall", handlePlayAll);
  serialManager.addListener("stop", handleStop);
  serialManager.addListener("scan", handleScan);
  serialManager.addListener("help", handleHelp);

  serialManager.send("info", "RP2350_wavPlayer initialized");

  // SDカード初期化（SPI0使用、カスタムピン）
  char pinInfo[128];
  sprintf(pinInfo, "SPI Pins: CS=%d, SCK=%d, MOSI=%d, MISO=%d", SD_CS_PIN, SD_SCK_PIN, SD_MOSI_PIN, SD_MISO_PIN);
  serialManager.send("info", pinInfo);

  // SPI0の設定（ピン設定はbegin前に行う）
  SPI.setRX(SD_MISO_PIN);
  SPI.setTX(SD_MOSI_PIN);
  SPI.setSCK(SD_SCK_PIN);
  SPI.setCS(SD_CS_PIN);

  // SPIを初期化
  SPI.begin();

  serialManager.send("info", "SPI configured and started");
  serialManager.send("info", "Attempting SD card initialization...");

  // CSピンだけを指定（SPIは既に設定済み）
  if (!SD.begin(SD_CS_PIN)) {
    serialManager.send("error", "SD card initialization failed");
    serialManager.send("error", "Check: 1) Card inserted? 2) FAT32 format? 3) Wiring correct?");
    serialManager.send("error", "Please fix and reset");
    return;
  }

  serialManager.send("info", "SD card initialized successfully");

  // WAVプレイヤー初期化
  wavPlayer = new WavPlayer(serialManager);
  if (!wavPlayer->begin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN)) {
    serialManager.send("error", "WavPlayer initialization failed");
    return;
  }

  serialManager.send("info", "WavPlayer initialized");

  // MP3プレイヤー初期化
  mp3Player = new Mp3Player(serialManager);
  if (!mp3Player->begin(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN)) {
    serialManager.send("error", "Mp3Player initialization failed");
    return;
  }

  serialManager.send("info", "Mp3Player initialized");

  // オーディオファイル（WAV + MP3）をスキャン
  scanAudioFiles();
  printAudioFileList();

  // 起動完了メッセージ
  if (audioFileCount > 0) {
    serialManager.send("info", "Ready for playback. Use 'playall:' to start automatic playback.");
  } else {
    serialManager.send("warn", "No audio files found. Ready for commands.");
  }
}

void loop() {
  // Core0: シリアル通信処理のみ
  serialManager.update();

  // リアルタイム処理のため、delayではなくyieldを使用
  yield();
}
