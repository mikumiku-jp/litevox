#include "streaming_audio.hpp"

#include "utility.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

struct PcmWaveView {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    size_t audioOffset = 0;
    size_t audioBytes = 0;
};

static uint16_t readWaveUint16(const std::vector<uint8_t> &wavBytes, size_t byteOffset) {
    if (byteOffset + 2 > wavBytes.size()) {
        throw std::runtime_error("WAV が壊れています");
    }
    return static_cast<uint16_t>(wavBytes[byteOffset]) |
           static_cast<uint16_t>(wavBytes[byteOffset + 1] << 8);
}

static uint32_t readWaveUint32(const std::vector<uint8_t> &wavBytes, size_t byteOffset) {
    if (byteOffset + 4 > wavBytes.size()) {
        throw std::runtime_error("WAV が壊れています");
    }
    return static_cast<uint32_t>(wavBytes[byteOffset]) |
           (static_cast<uint32_t>(wavBytes[byteOffset + 1]) << 8) |
           (static_cast<uint32_t>(wavBytes[byteOffset + 2]) << 16) |
           (static_cast<uint32_t>(wavBytes[byteOffset + 3]) << 24);
}

static int16_t readWaveInt16(const std::vector<uint8_t> &wavBytes, size_t byteOffset) {
    return static_cast<int16_t>(readWaveUint16(wavBytes, byteOffset));
}

static void appendWaveUint16(std::vector<uint8_t> &wavBytes, uint16_t numberValue) {
    wavBytes.push_back(static_cast<uint8_t>(numberValue & 0xff));
    wavBytes.push_back(static_cast<uint8_t>((numberValue >> 8) & 0xff));
}

static void appendWaveUint32(std::vector<uint8_t> &wavBytes, uint32_t numberValue) {
    wavBytes.push_back(static_cast<uint8_t>(numberValue & 0xff));
    wavBytes.push_back(static_cast<uint8_t>((numberValue >> 8) & 0xff));
    wavBytes.push_back(static_cast<uint8_t>((numberValue >> 16) & 0xff));
    wavBytes.push_back(static_cast<uint8_t>((numberValue >> 24) & 0xff));
}

static void appendWaveTag(std::vector<uint8_t> &wavBytes, const char *tagText) {
    wavBytes.push_back(static_cast<uint8_t>(tagText[0]));
    wavBytes.push_back(static_cast<uint8_t>(tagText[1]));
    wavBytes.push_back(static_cast<uint8_t>(tagText[2]));
    wavBytes.push_back(static_cast<uint8_t>(tagText[3]));
}

static void appendWaveInt16(std::vector<uint8_t> &wavBytes, int16_t sampleValue) {
    appendWaveUint16(wavBytes, static_cast<uint16_t>(sampleValue));
}

static bool matchesWaveTag(const std::vector<uint8_t> &wavBytes, size_t byteOffset, const char *tagText) {
    if (byteOffset + 4 > wavBytes.size()) {
        return false;
    }
    return wavBytes[byteOffset] == static_cast<uint8_t>(tagText[0]) &&
           wavBytes[byteOffset + 1] == static_cast<uint8_t>(tagText[1]) &&
           wavBytes[byteOffset + 2] == static_cast<uint8_t>(tagText[2]) &&
           wavBytes[byteOffset + 3] == static_cast<uint8_t>(tagText[3]);
}

static PcmWaveView parsePcmWaveView(const std::vector<uint8_t> &wavBytes) {
    if (wavBytes.size() < 12 || !matchesWaveTag(wavBytes, 0, "RIFF") || !matchesWaveTag(wavBytes, 8, "WAVE")) {
        throw std::runtime_error("PCM WAV ではありません");
    }
    PcmWaveView pcmWaveView;
    bool hasFormatChunk = false;
    bool hasAudioChunk = false;
    size_t chunkOffset = 12;
    while (chunkOffset + 8 <= wavBytes.size()) {
        uint32_t chunkBytes = readWaveUint32(wavBytes, chunkOffset + 4);
        size_t chunkBodyOffset = chunkOffset + 8;
        size_t nextChunkOffset = chunkBodyOffset + chunkBytes + (chunkBytes % 2);
        if (nextChunkOffset > wavBytes.size()) {
            throw std::runtime_error("WAV chunk が壊れています");
        }
        if (matchesWaveTag(wavBytes, chunkOffset, "fmt ")) {
            if (chunkBytes < 16) {
                throw std::runtime_error("WAV fmt chunk が壊れています");
            }
            uint16_t audioFormat = readWaveUint16(wavBytes, chunkBodyOffset);
            if (audioFormat != 1) {
                throw std::runtime_error("PCM WAV ではありません");
            }
            pcmWaveView.channels = readWaveUint16(wavBytes, chunkBodyOffset + 2);
            pcmWaveView.sampleRate = readWaveUint32(wavBytes, chunkBodyOffset + 4);
            pcmWaveView.bitsPerSample = readWaveUint16(wavBytes, chunkBodyOffset + 14);
            hasFormatChunk = true;
        } else if (matchesWaveTag(wavBytes, chunkOffset, "data")) {
            pcmWaveView.audioOffset = chunkBodyOffset;
            pcmWaveView.audioBytes = chunkBytes;
            hasAudioChunk = true;
        }
        chunkOffset = nextChunkOffset;
    }
    if (!hasFormatChunk || !hasAudioChunk || pcmWaveView.channels == 0 || pcmWaveView.sampleRate == 0 || pcmWaveView.bitsPerSample == 0) {
        throw std::runtime_error("WAV の PCM 情報を読めません");
    }
    return pcmWaveView;
}

