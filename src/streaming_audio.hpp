#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class AudioStreamFormat {
    Wav,
    Pcm
};

struct AudioStreamPayload {
    std::string contentType;
    std::vector<uint8_t> audioBytes;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    size_t frameBytes = 0;
};

AudioStreamFormat parseAudioStreamFormat(const std::string &formatText);
AudioStreamPayload createAudioStreamPayload(const std::vector<uint8_t> &wavBytes, AudioStreamFormat audioStreamFormat);
std::vector<uint8_t> createPcmWaveHeader(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample, uint64_t pcmByteCount);
std::vector<uint8_t> connectPcmWaveBytes(const std::vector<std::vector<uint8_t>> &wavFiles);
std::vector<uint8_t> mixPcmWaveBytes(const std::vector<uint8_t> &baseWavBytes, const std::vector<uint8_t> &targetWavBytes, double targetRate);
size_t calculateAudioStreamChunkBytes(const AudioStreamPayload &audioStreamPayload, size_t chunkSamples, size_t fallbackChunkBytes);
void writeAudioStreamChunks(const AudioStreamPayload &audioStreamPayload, size_t chunkBytes, const std::function<void(const uint8_t *, size_t)> &writeChunk);
