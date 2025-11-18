/**
 * WavPlayer.cpp
 *
 * WAVファイル再生クラスの実装
 */

#include "WavPlayer.h"

WavPlayer::WavPlayer(ofxSerialManager& serialMgr)
  : _serialManager(serialMgr),
    _i2sBclkPin(-1),
    _i2sLrcPin(-1),
    _i2sDinPin(-1),
    _isPlaying(false) {
}

bool WavPlayer::begin(int i2sBclkPin, int i2sLrcPin, int i2sDinPin) {
  _i2sBclkPin = i2sBclkPin;
  _i2sLrcPin = i2sLrcPin;
  _i2sDinPin = i2sDinPin;

  return true;
}

bool WavPlayer::readWavHeader(File& file, WavHeader& header, DataChunkHeader& dataChunk) {
  // RIFFヘッダー（12バイト）を読み込む
  char riffHeader[4];
  uint32_t fileSize;
  char waveHeader[4];

  file.read((uint8_t*)riffHeader, 4);
  file.read((uint8_t*)&fileSize, 4);
  file.read((uint8_t*)waveHeader, 4);

  // RIFFヘッダー検証
  if (strncmp(riffHeader, "RIFF", 4) != 0) {
    _serialManager.send("error", "Invalid RIFF header");
    return false;
  }

  // WAVEヘッダー検証
  if (strncmp(waveHeader, "WAVE", 4) != 0) {
    _serialManager.send("error", "Invalid WAVE header");
    return false;
  }

  // チャンクを順番に読んで、fmtとdataを探す
  bool fmtFound = false;
  bool dataFound = false;

  while (file.available() && (!fmtFound || !dataFound)) {
    char chunkId[4];
    uint32_t chunkSize;

    // チャンクIDとサイズを読む
    if (file.read((uint8_t*)chunkId, 4) != 4) break;
    if (file.read((uint8_t*)&chunkSize, 4) != 4) break;

    if (strncmp(chunkId, "fmt ", 4) == 0) {
      // fmtチャンクを読み込む
      file.read((uint8_t*)&header.audioFormat, 2);
      file.read((uint8_t*)&header.numChannels, 2);
      file.read((uint8_t*)&header.sampleRate, 4);
      file.read((uint8_t*)&header.byteRate, 4);
      file.read((uint8_t*)&header.blockAlign, 2);
      file.read((uint8_t*)&header.bitsPerSample, 2);

      // fmtチャンクが16バイトより大きい場合は残りをスキップ
      if (chunkSize > 16) {
        file.seek(file.position() + (chunkSize - 16));
      }

      // PCMフォーマット検証
      if (header.audioFormat != 1) {
        _serialManager.send("error", "Only PCM format supported");
        return false;
      }

      fmtFound = true;

    } else if (strncmp(chunkId, "data", 4) == 0) {
      // dataチャンク発見
      dataChunk.dataSize = chunkSize;
      memcpy(dataChunk.dataHeader, chunkId, 4);
      dataFound = true;
      // dataチャンクの位置はここ（読み取りを開始する位置）

    } else {
      // その他のチャンク（JUNK, LIST等）はスキップ
      file.seek(file.position() + chunkSize);
    }
  }

  if (!fmtFound) {
    _serialManager.send("error", "fmt chunk not found");
    return false;
  }

  if (!dataFound) {
    _serialManager.send("error", "data chunk not found");
    return false;
  }

  return true;
}

void WavPlayer::play16BitStereo(File& file, uint32_t dataSize) {
  uint32_t bytesRemaining = dataSize;

  while (bytesRemaining > 0 && file.available() && _isPlaying) {
    int bytesToRead = min(bytesRemaining, (uint32_t)BUFFER_SIZE);
    int bytesRead = file.read(_buffer, bytesToRead);

    if (bytesRead <= 0) break;

    // 16bit stereo: 4バイトずつ処理
    for (int i = 0; i < bytesRead; i += 4) {
      int16_t left = (int16_t)(_buffer[i] | (_buffer[i+1] << 8));
      int16_t right = (int16_t)(_buffer[i+2] | (_buffer[i+3] << 8));
      _i2s.write(left, right);
    }

    bytesRemaining -= bytesRead;

    // シリアル通信を処理
    _serialManager.update();
    yield();
  }
}

