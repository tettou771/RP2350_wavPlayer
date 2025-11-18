/**
 * Mp3Player.cpp
 *
 * MP3ファイル再生クラスの実装
 * BackgroundAudioライブラリを使用
 */

#include "Mp3Player.h"

Mp3Player::Mp3Player(ofxSerialManager& serialMgr)
  : _serialManager(serialMgr),
    _i2sBclkPin(-1),
    _i2sLrcPin(-1),
    _i2sDinPin(-1),
    _isPlaying(false),
    _i2s(nullptr),
    _bmp(nullptr) {
}

Mp3Player::~Mp3Player() {
  stop();
  if (_bmp) {
    delete _bmp;
  }
  if (_i2s) {
    delete _i2s;
  }
}

bool Mp3Player::begin(int i2sBclkPin, int i2sLrcPin, int i2sDinPin) {
  _i2sBclkPin = i2sBclkPin;
  _i2sLrcPin = i2sLrcPin;
  _i2sDinPin = i2sDinPin;

  // I2SとBackgroundAudioはplay()が呼ばれたときに作成する
  // これでWavPlayerとの競合を避ける

  _serialManager.send("info", "MP3 Player initialized");
  return true;
}

bool Mp3Player::play(const char* filename) {
  // 既存の再生を停止
  stop();

  // I2SとBackgroundAudioを再作成（stop()で削除された場合）
  if (!_i2s) {
    _i2s = new I2S(OUTPUT);
    _i2s->setBCLK(_i2sBclkPin);    // GP11
    _i2s->setDATA(_i2sDinPin);     // GP10
    // LRCは自動的にBCLK+1 (GP12) に設定される
  }

  if (!_bmp) {
    _bmp = new BackgroundAudioMP3(*_i2s);
    if (!_bmp->begin()) {
      _serialManager.send("error", "Failed to initialize BackgroundAudioMP3");
      return false;
    }
  }

  // ファイルを開く
  _currentFile = SD.open(filename);
  if (!_currentFile) {
    char msg[128];
    sprintf(msg, "Failed to open MP3: %s", filename);
    _serialManager.send("error", msg);
    return false;
  }

  _isPlaying = true;

  char msg[128];
  sprintf(msg, "Playing MP3: %s", filename);
  _serialManager.send("info", msg);

  return true;
}

void Mp3Player::update() {
  if (!_isPlaying || !_currentFile || !_bmp) {
    return;
  }

  // BackgroundAudioMP3のバッファに空きがあれば、データを読み込んで送信
  // 512バイトずつ読み込み（SDカードのセクタサイズに合わせる）
  while (_currentFile.available() && _bmp->availableForWrite() > BUFFER_SIZE) {
    int bytesRead = _currentFile.read(_buffer, BUFFER_SIZE);

    if (bytesRead > 0) {
      _bmp->write(_buffer, bytesRead);
    }

    // 短い読み取り = EOF
    if (bytesRead < BUFFER_SIZE) {
      // 再生完了
      _currentFile.close();
      _isPlaying = false;
      _serialManager.send("info", "MP3 playback finished");
      break;
    }
  }

  // シリアル通信を処理
  _serialManager.update();
}

void Mp3Player::stop() {
  _isPlaying = false;

  if (_currentFile) {
    _currentFile.close();
  }

  // BackgroundAudioとI2Sを解放（WavPlayerと切り替えるため）
  if (_bmp) {
    delete _bmp;
    _bmp = nullptr;
  }

  if (_i2s) {
    delete _i2s;
    _i2s = nullptr;
  }
}
