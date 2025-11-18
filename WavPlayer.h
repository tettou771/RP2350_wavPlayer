/**
 * WavPlayer.h
 *
 * WAVファイル再生クラス
 * SDカードからWAVファイルを読み込み、I2S経由で再生する
 */

#pragma once

#include <Arduino.h>
#include <I2S.h>
#include <SD.h>
#include "ofxSerialManager.h"

/**
 * WAVファイルヘッダー構造体
 */
struct WavHeader {
  char riffHeader[4];       // "RIFF"
  uint32_t wavSize;         // ファイルサイズ - 8
  char waveHeader[4];       // "WAVE"
  char fmtHeader[4];        // "fmt "
  uint32_t fmtChunkSize;    // fmtチャンクサイズ（通常16）
  uint16_t audioFormat;     // オーディオフォーマット（1=PCM）
  uint16_t numChannels;     // チャンネル数（1=モノラル, 2=ステレオ）
  uint32_t sampleRate;      // サンプリングレート
  uint32_t byteRate;        // バイトレート
  uint16_t blockAlign;      // ブロックアライン
  uint16_t bitsPerSample;   // ビット深度（8, 16, 24, 32）
};

/**
 * データチャンクヘッダー構造体
 */
struct DataChunkHeader {
  char dataHeader[4];       // "data"
  uint32_t dataSize;        // データサイズ
};

/**
 * WAVプレイヤークラス
 */
class WavPlayer {
public:
  /**
   * コンストラクタ
   * @param serialMgr シリアル通信マネージャーへの参照
   */
  WavPlayer(ofxSerialManager& serialMgr);

  /**
   * 初期化
   * @param i2sBclkPin I2S Bit Clock pin
   * @param i2sLrcPin I2S LR Clock pin
   * @param i2sDinPin I2S Data pin
   * @return 成功時true
   */
  bool begin(int i2sBclkPin, int i2sLrcPin, int i2sDinPin);

  /**
   * WAVファイルを再生
   * @param filename ファイル名（絶対パス）
   * @param stopFlag 外部から停止を指示するフラグへのポインタ（オプション）
   * @return 成功時true
   */
  bool play(const char* filename, volatile bool* stopFlag = nullptr);

  /**
   * 現在の再生状態を取得
   * @return 再生中ならtrue
   */
  bool isPlaying() const { return _isPlaying; }

  /**
   * 再生を停止
   */
  void stop();

private:
  /**
   * WAVヘッダーを読み込み、検証する
   * @param file ファイルオブジェクト
   * @param header ヘッダー構造体（出力）
   * @param dataChunk データチャンクヘッダー（出力）
   * @return 成功時true
   */
  bool readWavHeader(File& file, WavHeader& header, DataChunkHeader& dataChunk);

  /**
   * 16bit stereo データを再生
   */
  void play16BitStereo(File& file, uint32_t dataSize, volatile bool* stopFlag);

  /**
   * 16bit mono データを再生
   */
  void play16BitMono(File& file, uint32_t dataSize, volatile bool* stopFlag);

  /**
   * 8bit stereo データを再生
   */
  void play8BitStereo(File& file, uint32_t dataSize, volatile bool* stopFlag);

  /**
   * 8bit mono データを再生
   */
  void play8BitMono(File& file, uint32_t dataSize, volatile bool* stopFlag);

  ofxSerialManager& _serialManager;

  int _i2sBclkPin;
  int _i2sLrcPin;
  int _i2sDinPin;

  bool _isPlaying;
  File _currentFile;

  I2S _i2s;  // I2Sインスタンス

  static const int BUFFER_SIZE = 512;
  uint8_t _buffer[BUFFER_SIZE];
};