AudioStreamFormat parseAudioStreamFormat(const std::string &formatText) {
    std::string normalizedText = lowercaseAscii(formatText);
    if (normalizedText.empty() || normalizedText == "wav") {
        return AudioStreamFormat::Wav;
    }
    if (normalizedText == "pcm" || normalizedText == "raw" || normalizedText == "raw-pcm") {
        return AudioStreamFormat::Pcm;
    }
    throw std::runtime_error("format が不正です: " + formatText);
}

AudioStreamPayload createAudioStreamPayload(const std::vector<uint8_t> &wavBytes, AudioStreamFormat audioStreamFormat) {
    AudioStreamPayload audioStreamPayload;
    PcmWaveView pcmWaveView = parsePcmWaveView(wavBytes);
    audioStreamPayload.sampleRate = pcmWaveView.sampleRate;
    audioStreamPayload.channels = pcmWaveView.channels;
    audioStreamPayload.bitsPerSample = pcmWaveView.bitsPerSample;
    audioStreamPayload.frameBytes = static_cast<size_t>(pcmWaveView.channels) * (static_cast<size_t>(pcmWaveView.bitsPerSample) / 8);
    if (audioStreamFormat == AudioStreamFormat::Wav) {
        audioStreamPayload.contentType = "audio/wav";
        audioStreamPayload.audioBytes = wavBytes;
        return audioStreamPayload;
    }
    audioStreamPayload.contentType = "audio/L16; rate=" + std::to_string(pcmWaveView.sampleRate) + "; channels=" + std::to_string(pcmWaveView.channels);
    audioStreamPayload.audioBytes.assign(wavBytes.begin() + static_cast<std::vector<uint8_t>::difference_type>(pcmWaveView.audioOffset), wavBytes.begin() + static_cast<std::vector<uint8_t>::difference_type>(pcmWaveView.audioOffset + pcmWaveView.audioBytes));
    return audioStreamPayload;
}

std::vector<uint8_t> createPcmWaveHeader(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample, uint64_t pcmByteCount) {
    if (pcmByteCount > std::numeric_limits<uint32_t>::max() - 44) {
        throw std::runtime_error("WAV が大きすぎます");
    }
    uint16_t frameBytes = static_cast<uint16_t>(channels * (bitsPerSample / 8));
    uint32_t byteRate = sampleRate * frameBytes;
    uint32_t pcmBytes = static_cast<uint32_t>(pcmByteCount);
    std::vector<uint8_t> wavHeader;
    wavHeader.reserve(44);
    appendWaveTag(wavHeader, "RIFF");
    appendWaveUint32(wavHeader, 36 + pcmBytes);
    appendWaveTag(wavHeader, "WAVE");
    appendWaveTag(wavHeader, "fmt " );
    appendWaveUint32(wavHeader, 16);
    appendWaveUint16(wavHeader, 1);
    appendWaveUint16(wavHeader, channels);
    appendWaveUint32(wavHeader, sampleRate);
    appendWaveUint32(wavHeader, byteRate);
    appendWaveUint16(wavHeader, frameBytes);
    appendWaveUint16(wavHeader, bitsPerSample);
    appendWaveTag(wavHeader, "data");
    appendWaveUint32(wavHeader, pcmBytes);
    return wavHeader;
}

std::vector<uint8_t> connectPcmWaveBytes(const std::vector<std::vector<uint8_t>> &wavFiles) {
    if (wavFiles.empty()) {
        throw std::runtime_error("WAV がありません");
    }
    PcmWaveView firstWaveView = parsePcmWaveView(wavFiles.front());
    uint64_t totalPcmBytes = 0;
    for (const std::vector<uint8_t> &wavBytes : wavFiles) {
        PcmWaveView waveView = parsePcmWaveView(wavBytes);
        if (waveView.sampleRate != firstWaveView.sampleRate || waveView.channels != firstWaveView.channels || waveView.bitsPerSample != firstWaveView.bitsPerSample) {
            throw std::runtime_error("WAV 形式が一致しません");
        }
        totalPcmBytes += waveView.audioBytes;
    }
    std::vector<uint8_t> connectedWavBytes = createPcmWaveHeader(firstWaveView.sampleRate, firstWaveView.channels, firstWaveView.bitsPerSample, totalPcmBytes);
    for (const std::vector<uint8_t> &wavBytes : wavFiles) {
        PcmWaveView waveView = parsePcmWaveView(wavBytes);
        connectedWavBytes.insert(connectedWavBytes.end(), wavBytes.begin() + static_cast<std::vector<uint8_t>::difference_type>(waveView.audioOffset), wavBytes.begin() + static_cast<std::vector<uint8_t>::difference_type>(waveView.audioOffset + waveView.audioBytes));
    }
    return connectedWavBytes;
}