void WavPlayer::play16BitMono(File& file, uint32_t dataSize) {
  uint32_t bytesRemaining = dataSize;

  while (bytesRemaining > 0 && file.available() && _isPlaying) {
    int bytesToRead = min(bytesRemaining, (uint32_t)BUFFER_SIZE);
    int bytesRead = file.read(_buffer, bytesToRead);

    if (bytesRead <= 0) break;

    // 16bit mono: 2バイトずつ処理、両チャンネルに同じ音を出力
    for (int i = 0; i < bytesRead; i += 2) {
      int16_t sample = (int16_t)(_buffer[i] | (_buffer[i+1] << 8));
      _i2s.write(sample, sample);
    }

    bytesRemaining -= bytesRead;

    _serialManager.update();
    yield();
  }
}

void WavPlayer::play8BitStereo(File& file, uint32_t dataSize) {
  uint32_t bytesRemaining = dataSize;

  while (bytesRemaining > 0 && file.available() && _isPlaying) {
    int bytesToRead = min(bytesRemaining, (uint32_t)BUFFER_SIZE);
    int bytesRead = file.read(_buffer, bytesToRead);

    if (bytesRead <= 0) break;

    // 8bit stereo: 2バイトずつ処理、unsigned→signed変換
    for (int i = 0; i < bytesRead; i += 2) {
      int16_t left = ((int16_t)_buffer[i] - 128) << 8;
      int16_t right = ((int16_t)_buffer[i+1] - 128) << 8;
      _i2s.write(left, right);
    }

    bytesRemaining -= bytesRead;

    _serialManager.update();
    yield();
  }
}

void WavPlayer::play8BitMono(File& file, uint32_t dataSize) {
  uint32_t bytesRemaining = dataSize;

  while (bytesRemaining > 0 && file.available() && _isPlaying) {
    int bytesToRead = min(bytesRemaining, (uint32_t)BUFFER_SIZE);
    int bytesRead = file.read(_buffer, bytesToRead);

    if (bytesRead <= 0) break;

    // 8bit mono: 1バイトずつ処理、unsigned→signed変換、両チャンネルに出力
    for (int i = 0; i < bytesRead; i++) {
      int16_t sample = ((int16_t)_buffer[i] - 128) << 8;
      _i2s.write(sample, sample);
    }

    bytesRemaining -= bytesRead;

    _serialManager.update();
    yield();
  }
}

bool WavPlayer::play(const char* filename) {
  _currentFile = SD.open(filename);

  if (!_currentFile) {
    char msg[128];
    sprintf(msg, "Failed to open: %s", filename);
    _serialManager.send("error", msg);
    return false;
  }

  WavHeader header;
  DataChunkHeader dataChunk;

  if (!readWavHeader(_currentFile, header, dataChunk)) {
    _currentFile.close();
    return false;
  }

  // WAVファイル情報を表示
  char info[256];
  sprintf(info, "Playing: %s | %dHz, %dch, %dbit",
          filename,
          header.sampleRate,
          header.numChannels,
          header.bitsPerSample);
  _serialManager.send("info", info);

  // I2S設定
  _i2s.setBCLK(_i2sBclkPin);
  _i2s.setDOUT(_i2sDinPin);
  _i2s.setBitsPerSample(header.bitsPerSample);

  if (!_i2s.begin(header.sampleRate)) {
    _serialManager.send("error", "Failed to initialize I2S");
    _currentFile.close();
    return false;
  }

  _isPlaying = true;

  // フォーマットに応じた再生
  if (header.bitsPerSample == 16 && header.numChannels == 2) {
    play16BitStereo(_currentFile, dataChunk.dataSize);
  }
  else if (header.bitsPerSample == 16 && header.numChannels == 1) {
    play16BitMono(_currentFile, dataChunk.dataSize);
  }
  else if (header.bitsPerSample == 8 && header.numChannels == 2) {
    play8BitStereo(_currentFile, dataChunk.dataSize);
  }
  else if (header.bitsPerSample == 8 && header.numChannels == 1) {
    play8BitMono(_currentFile, dataChunk.dataSize);
  }
  else {
    char msg[128];
    sprintf(msg, "Unsupported format: %dbit, %dch", header.bitsPerSample, header.numChannels);
    _serialManager.send("error", msg);
    _currentFile.close();
    _i2s.end();
    _isPlaying = false;
    return false;
  }

  // 再生完了
  _currentFile.close();
  _i2s.end();
  _isPlaying = false;

  char msg[128];
  sprintf(msg, "Finished: %s", filename);
  _serialManager.send("info", msg);

  return true;
}

void WavPlayer::stop() {
  _isPlaying = false;

  if (_currentFile) {
    _currentFile.close();
  }

  _i2s.end();
}
