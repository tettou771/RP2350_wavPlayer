/**
 * Mp3Player.h
 *
 * MP3ファイル再生クラス
 * SDカードからMP3ファイルを読み込み、I2S経由で再生する
 * BackgroundAudioライブラリを使用
 */

#pragma once

#include <Arduino.h>
#include <I2S.h>
#include <SD.h>
#include <BackgroundAudioMP3.h>
#include "ofxSerialManager.h"

/**
 * MP3プレイヤークラス
 */
class Mp3Player {
public:
  /**
   * コンストラクタ
   * @param serialMgr シリアル通信マネージャーへの参照
   */
  Mp3Player(ofxSerialManager& serialMgr);

  /**
   * デストラクタ
   */
  ~Mp3Player();

  /**
   * 初期化
   * @param i2sBclkPin I2S Bit Clock pin
   * @param i2sLrcPin I2S LR Clock pin (実際はDATA pinとして使用)
   * @param i2sDinPin I2S Data pin (実際はBCLKとして使用)
   * @return 成功時true
   */
  bool begin(int i2sBclkPin, int i2sLrcPin, int i2sDinPin);

  /**
   * MP3ファイルを再生
   * @param filename ファイル名（絶対パス）
   * @return 成功時true
   */
  bool play(const char* filename);

  /**
   * 再生処理を更新（loop()内で呼び出す）
   * MP3デコード処理を継続する
   */
  void update();

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
  ofxSerialManager& _serialManager;

  int _i2sBclkPin;
  int _i2sLrcPin;
  int _i2sDinPin;

  bool _isPlaying;

  // BackgroundAudioライブラリのコンポーネント
  I2S* _i2s;
  BackgroundAudioMP3* _bmp;

  // SD card file
  File _currentFile;

  // Read buffer (512 bytes for sector-aligned reads)
  static const int BUFFER_SIZE = 512;
  uint8_t _buffer[BUFFER_SIZE];
};