std::vector<uint8_t> mixPcmWaveBytes(const std::vector<uint8_t> &baseWavBytes, const std::vector<uint8_t> &targetWavBytes, double targetRate) {
    PcmWaveView baseWaveView = parsePcmWaveView(baseWavBytes);
    PcmWaveView targetWaveView = parsePcmWaveView(targetWavBytes);
    if (baseWaveView.sampleRate != targetWaveView.sampleRate || baseWaveView.channels != targetWaveView.channels || baseWaveView.bitsPerSample != targetWaveView.bitsPerSample) {
        throw std::runtime_error("WAV 形式が一致しません");
    }
    if (baseWaveView.bitsPerSample != 16) {
        throw std::runtime_error("16bit PCM WAV だけ対応しています");
    }
    double safeTargetRate = std::min(1.0, std::max(0.0, targetRate));
    double baseRate = 1.0 - safeTargetRate;
    size_t baseSampleCount = baseWaveView.audioBytes / 2;
    size_t targetSampleCount = targetWaveView.audioBytes / 2;
    size_t morphingPaddingFrames = 8;
    size_t mixedSampleCount = std::max(baseSampleCount, targetSampleCount) + morphingPaddingFrames * baseWaveView.channels;
    std::vector<uint8_t> mixedPcmBytes;
    mixedPcmBytes.reserve(mixedSampleCount * 2);
    for (size_t sampleIndex = 0; sampleIndex < mixedSampleCount; sampleIndex++) {
        int16_t baseSample = 0;
        int16_t targetSample = 0;
        if (sampleIndex < baseSampleCount) {
            baseSample = readWaveInt16(baseWavBytes, baseWaveView.audioOffset + sampleIndex * 2);
        }
        if (sampleIndex < targetSampleCount) {
            targetSample = readWaveInt16(targetWavBytes, targetWaveView.audioOffset + sampleIndex * 2);
        }
        int mixedSample = static_cast<int>(baseSample * baseRate + targetSample * safeTargetRate);
        mixedSample = std::max(-32768, std::min(32767, mixedSample));
        appendWaveInt16(mixedPcmBytes, static_cast<int16_t>(mixedSample));
    }
    uint32_t pcmByteCount = static_cast<uint32_t>(mixedPcmBytes.size());
    uint16_t frameBytes = static_cast<uint16_t>(baseWaveView.channels * (baseWaveView.bitsPerSample / 8));
    uint32_t byteRate = baseWaveView.sampleRate * frameBytes;
    std::vector<uint8_t> mixedWavBytes;
    mixedWavBytes.reserve(44 + mixedPcmBytes.size());
    appendWaveTag(mixedWavBytes, "RIFF");
    appendWaveUint32(mixedWavBytes, 36 + pcmByteCount);
    appendWaveTag(mixedWavBytes, "WAVE");
    appendWaveTag(mixedWavBytes, "fmt ");
    appendWaveUint32(mixedWavBytes, 16);
    appendWaveUint16(mixedWavBytes, 1);
    appendWaveUint16(mixedWavBytes, baseWaveView.channels);
    appendWaveUint32(mixedWavBytes, baseWaveView.sampleRate);
    appendWaveUint32(mixedWavBytes, byteRate);
    appendWaveUint16(mixedWavBytes, frameBytes);
    appendWaveUint16(mixedWavBytes, baseWaveView.bitsPerSample);
    appendWaveTag(mixedWavBytes, "data");
    appendWaveUint32(mixedWavBytes, pcmByteCount);
    mixedWavBytes.insert(mixedWavBytes.end(), mixedPcmBytes.begin(), mixedPcmBytes.end());
    return mixedWavBytes;
}

size_t calculateAudioStreamChunkBytes(const AudioStreamPayload &audioStreamPayload, size_t chunkSamples, size_t fallbackChunkBytes) {
    if (chunkSamples > 0 && audioStreamPayload.frameBytes > 0) {
        return std::max<size_t>(audioStreamPayload.frameBytes, chunkSamples * audioStreamPayload.frameBytes);
    }
    return std::max<size_t>(1, fallbackChunkBytes);
}

void writeAudioStreamChunks(const AudioStreamPayload &audioStreamPayload, size_t chunkBytes, const std::function<void(const uint8_t *, size_t)> &writeChunk) {
    size_t safeChunkBytes = std::max<size_t>(1, chunkBytes);
    size_t byteOffset = 0;
    while (byteOffset < audioStreamPayload.audioBytes.size()) {
        size_t remainingBytes = audioStreamPayload.audioBytes.size() - byteOffset;
        size_t currentChunkBytes = std::min(remainingBytes, safeChunkBytes);
        writeChunk(audioStreamPayload.audioBytes.data() + byteOffset, currentChunkBytes);
        byteOffset += currentChunkBytes;
    }
}
