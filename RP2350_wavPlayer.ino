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

// WAVファイルリスト
String wavFiles[100];
int wavFileCount = 0;
int currentFileIndex = 0;

/**
 * SDカード内のWAVファイルをスキャン
 */
void scanWavFiles() {
  wavFileCount = 0;

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

  serialManager.send("info", "Scanning WAV files...");

  File entry = root.openNextFile();
  while (entry && wavFileCount < 100) {
    if (!entry.isDirectory()) {
      String filename = String(entry.name());

      // ドットで始まるファイル（隠しファイル、macOSのメタデータ）を除外
      if (filename.startsWith(".")) {
        entry.close();
        entry = root.openNextFile();
        continue;
      }

      // .wavファイルのみをリストに追加
      if (filename.endsWith(".wav") || filename.endsWith(".WAV")) {
        wavFiles[wavFileCount] = filename;
        wavFileCount++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }

  root.close();

  // ファイル名でソート（バブルソート）
  for (int i = 0; i < wavFileCount - 1; i++) {
    for (int j = 0; j < wavFileCount - i - 1; j++) {
      if (wavFiles[j] > wavFiles[j + 1]) {
        String temp = wavFiles[j];
        wavFiles[j] = wavFiles[j + 1];
        wavFiles[j + 1] = temp;
      }
    }
  }

  char msg[64];
  sprintf(msg, "Found %d WAV files", wavFileCount);
  serialManager.send("info", msg);
}

/**
 * WAVファイル一覧を表示
 */
void printWavFileList() {
  serialManager.send("info", "=== WAV Files on SD Card ===");

  for (int i = 0; i < wavFileCount; i++) {
    char msg[128];
    sprintf(msg, "[%d] %s", i, wavFiles[i].c_str());
    serialManager.send("info", msg);
  }

  char summary[64];
  sprintf(summary, "Total: %d files", wavFileCount);
  serialManager.send("info", summary);
}

/**
 * 次のファイルを再生
 */
void playNext() {
  if (wavFileCount == 0) {
    serialManager.send("warn", "No WAV files found");
    return;
  }

  if (currentFileIndex >= wavFileCount) {
    currentFileIndex = 0;  // 最初に戻る
  }

  wavPlayer->play(wavFiles[currentFileIndex].c_str());
  currentFileIndex++;
}

/**
 * リストコマンドハンドラ
 */
void handleList(const char* payload, int length) {
  printWavFileList();
}

/**
 * 再生コマンドハンドラ
 */
void handlePlay(const char* payload, int length) {
  if (length > 0) {
    // ファイル名指定
    char filename[128];
    memcpy(filename, payload, length);
    filename[length] = '\0';
    wavPlayer->play(filename);
  } else {
    // 次のファイルを再生
    playNext();
  }
}

/**
 * 全ファイル順次再生コマンドハンドラ
 */
void handlePlayAll(const char* payload, int length) {
  currentFileIndex = 0;

  while (currentFileIndex < wavFileCount) {
    playNext();
    delay(500);  // ファイル間に少し間隔を開ける
  }

  serialManager.send("info", "All files played");
}

/**
 * 停止コマンドハンドラ
 */
void handleStop(const char* payload, int length) {
  if (wavPlayer) {
    wavPlayer->stop();
    serialManager.send("info", "Playback stopped");
  }
}

/**
 * 再スキャンコマンドハンドラ
 */
void handleScan(const char* payload, int length) {
  scanWavFiles();
  printWavFileList();
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

void setup() {
  // シリアル通信初期化
  Serial.begin(115200);

  // シリアル接続待ち（最大5秒）
  unsigned long serialStart = millis();
  while (!Serial && (millis() - serialStart < 5000)) {
    delay(10);
  }

  // ofxSerialManagerセットアップ
  serialManager.setup(&Serial);

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

  // WAVファイルをスキャン
  scanWavFiles();
  printWavFileList();

  // 自動再生開始
  if (wavFileCount > 0) {
    serialManager.send("info", "Starting automatic playback...");
    delay(1000);
    currentFileIndex = 0;

    // 全ファイルを順番に再生
    while (currentFileIndex < wavFileCount) {
      playNext();
      delay(500);
    }

    serialManager.send("info", "Playback complete. Ready for commands.");
  } else {
    serialManager.send("warn", "No WAV files found. Ready for commands.");
  }
}

void loop() {
  // シリアル通信処理
  serialManager.update();

  delay(10);
}
