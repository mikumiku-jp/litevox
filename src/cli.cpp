#include "cli.hpp"

#include "archive_extract.hpp"
#include "http_server.hpp"
#include "json_utility.hpp"
#include "model_asset.hpp"
#include "model_metadata.hpp"
#include "model_session_cache.hpp"
#include "native_onnx.hpp"
#include "ort_inspect.hpp"
#include "runtime.hpp"
#include "runtime_trace.hpp"
#include "socket_compat.hpp"
#include "streaming_audio.hpp"
#include "utility.hpp"
#include "vvm_archive.hpp"
#include "vv_bin_inspect.hpp"
#include "api_schema.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

enum class CommandMode {
    Help,
    Server,
    Tts,
    Synthesis,
    Stream,
    Query,
    SingQuery,
    SingF0,
    SingVolume,
    FrameSynthesis,
    Models,
    Inspect,
    Inventory,
    Assets,
    ModelDump,
    VvBin,
    OrtInspect,
    NativeOnnx,
    OnnxRandomSeed,
    VvBinOrt,
    VvBinModels,
    VvBinOperators,
    VvBinExportOnnx,
    VvBinCompareOnnx,
    VvBinChain,
    VvBinSynth,
    Cache,
    Extract,
    Validate,
    Speakers,
    Deps,
    Devices,
    Version,
    RuntimeInfo,
    Trace,
    Bench,
    BenchSong,
    BenchHttp,
    BenchHttpSong,
    ApiSession,
    OpenApi
};

struct CliOptions {
    CommandMode commandMode = CommandMode::Server;
    RuntimePaths runtimePaths;
    bool hasExplicitModels = false;
    bool hasExplicitOnnxruntimePath = false;
    bool shouldPreload = false;
    bool shouldScanModels = false;
    bool shouldExtractOnnx = false;
    bool shouldExtractResources = false;
    bool shouldExtractEngineAssets = false;
    bool shouldExtractOnnxruntime = false;
    bool shouldExtractRuntime = false;
    bool shouldFullScan = false;
    bool isKana = false;
    bool httpKeepAlive = false;
    int port = 50021;
    size_t workers = 1;
    uint32_t speaker = 3;
    std::vector<uint32_t> benchSpeakers;
    std::vector<std::string> benchTexts;
    std::vector<fs::path> benchScorePaths;
    std::vector<fs::path> benchFrameAudioQueryPaths;
    size_t runs = 1;
    size_t chunkSamples = 1024;
    size_t previewBytes = 64;
    int64_t onnxSeed = 0;
    bool hasOnnxSeed = false;
    std::string nativeSingTeacherMode;
    std::string text;
    std::string httpHost = "127.0.0.1";
    std::string httpPath = "/tts";
    std::string modelAssetName;
    std::vector<std::string> benchHttpPaths;
    AudioStreamFormat audioStreamFormat = AudioStreamFormat::Wav;
    fs::path inspectPath;
    fs::path outputPath = "out.wav";
    fs::path onnxOutputDirectory;
    fs::path compareOnnxruntimeLibraryPath;
    fs::path autoDiscoveredOnnxruntimeLibraryPath;
    fs::path resourceOutputDirectory;
    fs::path engineAssetOutputDirectory;
    fs::path onnxruntimeOutputDirectory;
    fs::path runtimeOutputDirectory;
    fs::path nativeOnnxInputDirectory;
    fs::path audioQueryPath;
    fs::path scorePath;
    fs::path frameAudioQueryPath;
};

static void printUsage() {
    std::cout
        << "LiteVox\n"
        << "\n"
        << "usage:\n"
        << "  litevox help\n"
        << "  litevox models [--runtime DIR] [--models PATH]\n"
        << "  litevox styles [--runtime DIR] [--models PATH]\n"
        << "  litevox inspect MODEL.vvm\n"
        << "  litevox inventory MODEL.vvm\n"
        << "  litevox assets MODEL.vvm\n"
        << "  litevox model-dump MODEL.vvm|VOICEVOX.zip\n"
        << "  litevox model-dump MODEL.vvm|VOICEVOX.zip --scan-models\n"
        << "  litevox model-dump MODEL.vvm|VOICEVOX.zip --extract-onnx DIR\n"
        << "  litevox model-dump VOICEVOX.zip --extract-resources DIR\n"
        << "  litevox model-dump VOICEVOX.zip --extract-engine-assets DIR\n"
        << "  litevox model-dump VOICEVOX.zip --extract-runtime DIR\n"
        << "  litevox model-dump ONNXRUNTIME.tgz|onnxruntime.zip --extract-onnxruntime DIR\n"
        << "  litevox vv-bin MODEL.vvm|VOICEVOX.zip\n"
        << "  litevox vv-bin-ort MODEL.vvm|VOICEVOX.zip [--onnxruntime PATH] [--inputs TRACE_TENSORS_DIR]\n"
        << "  litevox vv-bin-models MODEL.vvm|VOICEVOX.zip [--onnxruntime PATH] [--cpu-threads N]\n"
        << "  litevox vv-bin-operators MODEL.vvm|VOICEVOX.zip [--onnxruntime PATH] [--cpu-threads N]\n"
        << "  litevox vv-bin-export-onnx MODEL.vvm|VOICEVOX.zip --extract-onnx DIR [--seed 1] [--onnxruntime PATH] [--cpu-threads N]\n"
        << "  litevox vv-bin-compare-onnx MODEL.vvm|VOICEVOX.zip [--inputs TRACE_TENSORS_DIR] [--asset models/pd.bin] [--runs 5] [--onnxruntime PATH] [--compare-onnxruntime PATH] [--cpu-threads N]\n"
        << "  litevox vv-bin-chain MODEL.vvm|VOICEVOX.zip --audio-query audio_query.json [--inputs TRACE_TENSORS_DIR] [--speaker STYLE_ID] [--onnxruntime PATH]\n"
        << "  litevox vv-bin-synth MODEL.vvm|VOICEVOX.zip --audio-query audio_query.json --out out.wav [--speaker STYLE_ID]\n"
        << "  litevox ort-inspect [--onnxruntime PATH]\n"
        << "  litevox native-onnx [MODEL.onnx] [--onnxruntime PATH] [--inputs TRACE_TENSORS_DIR] [--cpu-threads N]\n"
        << "  litevox onnx-random-seed MODEL.onnx --seed 1 --out patched.onnx\n"
        << "  litevox cache MODEL.vvm\n"
        << "  litevox extract MODEL.vvm --out DIR\n"
        << "  litevox validate MODEL.vvm\n"
        << "  litevox tts --speaker STYLE_ID --text TEXT --out out.wav\n"
        << "  litevox synthesis --speaker STYLE_ID --audio-query audio_query.json --out out.wav\n"
        << "  litevox stream --speaker STYLE_ID --text TEXT [--format wav|pcm] [--chunk-samples 1024] > out.wav\n"
        << "  litevox query --speaker STYLE_ID --text TEXT\n"
        << "  litevox sing-query --score score.json [--speaker 6000] [--out frame_audio_query.json]\n"
        << "  litevox sing-f0 --score score.json --frame-audio-query frame_audio_query.json [--speaker 6000]\n"
        << "  litevox sing-volume --score score.json --frame-audio-query frame_audio_query.json [--speaker 6000]\n"
        << "  litevox frame-synthesis --frame-audio-query frame_audio_query.json [--speaker 3000] --out out.wav\n"
        << "  litevox server [--port 50021] [--workers 4] [--acceleration-mode auto|cpu|gpu] [--cpu-threads N] [--enable_cancellable_synthesis] [--preload]\n"
        << "  litevox speakers\n"
        << "  litevox deps\n"
        << "  litevox devices\n"
        << "  litevox version\n"
        << "  litevox runtime_info\n"
        << "  litevox trace --speaker STYLE_ID --text TEXT --out DIR\n"
        << "  litevox openapi\n"
        << "  litevox bench --speaker STYLE_ID --text TEXT [--runs 3] [--workers 4] [--cpu-threads N]\n"
        << "  litevox bench --speakers 3,23,54,58 --text TEXT [--runs 8] [--workers 4]\n"
        << "  litevox bench-song --score score.json [--add-score score2.json] [--frame-audio-query frame_audio_query.json] [--add-frame-audio-query frame_audio_query2.json] [--speaker 6000] [--runs 5] [--workers 4] [--native-sing-teacher deterministic|vv-bin]\n"
        << "  litevox bench-http --speaker STYLE_ID --text TEXT [--port 50021] [--runs 8] [--workers 4]\n"
        << "  litevox bench-http --speakers 3,23,54,58 --text TEXT [--port 50021] [--runs 8] [--workers 4]\n"
        << "  litevox bench-http --http-path /tts --add-http-path /tts_stream --text 短文 --add-text 長文 [--runs 8] [--workers 4]\n"
        << "  litevox bench-http-song --score score.json [--add-score score2.json] [--frame-audio-query frame_audio_query.json] [--add-frame-audio-query frame_audio_query2.json] [--speaker 6000] [--port 50021] [--runs 5] [--workers 4] [--keep-alive]\n"
        << "  litevox api-session --speaker STYLE_ID [--host 127.0.0.1] [--port 50021] [--http-path /tts] [--format wav|pcm] [--out DIR] < texts.txt\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /sing_frame_audio_query --score score.json [--out DIR]\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /sing_frame_f0 --score score.json --frame-audio-query frame_audio_query.json [--out DIR]\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /sing_frame_f0 --score score.json --add-score score2.json --frame-audio-query frame_audio_query.json --add-frame-audio-query frame_audio_query2.json [--out DIR]\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /sing_frame_f0 --score score.json [--out DIR]\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /frame_synthesis --frame-audio-query frame_audio_query.json [--out DIR]\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /frame_synthesis --frame-audio-query frame_audio_query.json --add-frame-audio-query frame_audio_query2.json [--out DIR]\n"
        << "  litevox api-session --speaker 6000 [--host 127.0.0.1] [--port 50021] --http-path /frame_synthesis --score score.json [--out DIR]\n"
        << "  printf '@score.json\\t@frame_audio_query.json\\n' | litevox api-session --speaker 6000 --http-path /sing_frame_f0 [--host 127.0.0.1] [--port 50021] [--out DIR]\n"
        << "\n"
        << "common options:\n"
        << "  --runtime DIR|VOICEVOX.zip  runtime root or VOICEVOX product zip\n"
        << "  --core PATH         libvoicevox_core compatible library path\n"
        << "  --onnxruntime PATH  ONNX Runtime library path; omitted の場合は runtime 配下の libonnxruntime*.dylib または onnxruntime.dll を自動検出\n"
        << "  --compare-onnxruntime PATH  alternate ONNX Runtime library for exported ONNX compare\n"
        << "  --dict DIR          OpenJTalk dictionary directory\n"
        << "  --models PATH       VVM file, VOICEVOX zip, or directory; repeatable; replaces default model-vvm\n"
        << "  --add-model PATH    VVM file, VOICEVOX zip, or directory; repeatable; keeps default model-vvm\n"
        << "  --manifest PATH     engine manifest json path\n"
        << "  --state-dir DIR     writable state directory for user_dict.json, presets.json, setting.json, core_libraries\n"
        << "  --user-dict PATH    user dictionary json path\n"
        << "  --setting PATH      setting json path\n"
        << "  --presets PATH      presets json path\n"
        << "  --library-dir DIR   installed voice library directory\n"
        << "  --backend MODE      native, minimal-ort, voicevox-core, or core-fork; default native\n"
        << "  --core-profile NAME auto, talk-only, or full\n"
        << "  --acceleration-mode auto, cpu, or gpu\n"
        << "  --gpu               shorthand for --acceleration-mode gpu\n"
        << "  --cpu-threads N     CPU threads for synthesis; 0 uses core default\n"
        << "  --seed N            set ONNX Runtime random seed when supported\n"
        << "  --native-model-mode MODE  auto, vv-bin, or exported-onnx\n"
        << "  --native-sing-teacher MODE  deterministic or vv-bin\n"
        << "  --host HOST         HTTP bench host\n"
        << "  --http-path PATH    HTTP bench path, default /tts\n"
        << "  --add-http-path PATH  add another HTTP bench path\n"
        << "  --keep-alive        reuse one HTTP connection in bench-http\n"
        << "  --speaker ID        style ID from `litevox models`\n"
        << "  --speakers IDS      comma-separated style IDs for bench/bench-http\n"
        << "  --text TEXT         Japanese text\n"
        << "  --add-text TEXT     add another text for bench/bench-http\n"
        << "  --kana              treat --text as AquesTalk-style kana\n"
        << "  --format wav|pcm    stream output format\n"
        << "  --chunk-samples N   stream chunk size in PCM frames\n"
        << "  --preview-bytes N   model-dump/vv-bin asset preview byte count\n"
        << "  --full-scan         scan vv-bin assets fully for entropy and ONNX markers\n"
        << "  --scan-models       scan full model blobs for ONNX markers\n"
        << "  --extract-onnx DIR  export manifest models whose type is onnx\n"
        << "  --extract-resources DIR  export VOICEVOX character_info resources\n"
        << "  --extract-engine-assets DIR  export VOICEVOX engine manifest assets\n"
        << "  --extract-runtime DIR  assemble runnable runtime root from VOICEVOX zip, optional standard ONNX Runtime archive, and current litevox dist\n"
        << "  --extract-onnxruntime DIR  export standard ONNX Runtime archive into runtime root\n"
        << "  --inputs DIR        native-onnx/vv-bin-ort input or vv-bin-chain compare tensor directory\n"
        << "  --asset NAME        single model asset name such as models/pd.bin\n"
        << "  --audio-query PATH  audio_query json for synthesis/vv-bin-chain\n"
        << "  --score PATH        score json for song commands\n"
        << "  --add-score PATH    add another score json for song bench commands\n"
        << "  --frame-audio-query PATH  frame audio query json for song commands\n"
        << "  --add-frame-audio-query PATH  add another frame audio query json for song bench commands\n"
        << "  --workers N         server worker count or bench parallel runtime count\n"
        << "  --enable_cancellable_synthesis  enable /cancellable_synthesis endpoint\n"
        << "  --preload           preload models at runtime startup\n"
        << "  --out PATH          output wav path\n";
}

struct RuntimeHolder {
    RuntimeState *runtimeState = nullptr;
    explicit RuntimeHolder(RuntimeState *createdRuntimeState) : runtimeState(createdRuntimeState) {}
    RuntimeHolder(const RuntimeHolder &) = delete;
    RuntimeHolder &operator=(const RuntimeHolder &) = delete;
    ~RuntimeHolder() = default;
};

static fs::path resolveRuntimeRootPath(const fs::path &runtimeInputPath);

static void replaceModelPaths(CliOptions &cliOptions, const fs::path &modelPath) {
    if (!cliOptions.hasExplicitModels) {
        cliOptions.runtimePaths.modelPaths.clear();
        cliOptions.hasExplicitModels = true;
    }
    cliOptions.runtimePaths.modelPaths.push_back(modelPath);
}

static void applyStateDirectory(CliOptions &cliOptions, const fs::path &stateDirectory) {
    cliOptions.runtimePaths.userDictPath = stateDirectory / "user_dict.json";
    cliOptions.runtimePaths.presetPath = stateDirectory / "presets.json";
    cliOptions.runtimePaths.settingPath = stateDirectory / "setting.json";
    cliOptions.runtimePaths.libraryDirectory = stateDirectory / "core_libraries";
}

static std::vector<uint32_t> parseSpeakerList(const std::string &speakerListText) {
    std::vector<uint32_t> speakerIds;
    std::stringstream speakerStream(speakerListText);
    std::string speakerToken;
    while (std::getline(speakerStream, speakerToken, ',')) {
        if (speakerToken.empty()) {
            throw std::runtime_error("--speakers に空の要素があります");
        }
        speakerIds.push_back(static_cast<uint32_t>(std::stoul(speakerToken)));
    }
    if (speakerIds.empty()) {
        throw std::runtime_error("--speakers が空です");
    }
    return speakerIds;
}

static void setPrimaryBenchText(CliOptions &cliOptions, const std::string &textValue) {
    cliOptions.text = textValue;
    if (cliOptions.commandMode == CommandMode::Bench || cliOptions.commandMode == CommandMode::BenchHttp) {
        if (cliOptions.benchTexts.empty()) {
            cliOptions.benchTexts.push_back(textValue);
        } else {
            cliOptions.benchTexts.front() = textValue;
        }
    }
}

static void addBenchText(CliOptions &cliOptions, const std::string &textValue) {
    if (cliOptions.commandMode != CommandMode::Bench && cliOptions.commandMode != CommandMode::BenchHttp) {
        cliOptions.text = textValue;
        return;
    }
    if (cliOptions.benchTexts.empty()) {
        cliOptions.benchTexts.push_back(textValue);
        if (cliOptions.text.empty()) {
            cliOptions.text = textValue;
        }
        return;
    }
    cliOptions.benchTexts.push_back(textValue);
}

static void setPrimaryBenchHttpPath(CliOptions &cliOptions, const std::string &httpPath) {
    cliOptions.httpPath = httpPath;
    if (cliOptions.commandMode == CommandMode::BenchHttp) {
        if (cliOptions.benchHttpPaths.empty()) {
            cliOptions.benchHttpPaths.push_back(httpPath);
        } else {
            cliOptions.benchHttpPaths.front() = httpPath;
        }
    }
}

static void addBenchHttpPath(CliOptions &cliOptions, const std::string &httpPath) {
    if (cliOptions.commandMode != CommandMode::BenchHttp) {
        cliOptions.httpPath = httpPath;
        return;
    }
    if (cliOptions.benchHttpPaths.empty()) {
        cliOptions.benchHttpPaths.push_back(httpPath);
        if (cliOptions.httpPath.empty()) {
            cliOptions.httpPath = httpPath;
        }
        return;
    }
    cliOptions.benchHttpPaths.push_back(httpPath);
}

static bool isStandardOnnxruntimeLibraryPath(const fs::path &libraryPath) {
    std::string filenameText = lowercaseAscii(libraryPath.filename().string());
    std::string extensionText = lowercaseAscii(libraryPath.extension().string());
    if (extensionText == ".dylib") {
        return filenameText.rfind("libonnxruntime", 0) == 0;
    }
    if (extensionText == ".dll") {
        return filenameText == "onnxruntime.dll";
    }
    return false;
}

static fs::path findStandardOnnxruntimeLibraryPath(const fs::path &rootDirectory) {
    if (rootDirectory.empty() || !fs::exists(rootDirectory)) {
        return {};
    }
    std::vector<fs::path> candidatePaths;
    std::error_code iterationError;
    fs::recursive_directory_iterator iterator(rootDirectory, fs::directory_options::skip_permission_denied, iterationError);
    fs::recursive_directory_iterator endIterator;
    for (; iterator != endIterator; iterator.increment(iterationError)) {
        if (iterationError) {
            iterationError.clear();
            continue;
        }
        const fs::directory_entry &directoryEntry = *iterator;
        std::error_code statusError;
        if (!directoryEntry.is_regular_file(statusError) || statusError) {
            continue;
        }
        if (isStandardOnnxruntimeLibraryPath(directoryEntry.path())) {
            candidatePaths.push_back(directoryEntry.path());
        }
    }
    if (candidatePaths.empty()) {
        return {};
    }
    std::sort(candidatePaths.begin(), candidatePaths.end(), [](const fs::path &leftPath, const fs::path &rightPath) {
        size_t leftLength = leftPath.string().size();
        size_t rightLength = rightPath.string().size();
        if (leftLength != rightLength) {
            return leftLength < rightLength;
        }
        return leftPath.string() < rightPath.string();
    });
    return candidatePaths.front();
}

static bool hasUsableGpuProviderAtOnnxruntimePath(const fs::path &onnxruntimeLibraryPath) {
    if (onnxruntimeLibraryPath.empty()) {
        return false;
    }
    try {
        NativeOnnxRuntimeState runtimeState = createNativeOnnxRuntimeState(onnxruntimeLibraryPath, "auto");
        bool hasUsableGpuProvider = runtimeState.hasUsableGpuProvider;
        destroyNativeOnnxRuntimeState(runtimeState);
        return hasUsableGpuProvider;
    } catch (...) {
        return false;
    }
}

static bool shouldAutoDiscoverStandardOnnxruntime(const CliOptions &cliOptions) {
    std::string backendMode = lowercaseAscii(cliOptions.runtimePaths.backendMode);
    if (backendMode == "minimal-ort" || backendMode == "minimal_ort") {
        return true;
    }
    if (backendMode == "native" || backendMode == "vvm-native" || backendMode == "vvm_native") {
        std::string accelerationMode = lowercaseAscii(cliOptions.runtimePaths.accelerationMode);
        return accelerationMode == "gpu" || accelerationMode == "auto";
    }
    return false;
}

static void resolveAutoDiscoveredOnnxruntimePath(CliOptions &cliOptions) {
    if (cliOptions.hasExplicitOnnxruntimePath) {
        return;
    }
    if (!shouldAutoDiscoverStandardOnnxruntime(cliOptions)) {
        return;
    }
    fs::path discoveredOnnxruntimePath = findStandardOnnxruntimeLibraryPath(cliOptions.runtimePaths.rootDirectory);
    if (discoveredOnnxruntimePath.empty()) {
        return;
    }
    std::string backendMode = lowercaseAscii(cliOptions.runtimePaths.backendMode);
    std::string accelerationMode = lowercaseAscii(cliOptions.runtimePaths.accelerationMode);
    if ((backendMode == "native" || backendMode == "vvm-native" || backendMode == "vvm_native") && accelerationMode == "auto" && !hasUsableGpuProviderAtOnnxruntimePath(discoveredOnnxruntimePath)) {
        return;
    }
    cliOptions.runtimePaths.onnxruntimeLibraryPath = discoveredOnnxruntimePath;
    cliOptions.autoDiscoveredOnnxruntimeLibraryPath = discoveredOnnxruntimePath;
}

static CliOptions parseCliOptions(int argc, char **argv) {
    CliOptions cliOptions;
    cliOptions.runtimePaths = makeDefaultRuntimePaths(getExecutableDirectory());
    int argumentIndex = 1;
    if (argumentIndex < argc) {
        std::string commandText = argv[argumentIndex];
        if (commandText == "help") {
            cliOptions.commandMode = CommandMode::Help;
            argumentIndex++;
        } else if (commandText == "server") {
            cliOptions.commandMode = CommandMode::Server;
            argumentIndex++;
        } else if (commandText == "tts") {
            cliOptions.commandMode = CommandMode::Tts;
            argumentIndex++;
        } else if (commandText == "synthesis" || commandText == "synth") {
            cliOptions.commandMode = CommandMode::Synthesis;
            argumentIndex++;
        } else if (commandText == "stream") {
            cliOptions.commandMode = CommandMode::Stream;
            cliOptions.outputPath = "-";
            argumentIndex++;
        } else if (commandText == "query") {
            cliOptions.commandMode = CommandMode::Query;
            argumentIndex++;
        } else if (commandText == "sing-query") {
            cliOptions.commandMode = CommandMode::SingQuery;
            cliOptions.outputPath = "-";
            cliOptions.speaker = 6000;
            argumentIndex++;
        } else if (commandText == "sing-f0") {
            cliOptions.commandMode = CommandMode::SingF0;
            cliOptions.outputPath = "-";
            cliOptions.speaker = 6000;
            argumentIndex++;
        } else if (commandText == "sing-volume") {
            cliOptions.commandMode = CommandMode::SingVolume;
            cliOptions.outputPath = "-";
            cliOptions.speaker = 6000;
            argumentIndex++;
        } else if (commandText == "frame-synthesis") {
            cliOptions.commandMode = CommandMode::FrameSynthesis;
            cliOptions.speaker = 3000;
            argumentIndex++;
        } else if (commandText == "models" || commandText == "styles") {
            cliOptions.commandMode = CommandMode::Models;
            argumentIndex++;
        } else if (commandText == "inspect") {
            cliOptions.commandMode = CommandMode::Inspect;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string inspectArgument = argv[argumentIndex];
                if (!inspectArgument.empty() && inspectArgument[0] != '-') {
                    cliOptions.inspectPath = inspectArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "inventory") {
            cliOptions.commandMode = CommandMode::Inventory;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string inventoryArgument = argv[argumentIndex];
                if (!inventoryArgument.empty() && inventoryArgument[0] != '-') {
                    cliOptions.inspectPath = inventoryArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "assets") {
            cliOptions.commandMode = CommandMode::Assets;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string assetsArgument = argv[argumentIndex];
                if (!assetsArgument.empty() && assetsArgument[0] != '-') {
                    cliOptions.inspectPath = assetsArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "model-dump") {
            cliOptions.commandMode = CommandMode::ModelDump;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string modelDumpArgument = argv[argumentIndex];
                if (!modelDumpArgument.empty() && modelDumpArgument[0] != '-') {
                    cliOptions.inspectPath = modelDumpArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin" || commandText == "vvbin") {
            cliOptions.commandMode = CommandMode::VvBin;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinArgument = argv[argumentIndex];
                if (!vvBinArgument.empty() && vvBinArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "ort-inspect" || commandText == "ort") {
            cliOptions.commandMode = CommandMode::OrtInspect;
            argumentIndex++;
        } else if (commandText == "vv-bin-ort" || commandText == "vvbin-ort") {
            cliOptions.commandMode = CommandMode::VvBinOrt;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinOrtArgument = argv[argumentIndex];
                if (!vvBinOrtArgument.empty() && vvBinOrtArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinOrtArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin-models" || commandText == "vvbin-models") {
            cliOptions.commandMode = CommandMode::VvBinModels;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinModelsArgument = argv[argumentIndex];
                if (!vvBinModelsArgument.empty() && vvBinModelsArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinModelsArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin-operators" || commandText == "vvbin-operators") {
            cliOptions.commandMode = CommandMode::VvBinOperators;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinOperatorsArgument = argv[argumentIndex];
                if (!vvBinOperatorsArgument.empty() && vvBinOperatorsArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinOperatorsArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin-export-onnx" || commandText == "vvbin-export-onnx") {
            cliOptions.commandMode = CommandMode::VvBinExportOnnx;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinExportOnnxArgument = argv[argumentIndex];
                if (!vvBinExportOnnxArgument.empty() && vvBinExportOnnxArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinExportOnnxArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin-compare-onnx" || commandText == "vvbin-compare-onnx") {
            cliOptions.commandMode = CommandMode::VvBinCompareOnnx;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinCompareOnnxArgument = argv[argumentIndex];
                if (!vvBinCompareOnnxArgument.empty() && vvBinCompareOnnxArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinCompareOnnxArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin-chain" || commandText == "vvbin-chain" || commandText == "native-chain") {
            cliOptions.commandMode = CommandMode::VvBinChain;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinChainArgument = argv[argumentIndex];
                if (!vvBinChainArgument.empty() && vvBinChainArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinChainArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "vv-bin-synth" || commandText == "vvbin-synth" || commandText == "native-synth") {
            cliOptions.commandMode = CommandMode::VvBinSynth;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string vvBinSynthArgument = argv[argumentIndex];
                if (!vvBinSynthArgument.empty() && vvBinSynthArgument[0] != '-') {
                    cliOptions.inspectPath = vvBinSynthArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "native-onnx" || commandText == "onnx-session") {
            cliOptions.commandMode = CommandMode::NativeOnnx;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string nativeOnnxArgument = argv[argumentIndex];
                if (!nativeOnnxArgument.empty() && nativeOnnxArgument[0] != '-') {
                    cliOptions.inspectPath = nativeOnnxArgument;
                    argumentIndex++;
                }
            }
        } else if (commandText == "onnx-random-seed") {
            cliOptions.commandMode = CommandMode::OnnxRandomSeed;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string onnxRandomSeedArgument = argv[argumentIndex];
                if (!onnxRandomSeedArgument.empty() && onnxRandomSeedArgument[0] != '-') {
                    cliOptions.inspectPath = onnxRandomSeedArgument;
                    argumentIndex++;
                }
            }
        } else if (commandText == "cache") {
            cliOptions.commandMode = CommandMode::Cache;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string cacheArgument = argv[argumentIndex];
                if (!cacheArgument.empty() && cacheArgument[0] != '-') {
                    cliOptions.inspectPath = cacheArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "extract") {
            cliOptions.commandMode = CommandMode::Extract;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string extractArgument = argv[argumentIndex];
                if (!extractArgument.empty() && extractArgument[0] != '-') {
                    cliOptions.inspectPath = extractArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "validate") {
            cliOptions.commandMode = CommandMode::Validate;
            argumentIndex++;
            if (argumentIndex < argc) {
                std::string validateArgument = argv[argumentIndex];
                if (!validateArgument.empty() && validateArgument[0] != '-') {
                    cliOptions.inspectPath = validateArgument;
                    replaceModelPaths(cliOptions, cliOptions.inspectPath);
                    argumentIndex++;
                }
            }
        } else if (commandText == "speakers") {
            cliOptions.commandMode = CommandMode::Speakers;
            argumentIndex++;
        } else if (commandText == "deps" || commandText == "dependencies") {
            cliOptions.commandMode = CommandMode::Deps;
            argumentIndex++;
        } else if (commandText == "devices") {
            cliOptions.commandMode = CommandMode::Devices;
            argumentIndex++;
        } else if (commandText == "version") {
            cliOptions.commandMode = CommandMode::Version;
            argumentIndex++;
        } else if (commandText == "runtime_info") {
            cliOptions.commandMode = CommandMode::RuntimeInfo;
            argumentIndex++;
        } else if (commandText == "trace") {
            cliOptions.commandMode = CommandMode::Trace;
            argumentIndex++;
        } else if (commandText == "bench") {
            cliOptions.commandMode = CommandMode::Bench;
            argumentIndex++;
        } else if (commandText == "bench-song" || commandText == "song-bench") {
            cliOptions.commandMode = CommandMode::BenchSong;
            cliOptions.runs = 5;
            cliOptions.speaker = 6000;
            argumentIndex++;
        } else if (commandText == "bench-http" || commandText == "http-bench") {
            cliOptions.commandMode = CommandMode::BenchHttp;
            cliOptions.runs = 8;
            cliOptions.workers = 4;
            argumentIndex++;
        } else if (commandText == "bench-http-song" || commandText == "http-song-bench") {
            cliOptions.commandMode = CommandMode::BenchHttpSong;
            cliOptions.runs = 5;
            cliOptions.speaker = 6000;
            argumentIndex++;
        } else if (commandText == "api-session" || commandText == "http-session") {
            cliOptions.commandMode = CommandMode::ApiSession;
            argumentIndex++;
        } else if (commandText == "openapi") {
            cliOptions.commandMode = CommandMode::OpenApi;
            argumentIndex++;
        } else if (commandText == "--help" || commandText == "-h") {
            printUsage();
            std::exit(0);
        }
    }
    while (argumentIndex < argc) {
        std::string argumentText = argv[argumentIndex++];
        auto requireValue = [&](const std::string &optionName) -> std::string {
            if (argumentIndex >= argc) {
                throw std::runtime_error(optionName + " の値がありません");
            }
            return argv[argumentIndex++];
        };
        if (argumentText == "--runtime") {
            std::vector<fs::path> explicitModelPaths = cliOptions.runtimePaths.modelPaths;
            cliOptions.runtimePaths = makeDefaultRuntimePaths(resolveRuntimeRootPath(requireValue(argumentText)));
            cliOptions.hasExplicitOnnxruntimePath = false;
            cliOptions.autoDiscoveredOnnxruntimeLibraryPath.clear();
            if (cliOptions.hasExplicitModels) {
                cliOptions.runtimePaths.modelPaths = explicitModelPaths;
            }
        } else if (argumentText == "--core") {
            cliOptions.runtimePaths.coreLibraryPath = requireValue(argumentText);
        } else if (argumentText == "--onnxruntime") {
            cliOptions.runtimePaths.onnxruntimeLibraryPath = requireValue(argumentText);
            cliOptions.hasExplicitOnnxruntimePath = true;
            cliOptions.autoDiscoveredOnnxruntimeLibraryPath.clear();
        } else if (argumentText == "--compare-onnxruntime") {
            cliOptions.compareOnnxruntimeLibraryPath = requireValue(argumentText);
        } else if (argumentText == "--dict") {
            cliOptions.runtimePaths.dictionaryDirectory = requireValue(argumentText);
        } else if (argumentText == "--models" || argumentText == "--model") {
            replaceModelPaths(cliOptions, requireValue(argumentText));
        } else if (argumentText == "--add-model") {
            cliOptions.runtimePaths.modelPaths.push_back(requireValue(argumentText));
        } else if (argumentText == "--manifest") {
            cliOptions.runtimePaths.manifestPath = requireValue(argumentText);
            cliOptions.runtimePaths.hasManifestOverride = true;
        } else if (argumentText == "--state-dir") {
            applyStateDirectory(cliOptions, requireValue(argumentText));
        } else if (argumentText == "--user-dict") {
            cliOptions.runtimePaths.userDictPath = requireValue(argumentText);
        } else if (argumentText == "--setting") {
            cliOptions.runtimePaths.settingPath = requireValue(argumentText);
        } else if (argumentText == "--presets") {
            cliOptions.runtimePaths.presetPath = requireValue(argumentText);
        } else if (argumentText == "--library-dir") {
            cliOptions.runtimePaths.libraryDirectory = requireValue(argumentText);
        } else if (argumentText == "--backend") {
            cliOptions.runtimePaths.backendMode = requireValue(argumentText);
        } else if (argumentText == "--core-profile") {
            cliOptions.runtimePaths.coreProfile = requireValue(argumentText);
        } else if (argumentText == "--acceleration-mode") {
            cliOptions.runtimePaths.accelerationMode = requireValue(argumentText);
        } else if (argumentText == "--gpu") {
            cliOptions.runtimePaths.accelerationMode = "gpu";
        } else if (argumentText == "--cpu-threads") {
            unsigned long cpuThreadCount = std::stoul(requireValue(argumentText));
            if (cpuThreadCount > std::numeric_limits<uint16_t>::max()) {
                throw std::runtime_error("--cpu-threads が大きすぎます");
            }
            cliOptions.runtimePaths.cpuNumThreads = static_cast<uint16_t>(cpuThreadCount);
        } else if (argumentText == "--seed") {
            cliOptions.onnxSeed = std::stoll(requireValue(argumentText));
            cliOptions.hasOnnxSeed = true;
        } else if (argumentText == "--native-model-mode") {
            cliOptions.runtimePaths.nativeModelMode = requireValue(argumentText);
        } else if (argumentText == "--native-sing-teacher") {
            cliOptions.nativeSingTeacherMode = requireValue(argumentText);
        } else if (argumentText == "--host") {
            cliOptions.httpHost = requireValue(argumentText);
        } else if (argumentText == "--http-path") {
            setPrimaryBenchHttpPath(cliOptions, requireValue(argumentText));
        } else if (argumentText == "--add-http-path") {
            addBenchHttpPath(cliOptions, requireValue(argumentText));
        } else if (argumentText == "--keep-alive") {
            cliOptions.httpKeepAlive = true;
        } else if (argumentText == "--port") {
            cliOptions.port = std::stoi(requireValue(argumentText));
        } else if (argumentText == "--workers") {
            cliOptions.workers = static_cast<size_t>(std::stoul(requireValue(argumentText)));
            if (cliOptions.workers == 0) {
                throw std::runtime_error("--workers は 1 以上が必要です");
            }
        } else if (argumentText == "--enable_cancellable_synthesis" || argumentText == "--enable-cancellable-synthesis") {
            cliOptions.runtimePaths.enableCancellableSynthesis = true;
        } else if (argumentText == "--speaker") {
            cliOptions.speaker = static_cast<uint32_t>(std::stoul(requireValue(argumentText)));
            cliOptions.benchSpeakers.clear();
        } else if (argumentText == "--speakers") {
            cliOptions.benchSpeakers = parseSpeakerList(requireValue(argumentText));
            cliOptions.speaker = cliOptions.benchSpeakers.front();
        } else if (argumentText == "--runs") {
            cliOptions.runs = static_cast<size_t>(std::stoul(requireValue(argumentText)));
            if (cliOptions.runs == 0) {
                throw std::runtime_error("--runs は 1 以上が必要です");
            }
        } else if (argumentText == "--text") {
            setPrimaryBenchText(cliOptions, requireValue(argumentText));
        } else if (argumentText == "--add-text") {
            addBenchText(cliOptions, requireValue(argumentText));
        } else if (argumentText == "--kana") {
            cliOptions.isKana = true;
        } else if (argumentText == "--format") {
            cliOptions.audioStreamFormat = parseAudioStreamFormat(requireValue(argumentText));
        } else if (argumentText == "--chunk-samples") {
            cliOptions.chunkSamples = static_cast<size_t>(std::stoul(requireValue(argumentText)));
            if (cliOptions.chunkSamples == 0) {
                throw std::runtime_error("--chunk-samples は 1 以上が必要です");
            }
        } else if (argumentText == "--preview-bytes") {
            cliOptions.previewBytes = static_cast<size_t>(std::stoul(requireValue(argumentText)));
        } else if (argumentText == "--scan-models") {
            cliOptions.shouldScanModels = true;
        } else if (argumentText == "--full-scan") {
            cliOptions.shouldFullScan = true;
        } else if (argumentText == "--extract-onnx") {
            cliOptions.shouldExtractOnnx = true;
            cliOptions.onnxOutputDirectory = requireValue(argumentText);
        } else if (argumentText == "--extract-resources") {
            cliOptions.shouldExtractResources = true;
            cliOptions.resourceOutputDirectory = requireValue(argumentText);
        } else if (argumentText == "--extract-engine-assets") {
            cliOptions.shouldExtractEngineAssets = true;
            cliOptions.engineAssetOutputDirectory = requireValue(argumentText);
        } else if (argumentText == "--extract-runtime") {
            cliOptions.shouldExtractRuntime = true;
            cliOptions.runtimeOutputDirectory = requireValue(argumentText);
        } else if (argumentText == "--extract-onnxruntime") {
            cliOptions.shouldExtractOnnxruntime = true;
            cliOptions.onnxruntimeOutputDirectory = requireValue(argumentText);
        } else if (argumentText == "--inputs") {
            cliOptions.nativeOnnxInputDirectory = requireValue(argumentText);
        } else if (argumentText == "--asset") {
            cliOptions.modelAssetName = requireValue(argumentText);
        } else if (argumentText == "--audio-query") {
            cliOptions.audioQueryPath = requireValue(argumentText);
        } else if (argumentText == "--score") {
            cliOptions.scorePath = requireValue(argumentText);
        } else if (argumentText == "--add-score") {
            cliOptions.benchScorePaths.push_back(requireValue(argumentText));
        } else if (argumentText == "--frame-audio-query") {
            cliOptions.frameAudioQueryPath = requireValue(argumentText);
        } else if (argumentText == "--add-frame-audio-query") {
            cliOptions.benchFrameAudioQueryPaths.push_back(requireValue(argumentText));
        } else if (argumentText == "--out") {
            cliOptions.outputPath = requireValue(argumentText);
        } else if (argumentText == "--preload") {
            cliOptions.shouldPreload = true;
        } else if (argumentText == "--help" || argumentText == "-h") {
            printUsage();
            std::exit(0);
        } else {
            throw std::runtime_error("不明な引数です: " + argumentText);
        }
    }
    resolveAutoDiscoveredOnnxruntimePath(cliOptions);
    return cliOptions;
}

static std::vector<VvmArchiveSummary> collectArchiveSummaries(const std::vector<fs::path> &modelRoots) {
    std::vector<VvmArchiveSummary> archiveSummaries;
    for (const fs::path &modelPath : collectVvmModelFiles(modelRoots)) {
        archiveSummaries.push_back(inspectVvmArchive(modelPath));
    }
    return archiveSummaries;
}

static std::string createArchiveInspectionText(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    size_t styleCount = 0;
    size_t modelBinCount = 0;
    std::vector<std::pair<fs::path, std::vector<StyleRecord>>> modelStyleGroups;
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::vector<StyleRecord> styleRecords = extractStylesFromMetasJson(archiveSummary.metasJson);
        styleCount += styleRecords.size();
        modelBinCount += archiveSummary.modelBinNames.size();
        modelStyleGroups.push_back({archiveSummary.archivePath, std::move(styleRecords)});
    }
    std::ostringstream inspectionStream;
    inspectionStream << "models\t" << archiveSummaries.size() << "\n";
    inspectionStream << "styles\t" << styleCount << "\n";
    inspectionStream << "model_bins\t" << modelBinCount << "\n";
    inspectionStream << createStyleTable(modelStyleGroups);
    return inspectionStream.str();
}

static std::string createArchiveValidationText(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::ostringstream validationStream;
    validationStream << "vvm\tstatus\tentries\tchecked_entries\tmodel_bins\tstyles\tmanifest_bytes\tmetas_bytes\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::vector<StyleRecord> styleRecords = extractStylesFromMetasJson(archiveSummary.metasJson);
        if (styleRecords.empty()) {
            throw std::runtime_error("style がありません: " + archiveSummary.archivePath.string());
        }
        std::vector<VvmEntryValidationRecord> validationRecords = validateVvmArchiveEntries(archiveSummary.archivePath);
        for (const VvmEntryValidationRecord &validationRecord : validationRecords) {
            if (!validationRecord.isValid) {
                throw std::runtime_error("VVM entry 検証に失敗しました: " + archiveSummary.archivePath.string() + ":" + validationRecord.name);
            }
        }
        validationStream << archiveSummary.archivePath.filename().string() << "\t"
                         << "ok\t"
                         << archiveSummary.entries.size() << "\t"
                         << validationRecords.size() << "\t"
                         << archiveSummary.modelBinNames.size() << "\t"
                         << styleRecords.size() << "\t"
                         << archiveSummary.manifestJson.size() << "\t"
                         << archiveSummary.metasJson.size() << "\n";
    }
    return validationStream.str();
}

static std::string createHexUint32(uint32_t numberValue) {
    std::ostringstream hexStream;
    hexStream << std::hex << std::setfill('0') << std::setw(8) << numberValue;
    return hexStream.str();
}

static std::string createArchiveInventoryText(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::ostringstream inventoryStream;
    inventoryStream << "vvm\tentry\tdata_offset\tmethod\tcompressed_bytes\tuncompressed_bytes\tcrc32\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::string archiveName = archiveSummary.archivePath.filename().string();
        for (const VvmEntryRecord &archiveEntry : archiveSummary.entries) {
            inventoryStream << archiveName << "\t"
                            << archiveEntry.name << "\t"
                            << archiveEntry.dataOffset << "\t"
                            << archiveEntry.compressionMethod << "\t"
                            << archiveEntry.compressedSize << "\t"
                            << archiveEntry.uncompressedSize << "\t"
                            << createHexUint32(archiveEntry.crc32) << "\n";
        }
    }
    return inventoryStream.str();
}

static int runExtractCommand(const CliOptions &cliOptions) {
    if (cliOptions.outputPath.empty() || cliOptions.outputPath == "out.wav" || cliOptions.outputPath == "-") {
        throw std::runtime_error("extract には --out DIR が必要です");
    }
    std::vector<fs::path> modelFiles = collectVvmModelFiles(cliOptions.runtimePaths.modelPaths);
    for (const fs::path &modelPath : modelFiles) {
        fs::path outputDirectory = modelFiles.size() == 1 ? cliOptions.outputPath : cliOptions.outputPath / modelPath.stem();
        extractVvmArchive(modelPath, outputDirectory);
        std::cout << outputDirectory.string() << "\n";
    }
    return 0;
}

static bool isSupportedArchivePath(const fs::path &archivePath);
static std::vector<fs::path> collectArchivePaths(const std::vector<fs::path> &inputPaths);
static std::vector<std::string> createVoicevoxArchiveEntryMarkers(const std::vector<std::string> &fileNames) {
    std::vector<std::string> entryMarkers;
    entryMarkers.reserve(fileNames.size());
    for (const std::string &fileName : fileNames) {
        entryMarkers.push_back("vv-engine/" + fileName);
    }
    return entryMarkers;
}

static bool zipArchiveHasAnyEntryMarker(const fs::path &zipPath, const std::vector<std::string> &entryMarkers) {
    for (const std::string &entryMarker : entryMarkers) {
        if (zipArchiveHasEntryMarker(zipPath, entryMarker)) {
            return true;
        }
    }
    return false;
}

static std::string findZipEntryMarker(const fs::path &zipPath, const std::vector<std::string> &entryMarkers) {
    for (const std::string &entryMarker : entryMarkers) {
        if (zipArchiveHasEntryMarker(zipPath, entryMarker)) {
            return entryMarker;
        }
    }
    return "";
}

static std::vector<fs::path> collectVoicevoxZipPaths(const CliOptions &cliOptions) {
    std::vector<fs::path> zipPaths;
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    auto appendVoicevoxZipIfPresent = [&](const fs::path &zipPath) {
        if (lowercaseAscii(zipPath.extension().string()) != ".zip") {
            return;
        }
        if (zipArchiveHasAnyEntryMarker(zipPath, coreEntryMarkers)) {
            zipPaths.push_back(zipPath);
        }
    };
    for (const fs::path &modelRoot : cliOptions.runtimePaths.modelPaths) {
        ensurePathExists(modelRoot, "ZIP パス");
        if (fs::is_regular_file(modelRoot)) {
            appendVoicevoxZipIfPresent(modelRoot);
            continue;
        }
        if (fs::is_directory(modelRoot)) {
            for (const fs::directory_entry &directoryEntry : fs::directory_iterator(modelRoot)) {
                if (directoryEntry.is_regular_file()) {
                    appendVoicevoxZipIfPresent(directoryEntry.path());
                }
            }
        }
    }
    if (zipPaths.empty()) {
        throw std::runtime_error("VOICEVOX zip が見つかりません");
    }
    return zipPaths;
}

static bool isStandardOnnxruntimeArchivePath(const fs::path &archivePath) {
    if (!isSupportedArchivePath(archivePath)) {
        return false;
    }
    std::string filenameText = lowercaseAscii(archivePath.filename().string());
    return filenameText.find("onnxruntime") != std::string::npos;
}

static std::vector<fs::path> collectOnnxruntimeArchivePaths(const CliOptions &cliOptions) {
    std::vector<fs::path> archivePaths;
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    for (const fs::path &archivePath : collectArchivePaths(cliOptions.runtimePaths.modelPaths)) {
        if (!isStandardOnnxruntimeArchivePath(archivePath)) {
            continue;
        }
        if (lowercaseAscii(archivePath.extension().string()) == ".zip" && zipArchiveHasAnyEntryMarker(archivePath, coreEntryMarkers)) {
            continue;
        }
        archivePaths.push_back(archivePath);
    }
    return archivePaths;
}

static bool hasTarGzExtension(const fs::path &archivePath) {
    std::string archiveText = lowercaseAscii(archivePath.filename().string());
    return (archiveText.size() >= 4 && archiveText.substr(archiveText.size() - 4) == ".tgz")
        || (archiveText.size() >= 7 && archiveText.substr(archiveText.size() - 7) == ".tar.gz");
}

static bool isSupportedArchivePath(const fs::path &archivePath) {
    std::string extensionText = lowercaseAscii(archivePath.extension().string());
    return extensionText == ".zip" || extensionText == ".tar" || hasTarGzExtension(archivePath);
}

static std::vector<fs::path> collectArchivePaths(const std::vector<fs::path> &inputPaths) {
    std::vector<fs::path> archivePaths;
    for (const fs::path &inputPath : inputPaths) {
        ensurePathExists(inputPath, "archive パス");
        if (fs::is_regular_file(inputPath)) {
            if (isSupportedArchivePath(inputPath)) {
                archivePaths.push_back(inputPath);
            }
            continue;
        }
        if (!fs::is_directory(inputPath)) {
            continue;
        }
        for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(inputPath)) {
            if (!directoryEntry.is_regular_file()) {
                continue;
            }
            if (isSupportedArchivePath(directoryEntry.path())) {
                archivePaths.push_back(directoryEntry.path());
            }
        }
    }
    if (archivePaths.empty()) {
        throw std::runtime_error("archive が見つかりません");
    }
    std::sort(archivePaths.begin(), archivePaths.end());
    archivePaths.erase(std::unique(archivePaths.begin(), archivePaths.end()), archivePaths.end());
    return archivePaths;
}

static std::string createZipDirectoryExtractionText(const CliOptions &cliOptions, const std::string &directoryMarker, const fs::path &outputDirectory) {
    std::ostringstream extractionStream;
    extractionStream << "zip\tfiles\tbytes\tout\n";
    for (const fs::path &zipPath : collectVoicevoxZipPaths(cliOptions)) {
        ZipDirectoryExtractionSummary extractionSummary = extractZipDirectoryEntries(zipPath, directoryMarker, outputDirectory);
        extractionStream << zipPath.filename().string() << "\t"
                         << extractionSummary.fileCount << "\t"
                         << extractionSummary.byteCount << "\t"
                         << outputDirectory.string() << "\n";
    }
    return extractionStream.str();
}

static std::string createResourceExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.resourceOutputDirectory.empty()) {
        throw std::runtime_error("--extract-resources DIR が必要です");
    }
    return createZipDirectoryExtractionText(cliOptions, "resources/character_info/", cliOptions.resourceOutputDirectory);
}

static std::string createEngineAssetExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.engineAssetOutputDirectory.empty()) {
        throw std::runtime_error("--extract-engine-assets DIR が必要です");
    }
    std::ostringstream extractionStream;
    extractionStream << createZipDirectoryExtractionText(cliOptions, "resources/engine_manifest_assets/", cliOptions.engineAssetOutputDirectory);
    fs::path defaultCsvPath = cliOptions.engineAssetOutputDirectory.parent_path() / "default.csv";
    for (const fs::path &zipPath : collectVoicevoxZipPaths(cliOptions)) {
        uint64_t byteCount = 0;
        if (extractZipEntryByMarker(zipPath, "resources/default.csv", defaultCsvPath, byteCount)) {
            extractionStream << zipPath.filename().string() << "\t1\t" << byteCount << "\t" << defaultCsvPath.string() << "\n";
        }
        fs::path settingTemplatePath = cliOptions.engineAssetOutputDirectory.parent_path() / "setting_ui_template.html";
        if (extractZipEntryByMarker(zipPath, "resources/setting_ui_template.html", settingTemplatePath, byteCount)) {
            extractionStream << zipPath.filename().string() << "\t1\t" << byteCount << "\t" << settingTemplatePath.string() << "\n";
        }
    }
    return extractionStream.str();
}

static std::string createOnnxruntimeExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.onnxruntimeOutputDirectory.empty()) {
        throw std::runtime_error("--extract-onnxruntime DIR が必要です");
    }
    std::vector<fs::path> archivePaths = collectOnnxruntimeArchivePaths(cliOptions);
    if (archivePaths.empty()) {
        throw std::runtime_error("標準 ONNX Runtime archive が見つかりません");
    }
    std::ostringstream extractionStream;
    extractionStream << "archive\tfiles\tbytes\tout\n";
    for (const fs::path &archivePath : archivePaths) {
        ArchiveExtractionSummary extractionSummary = extractArchivePreservingPaths(archivePath, cliOptions.onnxruntimeOutputDirectory);
        extractionStream << archivePath.filename().string() << "\t"
                         << extractionSummary.fileCount << "\t"
                         << extractionSummary.byteCount << "\t"
                         << cliOptions.onnxruntimeOutputDirectory.string() << "\n";
    }
    return extractionStream.str();
}

static void appendRuntimeExtractionRecord(std::ostream *extractionStream, const std::string &componentName, size_t fileCount, uint64_t byteCount, const fs::path &outputPath) {
    if (extractionStream == nullptr) {
        return;
    }
    *extractionStream << componentName << "\t"
                      << fileCount << "\t"
                      << byteCount << "\t"
                      << outputPath.string() << "\n";
}

static size_t countRegularFilesInDirectory(const fs::path &directoryPath) {
    if (!fs::exists(directoryPath)) {
        return 0;
    }
    size_t fileCount = 0;
    for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(directoryPath)) {
        if (directoryEntry.is_regular_file()) {
            fileCount++;
        }
    }
    return fileCount;
}

static uint64_t sumRegularFileBytesInDirectory(const fs::path &directoryPath) {
    if (!fs::exists(directoryPath)) {
        return 0;
    }
    uint64_t byteCount = 0;
    for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(directoryPath)) {
        if (directoryEntry.is_regular_file()) {
            byteCount += static_cast<uint64_t>(directoryEntry.file_size());
        }
    }
    return byteCount;
}

static void copyRuntimeTree(const fs::path &sourcePath, const fs::path &destinationPath) {
    std::error_code copyError;
    fs::create_directories(destinationPath.parent_path());
    fs::copy(sourcePath, destinationPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("runtime resource をコピーできません: " + sourcePath.string());
    }
}

static void copyRuntimeFile(const fs::path &sourcePath, const fs::path &destinationPath) {
    std::error_code copyError;
    fs::create_directories(destinationPath.parent_path());
    fs::copy_file(sourcePath, destinationPath, fs::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("runtime file をコピーできません: " + sourcePath.string());
    }
    std::error_code permissionError;
    fs::permissions(destinationPath, fs::status(sourcePath).permissions(), fs::perm_options::replace, permissionError);
#if defined(_WIN32)
    permissionError.clear();
#endif
    if (permissionError) {
        throw std::runtime_error("runtime file 権限を設定できません: " + destinationPath.string());
    }
}

static void assembleRuntimeRoot(const fs::path &runtimeRoot, const std::vector<fs::path> &voicevoxZipPaths, const std::vector<fs::path> &onnxruntimeArchivePaths, std::ostream *extractionStream) {
    fs::path sourceRoot = getExecutableDirectory();
    fs::path sourceBinaryPath = sourceRoot / getPlatformExecutableFilename("litevox");
    fs::path sourceResourcesPath = sourceRoot / "resources";
    ensurePathExists(sourceBinaryPath, "litevox binary");
    ensurePathExists(sourceResourcesPath, "litevox resources");
    fs::create_directories(runtimeRoot);
    copyRuntimeFile(sourceBinaryPath, runtimeRoot / getPlatformExecutableFilename("litevox"));
    appendRuntimeExtractionRecord(extractionStream, "litevox", 1, fs::file_size(sourceBinaryPath), runtimeRoot / getPlatformExecutableFilename("litevox"));
    copyRuntimeTree(sourceResourcesPath, runtimeRoot / "resources");
    appendRuntimeExtractionRecord(extractionStream, "resources", countRegularFilesInDirectory(sourceResourcesPath), sumRegularFileBytesInDirectory(sourceResourcesPath), runtimeRoot / "resources");
    fs::create_directories(runtimeRoot / "core_libraries");
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    const std::vector<std::string> onnxruntimeEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxOnnxruntimeLibraryFileNames());
    for (const fs::path &zipPath : voicevoxZipPaths) {
        uint64_t byteCount = 0;
        std::string coreEntryMarker = findZipEntryMarker(zipPath, coreEntryMarkers);
        if (coreEntryMarker.empty() || !extractZipEntryByMarker(zipPath, coreEntryMarker, runtimeRoot / fs::path(coreEntryMarker).filename(), byteCount)) {
            throw std::runtime_error("voicevox_core library が見つかりません: " + zipPath.string());
        }
        appendRuntimeExtractionRecord(extractionStream, "voicevox_core", 1, byteCount, runtimeRoot / fs::path(coreEntryMarker).filename());
        std::string onnxruntimeEntryMarker = findZipEntryMarker(zipPath, onnxruntimeEntryMarkers);
        if (onnxruntimeEntryMarker.empty() || !extractZipEntryByMarker(zipPath, onnxruntimeEntryMarker, runtimeRoot / fs::path(onnxruntimeEntryMarker).filename(), byteCount)) {
            throw std::runtime_error("voicevox_onnxruntime library が見つかりません: " + zipPath.string());
        }
        appendRuntimeExtractionRecord(extractionStream, "voicevox_onnxruntime", 1, byteCount, runtimeRoot / fs::path(onnxruntimeEntryMarker).filename());
        if (!extractZipEntryByMarker(zipPath, "vv-engine/engine_manifest.json", runtimeRoot / "engine_manifest.json", byteCount)) {
            throw std::runtime_error("engine_manifest.json が見つかりません: " + zipPath.string());
        }
        appendRuntimeExtractionRecord(extractionStream, "engine_manifest", 1, byteCount, runtimeRoot / "engine_manifest.json");
        ZipDirectoryExtractionSummary modelSummary = extractZipDirectoryEntries(zipPath, "vv-engine/model/", runtimeRoot / "model-vvm");
        appendRuntimeExtractionRecord(extractionStream, "model_vvm", modelSummary.fileCount, modelSummary.byteCount, runtimeRoot / "model-vvm");
        ZipDirectoryExtractionSummary dictSummary = extractZipDirectoryEntries(zipPath, "open_jtalk_dic_utf_8-1.11/", runtimeRoot / "open_jtalk_dic_utf_8-1.11");
        appendRuntimeExtractionRecord(extractionStream, "open_jtalk_dict", dictSummary.fileCount, dictSummary.byteCount, runtimeRoot / "open_jtalk_dic_utf_8-1.11");
        ZipDirectoryExtractionSummary characterSummary = extractZipDirectoryEntries(zipPath, "resources/character_info/", runtimeRoot / "resources" / "character_info");
        appendRuntimeExtractionRecord(extractionStream, "character_info", characterSummary.fileCount, characterSummary.byteCount, runtimeRoot / "resources" / "character_info");
        ZipDirectoryExtractionSummary engineAssetSummary = extractZipDirectoryEntries(zipPath, "resources/engine_manifest_assets/", runtimeRoot / "resources" / "engine_manifest_assets");
        appendRuntimeExtractionRecord(extractionStream, "engine_manifest_assets", engineAssetSummary.fileCount, engineAssetSummary.byteCount, runtimeRoot / "resources" / "engine_manifest_assets");
        if (extractZipEntryByMarker(zipPath, "resources/default.csv", runtimeRoot / "resources" / "default.csv", byteCount)) {
            appendRuntimeExtractionRecord(extractionStream, "default_csv", 1, byteCount, runtimeRoot / "resources" / "default.csv");
        }
        if (extractZipEntryByMarker(zipPath, "resources/setting_ui_template.html", runtimeRoot / "resources" / "setting_ui_template.html", byteCount)) {
            appendRuntimeExtractionRecord(extractionStream, "setting_ui_template", 1, byteCount, runtimeRoot / "resources" / "setting_ui_template.html");
        }
    }
    for (const fs::path &archivePath : onnxruntimeArchivePaths) {
        ArchiveExtractionSummary extractionSummary = extractArchivePreservingPaths(archivePath, runtimeRoot);
        appendRuntimeExtractionRecord(extractionStream, "standard_onnxruntime", extractionSummary.fileCount, extractionSummary.byteCount, runtimeRoot);
    }
}

static uint64_t getPathFileSizeOrZero(const fs::path &filePath) {
    std::error_code fileError;
    uint64_t fileSize = fs::is_regular_file(filePath, fileError) ? static_cast<uint64_t>(fs::file_size(filePath, fileError)) : 0;
    if (fileError) {
        return 0;
    }
    return fileSize;
}

static int64_t getPathWriteTimeOrZero(const fs::path &filePath) {
    std::error_code timeError;
    fs::file_time_type writeTime = fs::last_write_time(filePath, timeError);
    if (timeError) {
        return 0;
    }
    return static_cast<int64_t>(writeTime.time_since_epoch().count());
}

static std::string createRuntimeCacheKey(const fs::path &runtimeArchivePath) {
    fs::path sourceRoot = getExecutableDirectory();
    fs::path sourceBinaryPath = sourceRoot / getPlatformExecutableFilename("litevox");
    fs::path sourceResourcesPath = sourceRoot / "resources";
    fs::path sourceOpenApiPath = sourceResourcesPath / "openapi.json";
    std::ostringstream cacheKeyStream;
    cacheKeyStream << fs::weakly_canonical(runtimeArchivePath).string() << "\n"
                   << getPathFileSizeOrZero(runtimeArchivePath) << "\n"
                   << getPathWriteTimeOrZero(runtimeArchivePath) << "\n"
                   << fs::weakly_canonical(sourceRoot).string() << "\n"
                   << getPathFileSizeOrZero(sourceBinaryPath) << "\n"
                   << getPathWriteTimeOrZero(sourceBinaryPath) << "\n"
                   << countRegularFilesInDirectory(sourceResourcesPath) << "\n"
                   << sumRegularFileBytesInDirectory(sourceResourcesPath) << "\n"
                   << getPathFileSizeOrZero(sourceOpenApiPath) << "\n"
                   << getPathWriteTimeOrZero(sourceOpenApiPath) << "\n";
    std::string cacheSeed = cacheKeyStream.str();
    return createSha256Hex(reinterpret_cast<const uint8_t *>(cacheSeed.data()), cacheSeed.size());
}

static bool isAssembledRuntimeRootReady(const fs::path &runtimeRoot) {
    return fs::exists(runtimeRoot / getPlatformExecutableFilename("litevox"))
        && !findFirstExistingPath(runtimeRoot, getVoicevoxCoreLibraryFileNames()).empty()
        && !findFirstExistingPath(runtimeRoot, getVoicevoxOnnxruntimeLibraryFileNames()).empty()
        && fs::exists(runtimeRoot / "engine_manifest.json")
        && fs::exists(runtimeRoot / "model-vvm")
        && fs::exists(runtimeRoot / "open_jtalk_dic_utf_8-1.11")
        && fs::exists(runtimeRoot / "resources");
}

static bool isVoicevoxRuntimeZipPath(const fs::path &archivePath) {
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    return lowercaseAscii(archivePath.extension().string()) == ".zip"
        && zipArchiveHasAnyEntryMarker(archivePath, coreEntryMarkers);
}

static fs::path resolveRuntimeRootPath(const fs::path &runtimeInputPath) {
    ensurePathExists(runtimeInputPath, "runtime パス");
    if (!fs::is_regular_file(runtimeInputPath) || !isVoicevoxRuntimeZipPath(runtimeInputPath)) {
        return runtimeInputPath;
    }
    fs::path parentPath = runtimeInputPath.parent_path();
    fs::path cacheBasePath = (parentPath.empty() ? fs::path(".") : parentPath) / ".litevox-runtime-cache";
    std::string cacheKey = createRuntimeCacheKey(runtimeInputPath);
    fs::path runtimeRoot = cacheBasePath / (runtimeInputPath.stem().string() + "-" + cacheKey.substr(0, 16));
    if (isAssembledRuntimeRootReady(runtimeRoot)) {
        return runtimeRoot;
    }
    std::error_code removeError;
    fs::remove_all(runtimeRoot, removeError);
    if (removeError) {
        throw std::runtime_error("runtime cache を初期化できません: " + runtimeRoot.string());
    }
    assembleRuntimeRoot(runtimeRoot, {runtimeInputPath}, {}, nullptr);
    return runtimeRoot;
}

static std::string createRuntimeExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.runtimeOutputDirectory.empty()) {
        throw std::runtime_error("--extract-runtime DIR が必要です");
    }
    std::ostringstream extractionStream;
    extractionStream << "component\tfiles\tbytes\tout\n";
    assembleRuntimeRoot(cliOptions.runtimeOutputDirectory, collectVoicevoxZipPaths(cliOptions), collectOnnxruntimeArchivePaths(cliOptions), &extractionStream);
    return extractionStream.str();
}

static int runCacheCommand(const CliOptions &cliOptions) {
    std::vector<ModelAssetRecord> modelAssets = collectModelAssetsFromModelRoots(cliOptions.runtimePaths.modelPaths);
    ModelSessionCache modelSessionCache = loadModelSessionCache(modelAssets);
    std::cout << createModelSessionCacheSummary(modelSessionCache);
    return 0;
}

static void writeCliTextOutput(const fs::path &outputPath, const std::string &outputText) {
    if (outputPath.empty() || outputPath == "-") {
        std::cout << outputText << "\n";
        return;
    }
    writeTextFile(outputPath, outputText);
    std::cout << outputPath.string() << "\n";
}

static std::string createCliOpenApiJson(const CliOptions &cliOptions) {
    fs::path schemaPath = cliOptions.runtimePaths.engineManifestAssetDirectory.parent_path() / "openapi.json";
    if (fs::exists(schemaPath)) {
        return readTextFile(schemaPath);
    }
    return createOpenApiJson();
}

static double getElapsedMilliseconds(std::chrono::steady_clock::time_point startTime, std::chrono::steady_clock::time_point endTime) {
    return std::chrono::duration<double, std::milli>(endTime - startTime).count();
}

static std::vector<uint32_t> getBenchSpeakerIds(const CliOptions &cliOptions) {
    if (!cliOptions.benchSpeakers.empty()) {
        return cliOptions.benchSpeakers;
    }
    return {cliOptions.speaker};
}

static std::vector<std::string> getBenchTexts(const CliOptions &cliOptions) {
    if (!cliOptions.benchTexts.empty()) {
        return cliOptions.benchTexts;
    }
    return {cliOptions.text};
}

static std::vector<std::string> getBenchHttpPaths(const CliOptions &cliOptions) {
    if (!cliOptions.benchHttpPaths.empty()) {
        return cliOptions.benchHttpPaths;
    }
    return {cliOptions.httpPath};
}

static std::vector<uint8_t> synthesizeBenchAudio(RuntimeState &runtimeState, const std::string &textValue, bool isKana, uint32_t speakerId) {
    if (isKana) {
        return synthesizeKana(runtimeState, textValue, speakerId);
    }
    return synthesizeText(runtimeState, textValue, speakerId);
}

static std::string joinSizeValues(const std::vector<size_t> &values) {
    std::ostringstream valuesStream;
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            valuesStream << ",";
        }
        valuesStream << values[valueIndex];
    }
    return valuesStream.str();
}

static std::string joinUint64Values(const std::vector<uint64_t> &values) {
    std::ostringstream valuesStream;
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            valuesStream << ",";
        }
        valuesStream << values[valueIndex];
    }
    return valuesStream.str();
}

static std::string joinDoubleValues(const std::vector<double> &values) {
    std::ostringstream valuesStream;
    valuesStream << std::fixed << std::setprecision(3);
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            valuesStream << ",";
        }
        valuesStream << values[valueIndex];
    }
    return valuesStream.str();
}

static std::string joinSpeakerIds(const std::vector<uint32_t> &speakerIds) {
    std::ostringstream speakerStream;
    for (size_t speakerIndex = 0; speakerIndex < speakerIds.size(); speakerIndex++) {
        if (speakerIndex > 0) {
            speakerStream << ",";
        }
        speakerStream << speakerIds[speakerIndex];
    }
    return speakerStream.str();
}

static std::string joinSpeakerCounts(const std::vector<uint32_t> &speakerIds, const std::vector<size_t> &speakerCounts) {
    std::ostringstream speakerStream;
    for (size_t speakerIndex = 0; speakerIndex < speakerIds.size(); speakerIndex++) {
        if (speakerIndex > 0) {
            speakerStream << ",";
        }
        speakerStream << speakerIds[speakerIndex] << ":" << speakerCounts[speakerIndex];
    }
    return speakerStream.str();
}

static std::string joinStringValues(const std::vector<std::string> &values) {
    std::ostringstream valueStream;
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            valueStream << ",";
        }
        valueStream << values[valueIndex];
    }
    return valueStream.str();
}

static std::string joinStringCounts(const std::vector<std::string> &values, const std::vector<size_t> &counts) {
    std::ostringstream valueStream;
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            valueStream << ",";
        }
        valueStream << values[valueIndex] << ":" << counts[valueIndex];
    }
    return valueStream.str();
}

static std::string joinTextByteLengths(const std::vector<std::string> &texts) {
    std::vector<size_t> textByteLengths;
    textByteLengths.reserve(texts.size());
    for (const std::string &textValue : texts) {
        textByteLengths.push_back(textValue.size());
    }
    return joinSizeValues(textByteLengths);
}

static std::string getRoundRobinModeText(size_t valueCount) {
    if (valueCount > 1) {
        return "round_robin";
    }
    return "single";
}

static std::vector<std::string> getBenchScoreTexts(const CliOptions &cliOptions) {
    if (cliOptions.scorePath.empty()) {
        throw std::runtime_error("--score が必要です");
    }
    std::vector<std::string> scoreTexts;
    scoreTexts.push_back(readTextFile(cliOptions.scorePath));
    for (const fs::path &scorePath : cliOptions.benchScorePaths) {
        scoreTexts.push_back(readTextFile(scorePath));
    }
    return scoreTexts;
}

static std::vector<std::string> getBenchFrameAudioQueryTexts(const CliOptions &cliOptions, size_t scoreCount) {
    std::vector<std::string> frameAudioQueryTexts;
    if (cliOptions.frameAudioQueryPath.empty()) {
        if (!cliOptions.benchFrameAudioQueryPaths.empty()) {
            throw std::runtime_error("--add-frame-audio-query の前に --frame-audio-query が必要です");
        }
        return frameAudioQueryTexts;
    }
    frameAudioQueryTexts.push_back(readTextFile(cliOptions.frameAudioQueryPath));
    for (const fs::path &frameAudioQueryPath : cliOptions.benchFrameAudioQueryPaths) {
        frameAudioQueryTexts.push_back(readTextFile(frameAudioQueryPath));
    }
    if (scoreCount > 1 && frameAudioQueryTexts.size() != scoreCount) {
        throw std::runtime_error("複数 score を使う場合、--frame-audio-query と --add-frame-audio-query の総数は score 数と一致する必要があります");
    }
    return frameAudioQueryTexts;
}

static std::vector<size_t> createRoundRobinCounts(size_t runCount, size_t valueCount) {
    std::vector<size_t> counts(valueCount, 0);
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        counts[runIndex % valueCount]++;
    }
    return counts;
}

static std::string getCartesianModeText(size_t combinationCycle) {
    if (combinationCycle > 1) {
        return "cartesian";
    }
    return "single";
}

struct BenchCaseSelection {
    size_t speakerIndex = 0;
    size_t textIndex = 0;
    size_t pathIndex = 0;
    size_t combinationIndex = 0;
};

static size_t getCombinationCycleSize(size_t speakerCount, size_t textCount, size_t pathCount) {
    return speakerCount * textCount * pathCount;
}

static BenchCaseSelection getBenchCaseSelection(size_t runIndex, size_t speakerCount, size_t textCount, size_t pathCount) {
    BenchCaseSelection selection;
    selection.speakerIndex = runIndex % speakerCount;
    size_t remainingIndex = runIndex / speakerCount;
    selection.textIndex = remainingIndex % textCount;
    remainingIndex /= textCount;
    selection.pathIndex = remainingIndex % pathCount;
    selection.combinationIndex = selection.pathIndex * textCount * speakerCount
        + selection.textIndex * speakerCount
        + selection.speakerIndex;
    return selection;
}

static std::string joinCombinationCounts(const std::vector<uint32_t> &speakerIds, size_t textCount, size_t pathCount, const std::vector<size_t> &combinationCounts) {
    std::ostringstream valueStream;
    size_t combinationIndex = 0;
    for (size_t pathIndex = 0; pathIndex < pathCount; pathIndex++) {
        for (size_t textIndex = 0; textIndex < textCount; textIndex++) {
            for (size_t speakerIndex = 0; speakerIndex < speakerIds.size(); speakerIndex++) {
                if (combinationIndex > 0) {
                    valueStream << ",";
                }
                valueStream << "s" << speakerIds[speakerIndex] << "-t" << textIndex;
                if (pathCount > 1) {
                    valueStream << "-p" << pathIndex;
                }
                valueStream << ":" << combinationCounts[combinationIndex];
                combinationIndex++;
            }
        }
    }
    return valueStream.str();
}

static size_t countActiveBenchWorkers(const std::vector<size_t> &completedRunsByWorker) {
    size_t activeWorkerCount = 0;
    for (size_t completedWorkerRuns : completedRunsByWorker) {
        if (completedWorkerRuns > 0) {
            activeWorkerCount++;
        }
    }
    return activeWorkerCount;
}

static void printBenchMetrics(std::chrono::steady_clock::time_point runtimeStartTime, std::chrono::steady_clock::time_point runtimeReadyTime, std::chrono::steady_clock::time_point synthesisStartTime, std::chrono::steady_clock::time_point synthesisEndTime, size_t runCount, size_t workerCount, const std::vector<uint32_t> &speakerIds, const std::vector<std::string> &benchTexts, size_t completedRunCount, size_t wavBytesSize, uint64_t totalWavBytesSize, const std::vector<size_t> &completedRunsByWorker, const std::vector<uint64_t> &totalWavBytesByWorker, const std::vector<size_t> &completedRunsBySpeaker, const std::vector<size_t> &completedRunsByText, const std::vector<size_t> &completedRunsByCombination) {
    double synthesisMilliseconds = getElapsedMilliseconds(synthesisStartTime, synthesisEndTime);
    double throughputPerSecond = synthesisMilliseconds > 0.0 ? static_cast<double>(completedRunCount) * 1000.0 / synthesisMilliseconds : 0.0;
    size_t combinationCycle = getCombinationCycleSize(speakerIds.size(), benchTexts.size(), 1);
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "init_ms\t" << getElapsedMilliseconds(runtimeStartTime, runtimeReadyTime) << "\n";
    std::cout << "synthesis_ms\t" << synthesisMilliseconds << "\n";
    std::cout << "runs\t" << runCount << "\n";
    std::cout << "workers\t" << workerCount << "\n";
    std::cout << "workload_mode\t" << getCartesianModeText(combinationCycle) << "\n";
    std::cout << "combination_cycle\t" << combinationCycle << "\n";
    std::cout << "speaker_mode\t" << getRoundRobinModeText(speakerIds.size()) << "\n";
    std::cout << "speakers\t" << joinSpeakerIds(speakerIds) << "\n";
    std::cout << "text_mode\t" << getRoundRobinModeText(benchTexts.size()) << "\n";
    std::cout << "text_count\t" << benchTexts.size() << "\n";
    std::cout << "text_utf8_bytes\t" << joinTextByteLengths(benchTexts) << "\n";
    std::cout << "completed_runs\t" << completedRunCount << "\n";
    std::cout << "speaker_completed_runs\t" << joinSpeakerCounts(speakerIds, completedRunsBySpeaker) << "\n";
    std::cout << "text_completed_runs\t" << joinSizeValues(completedRunsByText) << "\n";
    std::cout << "combination_completed_runs\t" << joinCombinationCounts(speakerIds, benchTexts.size(), 1, completedRunsByCombination) << "\n";
    std::cout << "wav_bytes\t" << wavBytesSize << "\n";
    std::cout << "total_wav_bytes\t" << totalWavBytesSize << "\n";
    std::cout << "throughput_rps\t" << throughputPerSecond << "\n";
    std::cout << "active_workers\t" << countActiveBenchWorkers(completedRunsByWorker) << "\n";
    std::cout << "worker_completed_runs\t" << joinSizeValues(completedRunsByWorker) << "\n";
    std::cout << "worker_total_wav_bytes\t" << joinUint64Values(totalWavBytesByWorker) << "\n";
    std::cout << "max_rss_bytes\t" << getPeakResidentBytes() << "\n";
}

static std::vector<std::unique_ptr<RuntimeHolder>> createRuntimeHolders(const CliOptions &cliOptions, std::map<uint32_t, size_t> *sharedLoadedStyleCounts, std::mutex *sharedLoadedStylesMutex, std::map<uint32_t, uint64_t> *sharedStyleUnloadGenerations, std::mutex *sharedStyleUnloadMutex, std::mutex *sharedUserDictMutex, std::mutex *sharedPresetMutex, std::mutex *sharedSettingMutex, std::mutex *sharedLibraryMutex) {
    std::vector<std::unique_ptr<RuntimeHolder>> runtimeHolders(cliOptions.workers);
    std::atomic<bool> hasRuntimeError{false};
    std::string firstErrorMessage;
    std::mutex firstErrorMessageMutex;
    std::vector<std::thread> initializerThreads;
    initializerThreads.reserve(cliOptions.workers);
    for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
        initializerThreads.emplace_back([&, workerIndex]() {
            try {
                RuntimeState *createdRuntimeState = new RuntimeState(createRuntimeState(cliOptions.runtimePaths, false));
                createdRuntimeState->workerIndex = workerIndex;
                createdRuntimeState->workerCount = cliOptions.workers;
                createdRuntimeState->sharedLoadedStyleCounts = sharedLoadedStyleCounts;
                createdRuntimeState->sharedLoadedStylesMutex = sharedLoadedStylesMutex;
                createdRuntimeState->sharedStyleUnloadGenerations = sharedStyleUnloadGenerations;
                createdRuntimeState->sharedStyleUnloadMutex = sharedStyleUnloadMutex;
                createdRuntimeState->sharedUserDictMutex = sharedUserDictMutex;
                createdRuntimeState->sharedPresetMutex = sharedPresetMutex;
                createdRuntimeState->sharedSettingMutex = sharedSettingMutex;
                createdRuntimeState->sharedLibraryMutex = sharedLibraryMutex;
                if (cliOptions.shouldPreload) {
                    loadAllVoiceModels(*createdRuntimeState);
                }
                runtimeHolders[workerIndex] = std::make_unique<RuntimeHolder>(createdRuntimeState);
            } catch (const std::exception &caughtException) {
                if (!hasRuntimeError.exchange(true)) {
                    std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                    firstErrorMessage = caughtException.what();
                }
            } catch (...) {
                if (!hasRuntimeError.exchange(true)) {
                    std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                    firstErrorMessage = "runtime worker 初期化中に不明なエラーが発生しました";
                }
            }
        });
    }
    for (std::thread &initializerThread : initializerThreads) {
        initializerThread.join();
    }
    if (hasRuntimeError.load()) {
        throw std::runtime_error(firstErrorMessage);
    }
    return runtimeHolders;
}

static int runBenchCommand(const CliOptions &cliOptions) {
    std::vector<std::string> benchTexts = getBenchTexts(cliOptions);
    if (benchTexts.empty() || benchTexts.front().empty()) {
        throw std::runtime_error("--text が必要です");
    }
    std::vector<uint32_t> benchSpeakerIds = getBenchSpeakerIds(cliOptions);
    auto runtimeStartTime = std::chrono::steady_clock::now();
    if (cliOptions.workers > 1) {
        std::map<uint32_t, size_t> sharedLoadedStyleCounts;
        std::mutex sharedLoadedStylesMutex;
        std::map<uint32_t, uint64_t> sharedStyleUnloadGenerations;
        std::mutex sharedStyleUnloadMutex;
        std::mutex sharedUserDictMutex;
        std::mutex sharedPresetMutex;
        std::mutex sharedSettingMutex;
        std::mutex sharedLibraryMutex;
        std::vector<std::unique_ptr<RuntimeHolder>> runtimeHolders = createRuntimeHolders(cliOptions, &sharedLoadedStyleCounts, &sharedLoadedStylesMutex, &sharedStyleUnloadGenerations, &sharedStyleUnloadMutex, &sharedUserDictMutex, &sharedPresetMutex, &sharedSettingMutex, &sharedLibraryMutex);
        auto runtimeReadyTime = std::chrono::steady_clock::now();
        std::atomic<size_t> nextRunIndex{0};
        std::atomic<size_t> completedRunCount{0};
        std::atomic<size_t> wavBytesSize{0};
        std::atomic<uint64_t> totalWavBytesSize{0};
        std::vector<size_t> completedRunsByWorker(cliOptions.workers, 0);
        std::vector<uint64_t> totalWavBytesByWorker(cliOptions.workers, 0);
        std::vector<size_t> completedRunsBySpeaker(benchSpeakerIds.size(), 0);
        std::vector<size_t> completedRunsByText(benchTexts.size(), 0);
        std::vector<size_t> completedRunsByCombination(getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), 1), 0);
        std::mutex completedRunsBySpeakerMutex;
        std::mutex completedRunsByTextMutex;
        std::mutex completedRunsByCombinationMutex;
        std::atomic<bool> hasBenchError{false};
        std::string firstErrorMessage;
        std::mutex firstErrorMessageMutex;
        std::vector<std::thread> benchThreads;
        benchThreads.reserve(cliOptions.workers);
        auto synthesisStartTime = std::chrono::steady_clock::now();
        for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
            benchThreads.emplace_back([&, workerIndex]() {
                RuntimeState &workerRuntimeState = *runtimeHolders[workerIndex]->runtimeState;
                size_t workerCompletedRuns = 0;
                uint64_t workerTotalWavBytes = 0;
                std::vector<size_t> workerCompletedRunsBySpeaker(benchSpeakerIds.size(), 0);
                std::vector<size_t> workerCompletedRunsByText(benchTexts.size(), 0);
                std::vector<size_t> workerCompletedRunsByCombination(completedRunsByCombination.size(), 0);
                while (!hasBenchError.load()) {
                    size_t runIndex = nextRunIndex.fetch_add(1);
                    if (runIndex >= cliOptions.runs) {
                        break;
                    }
                    try {
                        BenchCaseSelection selection = getBenchCaseSelection(runIndex, benchSpeakerIds.size(), benchTexts.size(), 1);
                        std::vector<uint8_t> wavBytes = synthesizeBenchAudio(workerRuntimeState, benchTexts[selection.textIndex], cliOptions.isKana, benchSpeakerIds[selection.speakerIndex]);
                        wavBytesSize.store(wavBytes.size());
                        totalWavBytesSize.fetch_add(static_cast<uint64_t>(wavBytes.size()));
                        completedRunCount.fetch_add(1);
                        workerCompletedRuns++;
                        workerTotalWavBytes += static_cast<uint64_t>(wavBytes.size());
                        workerCompletedRunsBySpeaker[selection.speakerIndex]++;
                        workerCompletedRunsByText[selection.textIndex]++;
                        workerCompletedRunsByCombination[selection.combinationIndex]++;
                    } catch (const std::exception &caughtException) {
                        if (!hasBenchError.exchange(true)) {
                            std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                            firstErrorMessage = caughtException.what();
                        }
                    } catch (...) {
                        if (!hasBenchError.exchange(true)) {
                            std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                            firstErrorMessage = "bench 実行中に不明なエラーが発生しました";
                        }
                    }
                }
                completedRunsByWorker[workerIndex] = workerCompletedRuns;
                totalWavBytesByWorker[workerIndex] = workerTotalWavBytes;
                std::lock_guard<std::mutex> completedRunsBySpeakerLock(completedRunsBySpeakerMutex);
                for (size_t speakerIndex = 0; speakerIndex < benchSpeakerIds.size(); speakerIndex++) {
                    completedRunsBySpeaker[speakerIndex] += workerCompletedRunsBySpeaker[speakerIndex];
                }
                std::lock_guard<std::mutex> completedRunsByTextLock(completedRunsByTextMutex);
                for (size_t textIndex = 0; textIndex < benchTexts.size(); textIndex++) {
                    completedRunsByText[textIndex] += workerCompletedRunsByText[textIndex];
                }
                std::lock_guard<std::mutex> completedRunsByCombinationLock(completedRunsByCombinationMutex);
                for (size_t combinationIndex = 0; combinationIndex < completedRunsByCombination.size(); combinationIndex++) {
                    completedRunsByCombination[combinationIndex] += workerCompletedRunsByCombination[combinationIndex];
                }
            });
        }
        for (std::thread &benchThread : benchThreads) {
            benchThread.join();
        }
        if (hasBenchError.load()) {
            throw std::runtime_error(firstErrorMessage);
        }
        auto synthesisEndTime = std::chrono::steady_clock::now();
        printBenchMetrics(runtimeStartTime, runtimeReadyTime, synthesisStartTime, synthesisEndTime, cliOptions.runs, cliOptions.workers, benchSpeakerIds, benchTexts, completedRunCount.load(), wavBytesSize.load(), totalWavBytesSize.load(), completedRunsByWorker, totalWavBytesByWorker, completedRunsBySpeaker, completedRunsByText, completedRunsByCombination);
        return 0;
    }
    RuntimeState *runtimeState = new RuntimeState(createRuntimeState(cliOptions.runtimePaths, cliOptions.shouldPreload));
    auto runtimeReadyTime = std::chrono::steady_clock::now();
    size_t wavBytesSize = 0;
    uint64_t totalWavBytesSize = 0;
    std::vector<size_t> completedRunsBySpeaker(benchSpeakerIds.size(), 0);
    std::vector<size_t> completedRunsByText(benchTexts.size(), 0);
    std::vector<size_t> completedRunsByCombination(getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), 1), 0);
    auto synthesisStartTime = std::chrono::steady_clock::now();
    for (size_t runIndex = 0; runIndex < cliOptions.runs; runIndex++) {
        BenchCaseSelection selection = getBenchCaseSelection(runIndex, benchSpeakerIds.size(), benchTexts.size(), 1);
        std::vector<uint8_t> wavBytes = synthesizeBenchAudio(*runtimeState, benchTexts[selection.textIndex], cliOptions.isKana, benchSpeakerIds[selection.speakerIndex]);
        wavBytesSize = wavBytes.size();
        totalWavBytesSize += static_cast<uint64_t>(wavBytesSize);
        completedRunsBySpeaker[selection.speakerIndex]++;
        completedRunsByText[selection.textIndex]++;
        completedRunsByCombination[selection.combinationIndex]++;
    }
    auto synthesisEndTime = std::chrono::steady_clock::now();
    printBenchMetrics(runtimeStartTime, runtimeReadyTime, synthesisStartTime, synthesisEndTime, cliOptions.runs, cliOptions.workers, benchSpeakerIds, benchTexts, cliOptions.runs, wavBytesSize, totalWavBytesSize, std::vector<size_t>{cliOptions.runs}, std::vector<uint64_t>{totalWavBytesSize}, completedRunsBySpeaker, completedRunsByText, completedRunsByCombination);
    return 0;
}

struct SongBenchResult {
    std::string endpoint;
    double totalElapsedMilliseconds = 0.0;
    double firstMilliseconds = 0.0;
    double averageMilliseconds = 0.0;
    double averageWarmMilliseconds = 0.0;
    double minimumMilliseconds = 0.0;
    double maximumMilliseconds = 0.0;
    double averageFirstResponseMilliseconds = 0.0;
    double averageFirstBodyMilliseconds = 0.0;
    double throughputPerSecond = 0.0;
    size_t activeWorkers = 0;
    std::vector<size_t> completedRequestsByWorker;
    std::vector<double> averageMillisecondsByWorker;
    std::vector<double> averageFirstResponseMillisecondsByWorker;
    std::vector<double> averageFirstBodyMillisecondsByWorker;
    size_t bytes = 0;
    std::string repeatStatus;
    size_t uniqueShaCount = 0;
    std::string firstSha256;
    std::string lastSha256;
};

static std::string getNativeSingTeacherModeLabel(const CliOptions &cliOptions) {
    if (cliOptions.nativeSingTeacherMode.empty()
        || cliOptions.nativeSingTeacherMode == "vv-bin"
        || cliOptions.nativeSingTeacherMode == "vv_bin"
        || cliOptions.nativeSingTeacherMode == "stochastic") {
        return "vv-bin";
    }
    if (cliOptions.nativeSingTeacherMode == "deterministic"
        || cliOptions.nativeSingTeacherMode == "seeded_exported_onnx") {
        return "deterministic";
    }
    return cliOptions.nativeSingTeacherMode;
}

static SongBenchResult benchmarkSongOperation(const std::string &endpoint, size_t runCount, size_t scoreCount, const std::function<std::vector<uint8_t>(size_t)> &runOperation) {
    if (runCount == 0) {
        throw std::runtime_error("runCount は 1 以上が必要です");
    }
    SongBenchResult result;
    result.endpoint = endpoint;
    std::vector<double> elapsedValues;
    elapsedValues.reserve(runCount);
    std::set<std::string> sha256Values;
    std::vector<std::set<std::string>> sha256ValuesByScore(scoreCount);
    auto benchmarkStartTime = std::chrono::steady_clock::now();
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        auto startTime = std::chrono::steady_clock::now();
        std::vector<uint8_t> outputBytes = runOperation(runIndex);
        auto endTime = std::chrono::steady_clock::now();
        elapsedValues.push_back(getElapsedMilliseconds(startTime, endTime));
        std::string sha256Value = createSha256Hex(outputBytes.data(), outputBytes.size());
        sha256Values.insert(sha256Value);
        sha256ValuesByScore[runIndex % scoreCount].insert(sha256Value);
        if (runIndex == 0) {
            result.bytes = outputBytes.size();
            result.firstSha256 = sha256Value;
        }
        result.lastSha256 = sha256Value;
    }
    auto benchmarkEndTime = std::chrono::steady_clock::now();
    result.totalElapsedMilliseconds = getElapsedMilliseconds(benchmarkStartTime, benchmarkEndTime);
    result.firstMilliseconds = elapsedValues.front();
    result.minimumMilliseconds = *std::min_element(elapsedValues.begin(), elapsedValues.end());
    result.maximumMilliseconds = *std::max_element(elapsedValues.begin(), elapsedValues.end());
    result.averageMilliseconds = std::accumulate(elapsedValues.begin(), elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size());
    result.throughputPerSecond = result.totalElapsedMilliseconds > 0.0 ? static_cast<double>(runCount) * 1000.0 / result.totalElapsedMilliseconds : 0.0;
    result.activeWorkers = 1;
    result.completedRequestsByWorker = {runCount};
    result.averageMillisecondsByWorker = {result.averageMilliseconds};
    if (elapsedValues.size() > 1) {
        result.averageWarmMilliseconds = std::accumulate(elapsedValues.begin() + 1, elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size() - 1);
    } else {
        result.averageWarmMilliseconds = result.averageMilliseconds;
    }
    result.uniqueShaCount = sha256Values.size();
    result.repeatStatus = "exact";
    for (const std::set<std::string> &scoreShaValues : sha256ValuesByScore) {
        if (scoreShaValues.size() > 1) {
            result.repeatStatus = "different";
            break;
        }
    }
    return result;
}

static SongBenchResult benchmarkSongOperationParallel(const std::string &endpoint, size_t runCount, size_t scoreCount, std::vector<std::unique_ptr<RuntimeHolder>> &runtimeHolders, const std::function<std::vector<uint8_t>(RuntimeState &, size_t)> &runOperation) {
    if (runCount == 0) {
        throw std::runtime_error("runCount は 1 以上が必要です");
    }
    if (runtimeHolders.empty()) {
        throw std::runtime_error("runtime worker がありません");
    }
    SongBenchResult result;
    result.endpoint = endpoint;
    const size_t workerCount = runtimeHolders.size();
    std::vector<double> elapsedValues(runCount, 0.0);
    std::vector<size_t> outputByteSizes(runCount, 0);
    std::vector<std::string> sha256ByRun(runCount);
    result.completedRequestsByWorker.assign(workerCount, 0);
    result.averageMillisecondsByWorker.assign(workerCount, 0.0);
    std::atomic<size_t> nextRunIndex{0};
    std::atomic<bool> hasBenchError{false};
    std::string firstErrorMessage;
    std::mutex firstErrorMessageMutex;
    std::vector<std::thread> benchmarkThreads;
    benchmarkThreads.reserve(workerCount);
    auto benchmarkStartTime = std::chrono::steady_clock::now();
    for (size_t workerIndex = 0; workerIndex < workerCount; workerIndex++) {
        benchmarkThreads.emplace_back([&, workerIndex]() {
            RuntimeState &workerRuntimeState = *runtimeHolders[workerIndex]->runtimeState;
            size_t workerCompletedRequests = 0;
            double workerTotalElapsedMilliseconds = 0.0;
            while (!hasBenchError.load()) {
                size_t runIndex = nextRunIndex.fetch_add(1);
                if (runIndex >= runCount) {
                    break;
                }
                try {
                    auto startTime = std::chrono::steady_clock::now();
                    std::vector<uint8_t> outputBytes = runOperation(workerRuntimeState, runIndex);
                    auto endTime = std::chrono::steady_clock::now();
                    elapsedValues[runIndex] = getElapsedMilliseconds(startTime, endTime);
                    outputByteSizes[runIndex] = outputBytes.size();
                    sha256ByRun[runIndex] = createSha256Hex(outputBytes.data(), outputBytes.size());
                    workerCompletedRequests++;
                    workerTotalElapsedMilliseconds += elapsedValues[runIndex];
                } catch (const std::exception &caughtException) {
                    if (!hasBenchError.exchange(true)) {
                        std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                        firstErrorMessage = caughtException.what();
                    }
                } catch (...) {
                    if (!hasBenchError.exchange(true)) {
                        std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                        firstErrorMessage = endpoint + " の bench 実行中に不明なエラーが発生しました";
                    }
                }
            }
            result.completedRequestsByWorker[workerIndex] = workerCompletedRequests;
            if (workerCompletedRequests > 0) {
                result.averageMillisecondsByWorker[workerIndex] = workerTotalElapsedMilliseconds / static_cast<double>(workerCompletedRequests);
            }
        });
    }
    for (std::thread &benchmarkThread : benchmarkThreads) {
        benchmarkThread.join();
    }
    if (hasBenchError.load()) {
        throw std::runtime_error(firstErrorMessage);
    }
    auto benchmarkEndTime = std::chrono::steady_clock::now();
    std::set<std::string> sha256Values(sha256ByRun.begin(), sha256ByRun.end());
    std::vector<std::set<std::string>> sha256ValuesByScore(scoreCount);
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        sha256ValuesByScore[runIndex % scoreCount].insert(sha256ByRun[runIndex]);
    }
    result.totalElapsedMilliseconds = getElapsedMilliseconds(benchmarkStartTime, benchmarkEndTime);
    result.firstMilliseconds = elapsedValues.front();
    result.minimumMilliseconds = *std::min_element(elapsedValues.begin(), elapsedValues.end());
    result.maximumMilliseconds = *std::max_element(elapsedValues.begin(), elapsedValues.end());
    result.averageMilliseconds = std::accumulate(elapsedValues.begin(), elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size());
    if (elapsedValues.size() > 1) {
        result.averageWarmMilliseconds = std::accumulate(elapsedValues.begin() + 1, elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size() - 1);
    } else {
        result.averageWarmMilliseconds = result.averageMilliseconds;
    }
    result.throughputPerSecond = result.totalElapsedMilliseconds > 0.0 ? static_cast<double>(runCount) * 1000.0 / result.totalElapsedMilliseconds : 0.0;
    result.activeWorkers = countActiveBenchWorkers(result.completedRequestsByWorker);
    result.bytes = outputByteSizes.front();
    result.firstSha256 = sha256ByRun.front();
    result.lastSha256 = sha256ByRun.back();
    result.uniqueShaCount = sha256Values.size();
    result.repeatStatus = "exact";
    for (const std::set<std::string> &scoreShaValues : sha256ValuesByScore) {
        if (scoreShaValues.size() > 1) {
            result.repeatStatus = "different";
            break;
        }
    }
    return result;
}

static int runSongBenchCommand(const CliOptions &cliOptions) {
    std::vector<std::string> scoreTexts = getBenchScoreTexts(cliOptions);
    std::vector<std::string> frameAudioQueryTexts = getBenchFrameAudioQueryTexts(cliOptions, scoreTexts.size());
    std::vector<size_t> scoreCompletedRuns = createRoundRobinCounts(cliOptions.runs, scoreTexts.size());
    std::vector<SongBenchResult> results;
    if (cliOptions.workers == 1) {
        RuntimeState *runtimeState = new RuntimeState(createRuntimeState(cliOptions.runtimePaths, cliOptions.shouldPreload));
        if (cliOptions.frameAudioQueryPath.empty()) {
            frameAudioQueryTexts.reserve(scoreTexts.size());
            for (const std::string &scoreText : scoreTexts) {
                frameAudioQueryTexts.push_back(createSingFrameAudioQuery(*runtimeState, scoreText, cliOptions.speaker));
            }
        }
        results.push_back(benchmarkSongOperation("sing_frame_audio_query", cliOptions.runs, scoreTexts.size(), [runtimeState, &scoreTexts, &cliOptions](size_t runIndex) {
            return makeBodyBytes(createSingFrameAudioQuery(*runtimeState, scoreTexts[runIndex % scoreTexts.size()], cliOptions.speaker));
        }));
        results.push_back(benchmarkSongOperation("sing_frame_f0", cliOptions.runs, scoreTexts.size(), [runtimeState, &scoreTexts, &frameAudioQueryTexts, &cliOptions](size_t runIndex) {
            size_t scoreIndex = runIndex % scoreTexts.size();
            return makeBodyBytes(createSingFrameF0(*runtimeState, scoreTexts[scoreIndex], frameAudioQueryTexts[scoreIndex], cliOptions.speaker));
        }));
        results.push_back(benchmarkSongOperation("sing_frame_volume", cliOptions.runs, scoreTexts.size(), [runtimeState, &scoreTexts, &frameAudioQueryTexts, &cliOptions](size_t runIndex) {
            size_t scoreIndex = runIndex % scoreTexts.size();
            return makeBodyBytes(createSingFrameVolume(*runtimeState, scoreTexts[scoreIndex], frameAudioQueryTexts[scoreIndex], cliOptions.speaker));
        }));
        results.push_back(benchmarkSongOperation("frame_synthesis", cliOptions.runs, scoreTexts.size(), [runtimeState, &scoreTexts, &frameAudioQueryTexts, &cliOptions](size_t runIndex) {
            return synthesizeFrameAudioQuery(*runtimeState, frameAudioQueryTexts[runIndex % scoreTexts.size()], cliOptions.speaker);
        }));
    } else {
        std::map<uint32_t, size_t> sharedLoadedStyleCounts;
        std::mutex sharedLoadedStylesMutex;
        std::map<uint32_t, uint64_t> sharedStyleUnloadGenerations;
        std::mutex sharedStyleUnloadMutex;
        std::mutex sharedUserDictMutex;
        std::mutex sharedPresetMutex;
        std::mutex sharedSettingMutex;
        std::mutex sharedLibraryMutex;
        std::vector<std::unique_ptr<RuntimeHolder>> runtimeHolders = createRuntimeHolders(cliOptions, &sharedLoadedStyleCounts, &sharedLoadedStylesMutex, &sharedStyleUnloadGenerations, &sharedStyleUnloadMutex, &sharedUserDictMutex, &sharedPresetMutex, &sharedSettingMutex, &sharedLibraryMutex);
        if (cliOptions.frameAudioQueryPath.empty()) {
            frameAudioQueryTexts.reserve(scoreTexts.size());
            for (const std::string &scoreText : scoreTexts) {
                frameAudioQueryTexts.push_back(createSingFrameAudioQuery(*runtimeHolders.front()->runtimeState, scoreText, cliOptions.speaker));
            }
        }
        results.push_back(benchmarkSongOperationParallel("sing_frame_audio_query", cliOptions.runs, scoreTexts.size(), runtimeHolders, [&scoreTexts, &cliOptions](RuntimeState &runtimeState, size_t runIndex) {
            return makeBodyBytes(createSingFrameAudioQuery(runtimeState, scoreTexts[runIndex % scoreTexts.size()], cliOptions.speaker));
        }));
        results.push_back(benchmarkSongOperationParallel("sing_frame_f0", cliOptions.runs, scoreTexts.size(), runtimeHolders, [&scoreTexts, &frameAudioQueryTexts, &cliOptions](RuntimeState &runtimeState, size_t runIndex) {
            size_t scoreIndex = runIndex % scoreTexts.size();
            return makeBodyBytes(createSingFrameF0(runtimeState, scoreTexts[scoreIndex], frameAudioQueryTexts[scoreIndex], cliOptions.speaker));
        }));
        results.push_back(benchmarkSongOperationParallel("sing_frame_volume", cliOptions.runs, scoreTexts.size(), runtimeHolders, [&scoreTexts, &frameAudioQueryTexts, &cliOptions](RuntimeState &runtimeState, size_t runIndex) {
            size_t scoreIndex = runIndex % scoreTexts.size();
            return makeBodyBytes(createSingFrameVolume(runtimeState, scoreTexts[scoreIndex], frameAudioQueryTexts[scoreIndex], cliOptions.speaker));
        }));
        results.push_back(benchmarkSongOperationParallel("frame_synthesis", cliOptions.runs, scoreTexts.size(), runtimeHolders, [&scoreTexts, &frameAudioQueryTexts, &cliOptions](RuntimeState &runtimeState, size_t runIndex) {
            return synthesizeFrameAudioQuery(runtimeState, frameAudioQueryTexts[runIndex % scoreTexts.size()], cliOptions.speaker);
        }));
    }
    std::cout << "mode\tendpoint\truns\tworkers\tscore_mode\tscore_count\tscore_utf8_bytes\tscore_completed_runs\telapsed_ms\tthroughput_rps\tfirst_ms\tavg_ms\tavg_warm_ms\tmin_ms\tmax_ms\tactive_workers\tworker_completed_runs\tworker_avg_ms\tbytes\trepeat_status\tunique_sha_count\tsha256_first\tsha256_last\n";
    std::cout << std::fixed << std::setprecision(3);
    std::string modeText = getNativeSingTeacherModeLabel(cliOptions);
    for (const SongBenchResult &result : results) {
        std::cout << modeText << "\t"
                  << result.endpoint << "\t"
                  << cliOptions.runs << "\t"
                  << cliOptions.workers << "\t"
                  << getRoundRobinModeText(scoreTexts.size()) << "\t"
                  << scoreTexts.size() << "\t"
                  << joinTextByteLengths(scoreTexts) << "\t"
                  << joinSizeValues(scoreCompletedRuns) << "\t"
                  << result.totalElapsedMilliseconds << "\t"
                  << result.throughputPerSecond << "\t"
                  << result.firstMilliseconds << "\t"
                  << result.averageMilliseconds << "\t"
                  << result.averageWarmMilliseconds << "\t"
                  << result.minimumMilliseconds << "\t"
                  << result.maximumMilliseconds << "\t"
                  << result.activeWorkers << "\t"
                  << joinSizeValues(result.completedRequestsByWorker) << "\t"
                  << joinDoubleValues(result.averageMillisecondsByWorker) << "\t"
                  << result.bytes << "\t"
                  << result.repeatStatus << "\t"
                  << result.uniqueShaCount << "\t"
                  << result.firstSha256 << "\t"
                  << result.lastSha256 << "\n";
    }
    return 0;
}

struct HttpBenchResponse {
    int statusCode = 0;
    size_t responseBytes = 0;
    size_t bodyBytes = 0;
    double elapsedMilliseconds = 0.0;
    double firstResponseMilliseconds = 0.0;
    double firstBodyMilliseconds = 0.0;
    std::vector<uint8_t> bodyBytesData;
    std::string contentType;
};

struct ApiSessionLineSpec {
    std::string httpPath;
    std::vector<std::string> inputFields;
};

static std::string getCliAudioStreamFormatText(AudioStreamFormat audioStreamFormat) {
    if (audioStreamFormat == AudioStreamFormat::Pcm) {
        return "pcm";
    }
    return "wav";
}

static bool isUrlUnreservedByte(unsigned char byteValue) {
    return (byteValue >= 'A' && byteValue <= 'Z')
        || (byteValue >= 'a' && byteValue <= 'z')
        || (byteValue >= '0' && byteValue <= '9')
        || byteValue == '-'
        || byteValue == '.'
        || byteValue == '_'
        || byteValue == '~';
}

static std::string percentEncodeQueryValue(const std::string &plainText) {
    const char *hexDigits = "0123456789ABCDEF";
    std::string encodedText;
    for (unsigned char byteValue : plainText) {
        if (isUrlUnreservedByte(byteValue)) {
            encodedText.push_back(static_cast<char>(byteValue));
        } else {
            encodedText.push_back('%');
            encodedText.push_back(hexDigits[(byteValue >> 4) & 0x0f]);
            encodedText.push_back(hexDigits[byteValue & 0x0f]);
        }
    }
    return encodedText;
}

static std::string normalizeHttpBenchPath(const std::string &httpPath) {
    if (httpPath.empty()) {
        return "/tts";
    }
    if (httpPath.front() == '/') {
        return httpPath;
    }
    return "/" + httpPath;
}

static std::string getHttpBenchPathname(const std::string &httpPath) {
    std::string normalizedPath = normalizeHttpBenchPath(httpPath);
    size_t queryPosition = normalizedPath.find('?');
    if (queryPosition == std::string::npos) {
        return normalizedPath;
    }
    return normalizedPath.substr(0, queryPosition);
}

static bool hasHttpBenchQueryParameter(const std::string &targetPath, const std::string &parameterName) {
    size_t queryStart = targetPath.find('?');
    if (queryStart == std::string::npos) {
        return false;
    }
    size_t position = queryStart + 1;
    while (position <= targetPath.size()) {
        size_t separatorPosition = targetPath.find('&', position);
        std::string pairText = targetPath.substr(position, separatorPosition == std::string::npos ? std::string::npos : separatorPosition - position);
        size_t equalsPosition = pairText.find('=');
        std::string keyText = equalsPosition == std::string::npos ? pairText : pairText.substr(0, equalsPosition);
        if (keyText == parameterName) {
            return true;
        }
        if (separatorPosition == std::string::npos) {
            break;
        }
        position = separatorPosition + 1;
    }
    return false;
}

static std::string createHttpBenchRequestText(const CliOptions &cliOptions, const std::string &method, const std::string &targetPath, bool shouldKeepAlive, const std::vector<uint8_t> &bodyBytes, const std::string &contentType) {
    std::string requestText = method + " " + targetPath + " HTTP/1.1\r\n";
    requestText += "Host: " + cliOptions.httpHost + ":" + std::to_string(cliOptions.port) + "\r\n";
    requestText += "Connection: " + std::string(shouldKeepAlive ? "keep-alive" : "close") + "\r\n";
    if (!contentType.empty()) {
        requestText += "Content-Type: " + contentType + "\r\n";
    }
    if (!bodyBytes.empty()) {
        requestText += "Content-Length: " + std::to_string(bodyBytes.size()) + "\r\n";
    }
    requestText += "\r\n";
    requestText.append(reinterpret_cast<const char *>(bodyBytes.data()), bodyBytes.size());
    return requestText;
}

static std::string createHttpBenchTargetPath(const CliOptions &cliOptions, const std::string &httpPath, const std::string &textValue, uint32_t speakerId) {
    std::string targetPath = normalizeHttpBenchPath(httpPath);
    targetPath += targetPath.find('?') == std::string::npos ? "?" : "&";
    if (!hasHttpBenchQueryParameter(targetPath, "speaker")) {
        targetPath += "speaker=" + std::to_string(speakerId) + "&";
    }
    if (!hasHttpBenchQueryParameter(targetPath, "text")) {
        targetPath += "text=" + percentEncodeQueryValue(textValue) + "&";
    }
    if (!hasHttpBenchQueryParameter(targetPath, "format")) {
        targetPath += "format=" + getCliAudioStreamFormatText(cliOptions.audioStreamFormat) + "&";
    }
    if (targetPath.find("/tts_stream") == 0 && !hasHttpBenchQueryParameter(targetPath, "chunk_samples")) {
        targetPath += "chunk_samples=" + std::to_string(cliOptions.chunkSamples) + "&";
    }
    if (cliOptions.isKana && !hasHttpBenchQueryParameter(targetPath, "is_kana")) {
        targetPath += "is_kana=true&";
    }
    while (!targetPath.empty() && targetPath.back() == '&') {
        targetPath.pop_back();
    }
    return targetPath;
}

static LitevoxSocket openHttpBenchSocket(const std::string &httpHost, int port) {
    initializeSocketRuntime();
    addrinfo addressHints{};
    addressHints.ai_family = AF_UNSPEC;
    addressHints.ai_socktype = SOCK_STREAM;
    std::string portText = std::to_string(port);
    addrinfo *addressList = nullptr;
    int lookupCode = getaddrinfo(httpHost.c_str(), portText.c_str(), &addressHints, &addressList);
    if (lookupCode != 0) {
        throw std::runtime_error("HTTP bench host を解決できません: " + getAddrInfoErrorText(lookupCode));
    }
    for (addrinfo *addressPointer = addressList; addressPointer; addressPointer = addressPointer->ai_next) {
        LitevoxSocket socketDescriptor = socket(addressPointer->ai_family, addressPointer->ai_socktype, addressPointer->ai_protocol);
        if (!isValidSocket(socketDescriptor)) {
            continue;
        }
        if (connect(socketDescriptor, addressPointer->ai_addr, addressPointer->ai_addrlen) == 0) {
            freeaddrinfo(addressList);
            return socketDescriptor;
        }
        closeSocket(socketDescriptor);
    }
    freeaddrinfo(addressList);
    throw std::runtime_error("HTTP bench 接続に失敗しました: " + httpHost + ":" + portText);
}

static void writeHttpBenchRequest(LitevoxSocket socketDescriptor, const std::string &requestText) {
    const char *requestBytes = requestText.data();
    size_t writtenByteCount = 0;
    while (writtenByteCount < requestText.size()) {
        int currentWriteBytes = static_cast<int>(send(socketDescriptor, requestBytes + writtenByteCount, static_cast<int>(requestText.size() - writtenByteCount), 0));
        if (currentWriteBytes <= 0) {
            throw std::runtime_error("HTTP bench request 送信に失敗しました: " + getSocketErrorText());
        }
        writtenByteCount += static_cast<size_t>(currentWriteBytes);
    }
}

static std::map<std::string, std::string> parseHttpBenchHeaderMap(const std::string &headerText) {
    std::map<std::string, std::string> headers;
    std::istringstream headerStream(headerText);
    std::string headerLine;
    std::getline(headerStream, headerLine);
    while (std::getline(headerStream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        size_t colonPosition = headerLine.find(':');
        if (colonPosition == std::string::npos) {
            continue;
        }
        headers[lowercaseAscii(trimAscii(headerLine.substr(0, colonPosition)))] = trimAscii(headerLine.substr(colonPosition + 1));
    }
    return headers;
}

static size_t findHttpBenchChunkedResponseEnd(const std::string &responseText, size_t bodyOffset) {
    size_t cursor = bodyOffset;
    while (true) {
        size_t chunkHeaderEnd = responseText.find("\r\n", cursor);
        if (chunkHeaderEnd == std::string::npos) {
            return std::string::npos;
        }
        std::string chunkSizeText = trimAscii(responseText.substr(cursor, chunkHeaderEnd - cursor));
        size_t semicolonPosition = chunkSizeText.find(';');
        if (semicolonPosition != std::string::npos) {
            chunkSizeText = chunkSizeText.substr(0, semicolonPosition);
        }
        if (chunkSizeText.empty()) {
            throw std::runtime_error("HTTP chunk size が不正です");
        }
        size_t chunkSize = 0;
        std::stringstream sizeStream;
        sizeStream << std::hex << chunkSizeText;
        sizeStream >> chunkSize;
        if (!sizeStream || !sizeStream.eof()) {
            throw std::runtime_error("HTTP chunk size を読めません");
        }
        size_t chunkDataOffset = chunkHeaderEnd + 2;
        if (chunkSize == 0) {
            if (responseText.size() < chunkDataOffset + 2) {
                return std::string::npos;
            }
            return chunkDataOffset + 2;
        }
        size_t chunkEnd = chunkDataOffset + chunkSize;
        if (responseText.size() < chunkEnd + 2) {
            return std::string::npos;
        }
        if (responseText.compare(chunkEnd, 2, "\r\n") != 0) {
            throw std::runtime_error("HTTP chunk 終端が不正です");
        }
        cursor = chunkEnd + 2;
    }
}

static size_t findHttpBenchCompleteResponseEnd(const std::string &responseText, size_t headerEndPosition, const std::map<std::string, std::string> &headers) {
    size_t bodyOffset = headerEndPosition + 4;
    auto transferEncodingIterator = headers.find("transfer-encoding");
    if (transferEncodingIterator != headers.end() && lowercaseAscii(transferEncodingIterator->second).find("chunked") != std::string::npos) {
        return findHttpBenchChunkedResponseEnd(responseText, bodyOffset);
    }
    auto contentLengthIterator = headers.find("content-length");
    if (contentLengthIterator == headers.end()) {
        return std::string::npos;
    }
    size_t contentLength = static_cast<size_t>(std::stoull(contentLengthIterator->second));
    size_t responseEndPosition = bodyOffset + contentLength;
    if (responseText.size() < responseEndPosition) {
        return std::string::npos;
    }
    return responseEndPosition;
}

static std::string readHttpBenchResponseText(LitevoxSocket socketDescriptor, std::string &pendingResponseText, std::chrono::steady_clock::time_point responseStartTime, double *firstResponseMilliseconds, double *firstBodyMilliseconds) {
    char responseBuffer[32768];
    while (true) {
        size_t headerEndPosition = pendingResponseText.find("\r\n\r\n");
        if (headerEndPosition != std::string::npos) {
            std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(pendingResponseText.substr(0, headerEndPosition));
            size_t responseEndPosition = findHttpBenchCompleteResponseEnd(pendingResponseText, headerEndPosition, headers);
            if (responseEndPosition != std::string::npos) {
                std::string responseText = pendingResponseText.substr(0, responseEndPosition);
                pendingResponseText.erase(0, responseEndPosition);
                return responseText;
            }
        }
        int currentReadBytes = static_cast<int>(recv(socketDescriptor, responseBuffer, sizeof(responseBuffer), 0));
        if (currentReadBytes < 0) {
            throw std::runtime_error("HTTP bench response 受信に失敗しました: " + getSocketErrorText());
        }
        if (currentReadBytes == 0) {
            throw std::runtime_error("HTTP bench response が途中で切断されました");
        }
        auto currentReadTime = std::chrono::steady_clock::now();
        if (*firstResponseMilliseconds <= 0.0) {
            *firstResponseMilliseconds = getElapsedMilliseconds(responseStartTime, currentReadTime);
        }
        pendingResponseText.append(responseBuffer, static_cast<size_t>(currentReadBytes));
        if (*firstBodyMilliseconds <= 0.0) {
            size_t bodyHeaderEndPosition = pendingResponseText.find("\r\n\r\n");
            if (bodyHeaderEndPosition != std::string::npos && pendingResponseText.size() > bodyHeaderEndPosition + 4) {
                *firstBodyMilliseconds = getElapsedMilliseconds(responseStartTime, currentReadTime);
            }
        }
    }
}

static int parseHttpBenchStatusCode(const std::string &responseText) {
    size_t firstSpacePosition = responseText.find(' ');
    if (firstSpacePosition == std::string::npos || firstSpacePosition + 4 > responseText.size()) {
        return 0;
    }
    return std::stoi(responseText.substr(firstSpacePosition + 1, 3));
}

static size_t countHttpBenchBodyBytes(const std::string &responseText) {
    size_t headerEndPosition = responseText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        return 0;
    }
    std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(responseText.substr(0, headerEndPosition));
    auto transferEncodingIterator = headers.find("transfer-encoding");
    if (transferEncodingIterator != headers.end() && lowercaseAscii(transferEncodingIterator->second).find("chunked") != std::string::npos) {
        size_t cursor = headerEndPosition + 4;
        size_t bodyBytes = 0;
        while (true) {
            size_t chunkHeaderEnd = responseText.find("\r\n", cursor);
            if (chunkHeaderEnd == std::string::npos) {
                return bodyBytes;
            }
            std::string chunkSizeText = trimAscii(responseText.substr(cursor, chunkHeaderEnd - cursor));
            size_t semicolonPosition = chunkSizeText.find(';');
            if (semicolonPosition != std::string::npos) {
                chunkSizeText = chunkSizeText.substr(0, semicolonPosition);
            }
            size_t chunkSize = 0;
            std::stringstream sizeStream;
            sizeStream << std::hex << chunkSizeText;
            sizeStream >> chunkSize;
            if (!sizeStream || !sizeStream.eof()) {
                return bodyBytes;
            }
            if (chunkSize == 0) {
                return bodyBytes;
            }
            bodyBytes += chunkSize;
            cursor = chunkHeaderEnd + 2 + chunkSize + 2;
        }
    }
    auto contentLengthIterator = headers.find("content-length");
    if (contentLengthIterator == headers.end()) {
        return responseText.size() - headerEndPosition - 4;
    }
    return static_cast<size_t>(std::stoull(contentLengthIterator->second));
}

static std::vector<uint8_t> extractHttpBenchBodyBytes(const std::string &responseText) {
    size_t headerEndPosition = responseText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        return {};
    }
    std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(responseText.substr(0, headerEndPosition));
    auto transferEncodingIterator = headers.find("transfer-encoding");
    if (transferEncodingIterator != headers.end() && lowercaseAscii(transferEncodingIterator->second).find("chunked") != std::string::npos) {
        std::vector<uint8_t> bodyBytes;
        size_t cursor = headerEndPosition + 4;
        while (true) {
            size_t chunkHeaderEnd = responseText.find("\r\n", cursor);
            if (chunkHeaderEnd == std::string::npos) {
                return bodyBytes;
            }
            std::string chunkSizeText = trimAscii(responseText.substr(cursor, chunkHeaderEnd - cursor));
            size_t semicolonPosition = chunkSizeText.find(';');
            if (semicolonPosition != std::string::npos) {
                chunkSizeText = chunkSizeText.substr(0, semicolonPosition);
            }
            size_t chunkSize = 0;
            std::stringstream sizeStream;
            sizeStream << std::hex << chunkSizeText;
            sizeStream >> chunkSize;
            if (!sizeStream || !sizeStream.eof() || chunkSize == 0) {
                return bodyBytes;
            }
            size_t chunkDataOffset = chunkHeaderEnd + 2;
            bodyBytes.insert(bodyBytes.end(), responseText.begin() + static_cast<std::string::difference_type>(chunkDataOffset), responseText.begin() + static_cast<std::string::difference_type>(chunkDataOffset + chunkSize));
            cursor = chunkDataOffset + chunkSize + 2;
        }
    }
    return std::vector<uint8_t>(responseText.begin() + static_cast<std::string::difference_type>(headerEndPosition + 4), responseText.end());
}

static std::string getHttpBenchContentType(const std::string &responseText) {
    size_t headerEndPosition = responseText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        return "";
    }
    std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(responseText.substr(0, headerEndPosition));
    auto contentTypeIterator = headers.find("content-type");
    if (contentTypeIterator == headers.end()) {
        return "";
    }
    return lowercaseAscii(contentTypeIterator->second);
}

static HttpBenchResponse requestHttpBenchTarget(const CliOptions &cliOptions, const std::string &targetPath) {
    LitevoxSocket socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
    try {
        std::string requestText = createHttpBenchRequestText(cliOptions, "GET", targetPath, false, {}, "");
        writeHttpBenchRequest(socketDescriptor, requestText);
        auto responseStartTime = std::chrono::steady_clock::now();
        double firstResponseMilliseconds = 0.0;
        double firstBodyMilliseconds = 0.0;
        std::string pendingResponseText;
        std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
        auto responseEndTime = std::chrono::steady_clock::now();
        closeSocket(socketDescriptor);
        HttpBenchResponse benchResponse;
        benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
        benchResponse.responseBytes = responseText.size();
        benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
        benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
        benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
        benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
        benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
        benchResponse.contentType = getHttpBenchContentType(responseText);
        return benchResponse;
    } catch (...) {
        closeSocket(socketDescriptor);
        throw;
    }
}

static HttpBenchResponse requestHttpBenchTargetKeepAlive(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const std::string &targetPath) {
    std::string requestText = createHttpBenchRequestText(cliOptions, "GET", targetPath, true, {}, "");
    writeHttpBenchRequest(socketDescriptor, requestText);
    auto responseStartTime = std::chrono::steady_clock::now();
    double firstResponseMilliseconds = 0.0;
    double firstBodyMilliseconds = 0.0;
    std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
    auto responseEndTime = std::chrono::steady_clock::now();
    HttpBenchResponse benchResponse;
    benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
    benchResponse.responseBytes = responseText.size();
    benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
    benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
    benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
    benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
    benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
    benchResponse.contentType = getHttpBenchContentType(responseText);
    return benchResponse;
}

static HttpBenchResponse requestHttpBenchPost(const CliOptions &cliOptions, const std::string &targetPath, const std::vector<uint8_t> &bodyBytes, const std::string &contentType) {
    LitevoxSocket socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
    try {
        std::string requestText = createHttpBenchRequestText(cliOptions, "POST", targetPath, false, bodyBytes, contentType);
        writeHttpBenchRequest(socketDescriptor, requestText);
        auto responseStartTime = std::chrono::steady_clock::now();
        double firstResponseMilliseconds = 0.0;
        double firstBodyMilliseconds = 0.0;
        std::string pendingResponseText;
        std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
        auto responseEndTime = std::chrono::steady_clock::now();
        closeSocket(socketDescriptor);
        HttpBenchResponse benchResponse;
        benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
        benchResponse.responseBytes = responseText.size();
        benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
        benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
        benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
        benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
        benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
        benchResponse.contentType = getHttpBenchContentType(responseText);
        return benchResponse;
    } catch (...) {
        closeSocket(socketDescriptor);
        throw;
    }
}

static HttpBenchResponse requestHttpBenchPostKeepAlive(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const std::string &targetPath, const std::vector<uint8_t> &bodyBytes, const std::string &contentType) {
    std::string requestText = createHttpBenchRequestText(cliOptions, "POST", targetPath, true, bodyBytes, contentType);
    writeHttpBenchRequest(socketDescriptor, requestText);
    auto responseStartTime = std::chrono::steady_clock::now();
    double firstResponseMilliseconds = 0.0;
    double firstBodyMilliseconds = 0.0;
    std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
    auto responseEndTime = std::chrono::steady_clock::now();
    HttpBenchResponse benchResponse;
    benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
    benchResponse.responseBytes = responseText.size();
    benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
    benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
    benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
    benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
    benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
    benchResponse.contentType = getHttpBenchContentType(responseText);
    return benchResponse;
}

static std::string createHttpSongBenchTargetPath(const std::string &httpPath, uint32_t speakerId, AudioStreamFormat audioStreamFormat) {
    std::string targetPath = normalizeHttpBenchPath(httpPath);
    targetPath += targetPath.find('?') == std::string::npos ? "?" : "&";
    if (!hasHttpBenchQueryParameter(targetPath, "speaker")) {
        targetPath += "speaker=" + std::to_string(speakerId) + "&";
    }
    if (targetPath.find("/frame_synthesis") == 0 && !hasHttpBenchQueryParameter(targetPath, "format")) {
        targetPath += "format=" + getCliAudioStreamFormatText(audioStreamFormat) + "&";
    }
    while (!targetPath.empty() && targetPath.back() == '&') {
        targetPath.pop_back();
    }
    return targetPath;
}

static std::string createSongBenchRequestBody(const std::string &scoreText, const std::string &frameAudioQueryText) {
    return std::string("{\"score\":") + scoreText + ",\"frame_audio_query\":" + frameAudioQueryText + "}";
}

static std::string detectHttpSongBenchMode(const CliOptions &cliOptions) {
    try {
        HttpBenchResponse runtimeInfoResponse = requestHttpBenchTarget(cliOptions, "/runtime_info");
        if (runtimeInfoResponse.statusCode != 200) {
            return "unknown";
        }
        std::string runtimeInfoText(runtimeInfoResponse.bodyBytesData.begin(), runtimeInfoResponse.bodyBytesData.end());
        size_t nativeSingTeacherPosition = runtimeInfoText.find("\"native_sing_teacher\":{");
        if (nativeSingTeacherPosition == std::string::npos) {
            return "unknown";
        }
        size_t modeFieldPosition = runtimeInfoText.find("\"mode\":\"", nativeSingTeacherPosition);
        if (modeFieldPosition == std::string::npos) {
            return "unknown";
        }
        modeFieldPosition += std::string("\"mode\":\"").size();
        size_t modeFieldEnd = runtimeInfoText.find('"', modeFieldPosition);
        if (modeFieldEnd == std::string::npos) {
            return "unknown";
        }
        std::string modeValue = runtimeInfoText.substr(modeFieldPosition, modeFieldEnd - modeFieldPosition);
        if (modeValue == "seeded_exported_onnx") {
            return "deterministic";
        }
        if (modeValue == "vv_bin") {
            return "vv-bin";
        }
        return modeValue;
    } catch (...) {
        return "unknown";
    }
}

static SongBenchResult benchmarkSongHttpOperation(const std::string &endpoint, size_t runCount, size_t workerCount, size_t scoreCount, const std::function<HttpBenchResponse(size_t, LitevoxSocket *, std::string &)> &runOperation) {
    if (runCount == 0) {
        throw std::runtime_error("runCount は 1 以上が必要です");
    }
    if (workerCount == 0) {
        throw std::runtime_error("workerCount は 1 以上が必要です");
    }
    SongBenchResult result;
    result.endpoint = endpoint;
    std::vector<double> elapsedValues(runCount, 0.0);
    std::vector<double> firstResponseValues(runCount, 0.0);
    std::vector<double> firstBodyValues(runCount, 0.0);
    std::vector<size_t> bodyByteSizes(runCount, 0);
    std::vector<std::string> sha256ByRun(runCount);
    result.completedRequestsByWorker.assign(workerCount, 0);
    result.averageMillisecondsByWorker.assign(workerCount, 0.0);
    result.averageFirstResponseMillisecondsByWorker.assign(workerCount, 0.0);
    result.averageFirstBodyMillisecondsByWorker.assign(workerCount, 0.0);
    std::atomic<size_t> nextRunIndex{0};
    std::atomic<bool> hasBenchError{false};
    std::string firstErrorMessage;
    std::mutex firstErrorMessageMutex;
    std::vector<std::thread> benchmarkThreads;
    benchmarkThreads.reserve(workerCount);
    auto benchmarkStartTime = std::chrono::steady_clock::now();
    for (size_t workerIndex = 0; workerIndex < workerCount; workerIndex++) {
        benchmarkThreads.emplace_back([&, workerIndex]() {
            LitevoxSocket socketDescriptor = static_cast<LitevoxSocket>(-1);
            std::string pendingResponseText;
            size_t workerCompletedRequests = 0;
            double workerTotalElapsedMilliseconds = 0.0;
            double workerTotalFirstResponseMilliseconds = 0.0;
            double workerTotalFirstBodyMilliseconds = 0.0;
            try {
                while (!hasBenchError.load()) {
                    size_t runIndex = nextRunIndex.fetch_add(1);
                    if (runIndex >= runCount) {
                        break;
                    }
                    HttpBenchResponse benchResponse = runOperation(runIndex, &socketDescriptor, pendingResponseText);
                    if (benchResponse.statusCode != 200) {
                        throw std::runtime_error(endpoint + " が失敗しました: status=" + std::to_string(benchResponse.statusCode));
                    }
                    elapsedValues[runIndex] = benchResponse.elapsedMilliseconds;
                    firstResponseValues[runIndex] = benchResponse.firstResponseMilliseconds;
                    firstBodyValues[runIndex] = benchResponse.firstBodyMilliseconds;
                    bodyByteSizes[runIndex] = benchResponse.bodyBytesData.size();
                    sha256ByRun[runIndex] = createSha256Hex(benchResponse.bodyBytesData.data(), benchResponse.bodyBytesData.size());
                    workerCompletedRequests++;
                    workerTotalElapsedMilliseconds += benchResponse.elapsedMilliseconds;
                    workerTotalFirstResponseMilliseconds += benchResponse.firstResponseMilliseconds;
                    workerTotalFirstBodyMilliseconds += benchResponse.firstBodyMilliseconds;
                }
            } catch (const std::exception &caughtException) {
                if (!hasBenchError.exchange(true)) {
                    std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                    firstErrorMessage = caughtException.what();
                }
            } catch (...) {
                if (!hasBenchError.exchange(true)) {
                    std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                    firstErrorMessage = endpoint + " の bench 実行中に不明なエラーが発生しました";
                }
            }
            if (isValidSocket(socketDescriptor)) {
                closeSocket(socketDescriptor);
            }
            result.completedRequestsByWorker[workerIndex] = workerCompletedRequests;
            if (workerCompletedRequests > 0) {
                result.averageMillisecondsByWorker[workerIndex] = workerTotalElapsedMilliseconds / static_cast<double>(workerCompletedRequests);
                result.averageFirstResponseMillisecondsByWorker[workerIndex] = workerTotalFirstResponseMilliseconds / static_cast<double>(workerCompletedRequests);
                result.averageFirstBodyMillisecondsByWorker[workerIndex] = workerTotalFirstBodyMilliseconds / static_cast<double>(workerCompletedRequests);
            }
        });
    }
    for (std::thread &benchmarkThread : benchmarkThreads) {
        benchmarkThread.join();
    }
    if (hasBenchError.load()) {
        throw std::runtime_error(firstErrorMessage);
    }
    auto benchmarkEndTime = std::chrono::steady_clock::now();
    std::set<std::string> sha256Values(sha256ByRun.begin(), sha256ByRun.end());
    std::vector<std::set<std::string>> sha256ValuesByScore(scoreCount);
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        sha256ValuesByScore[runIndex % scoreCount].insert(sha256ByRun[runIndex]);
    }
    result.totalElapsedMilliseconds = getElapsedMilliseconds(benchmarkStartTime, benchmarkEndTime);
    result.firstMilliseconds = elapsedValues.front();
    result.minimumMilliseconds = *std::min_element(elapsedValues.begin(), elapsedValues.end());
    result.maximumMilliseconds = *std::max_element(elapsedValues.begin(), elapsedValues.end());
    result.averageMilliseconds = std::accumulate(elapsedValues.begin(), elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size());
    result.averageFirstResponseMilliseconds = std::accumulate(firstResponseValues.begin(), firstResponseValues.end(), 0.0) / static_cast<double>(firstResponseValues.size());
    result.averageFirstBodyMilliseconds = std::accumulate(firstBodyValues.begin(), firstBodyValues.end(), 0.0) / static_cast<double>(firstBodyValues.size());
    result.throughputPerSecond = result.totalElapsedMilliseconds > 0.0 ? static_cast<double>(runCount) * 1000.0 / result.totalElapsedMilliseconds : 0.0;
    result.activeWorkers = countActiveBenchWorkers(result.completedRequestsByWorker);
    result.bytes = bodyByteSizes.front();
    result.firstSha256 = sha256ByRun.front();
    result.lastSha256 = sha256ByRun.back();
    if (elapsedValues.size() > 1) {
        result.averageWarmMilliseconds = std::accumulate(elapsedValues.begin() + 1, elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size() - 1);
    } else {
        result.averageWarmMilliseconds = result.averageMilliseconds;
    }
    result.uniqueShaCount = sha256Values.size();
    result.repeatStatus = "exact";
    for (const std::set<std::string> &scoreShaValues : sha256ValuesByScore) {
        if (scoreShaValues.size() > 1) {
            result.repeatStatus = "different";
            break;
        }
    }
    return result;
}

static int runHttpSongBenchCommand(const CliOptions &cliOptions) {
    if (cliOptions.port <= 0) {
        throw std::runtime_error("--port は 1 以上が必要です");
    }
    std::vector<std::string> scoreTexts = getBenchScoreTexts(cliOptions);
    std::vector<std::string> frameAudioQueryTexts = getBenchFrameAudioQueryTexts(cliOptions, scoreTexts.size());
    std::vector<size_t> scoreCompletedRuns = createRoundRobinCounts(cliOptions.runs, scoreTexts.size());
    std::vector<std::vector<uint8_t>> scoreBytesList;
    scoreBytesList.reserve(scoreTexts.size());
    for (const std::string &scoreText : scoreTexts) {
        scoreBytesList.push_back(makeBodyBytes(scoreText));
    }
    if (cliOptions.frameAudioQueryPath.empty()) {
        for (const std::vector<uint8_t> &scoreBytes : scoreBytesList) {
            HttpBenchResponse frameAudioQueryResponse = requestHttpBenchPost(
                cliOptions,
                createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat),
                scoreBytes,
                "application/json");
            if (frameAudioQueryResponse.statusCode != 200) {
                throw std::runtime_error("sing_frame_audio_query の初期化に失敗しました: status=" + std::to_string(frameAudioQueryResponse.statusCode));
            }
            frameAudioQueryTexts.emplace_back(frameAudioQueryResponse.bodyBytesData.begin(), frameAudioQueryResponse.bodyBytesData.end());
        }
    }
    std::vector<std::vector<uint8_t>> frameAudioQueryBytesList;
    std::vector<std::vector<uint8_t>> pairBodyBytesList;
    frameAudioQueryBytesList.reserve(frameAudioQueryTexts.size());
    pairBodyBytesList.reserve(frameAudioQueryTexts.size());
    for (size_t scoreIndex = 0; scoreIndex < scoreTexts.size(); scoreIndex++) {
        frameAudioQueryBytesList.push_back(makeBodyBytes(frameAudioQueryTexts[scoreIndex]));
        pairBodyBytesList.push_back(makeBodyBytes(createSongBenchRequestBody(scoreTexts[scoreIndex], frameAudioQueryTexts[scoreIndex])));
    }
    std::vector<SongBenchResult> results;
    results.push_back(benchmarkSongHttpOperation("sing_frame_audio_query", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &scoreBytes = scoreBytesList[runIndex % scoreBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat), scoreBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat), scoreBytes, "application/json");
    }));
    results.push_back(benchmarkSongHttpOperation("sing_frame_f0", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &pairBodyBytes = pairBodyBytesList[runIndex % pairBodyBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/sing_frame_f0", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/sing_frame_f0", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
    }));
    results.push_back(benchmarkSongHttpOperation("sing_frame_volume", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &pairBodyBytes = pairBodyBytesList[runIndex % pairBodyBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/sing_frame_volume", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/sing_frame_volume", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
    }));
    results.push_back(benchmarkSongHttpOperation("frame_synthesis", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &frameAudioQueryBytes = frameAudioQueryBytesList[runIndex % frameAudioQueryBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/frame_synthesis", cliOptions.speaker, cliOptions.audioStreamFormat), frameAudioQueryBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/frame_synthesis", cliOptions.speaker, cliOptions.audioStreamFormat), frameAudioQueryBytes, "application/json");
    }));
    std::cout << "mode\tendpoint\truns\tworkers\tkeep_alive\tscore_mode\tscore_count\tscore_utf8_bytes\tscore_completed_requests\telapsed_ms\tthroughput_rps\tfirst_ms\tavg_ms\tavg_warm_ms\tmin_ms\tmax_ms\tavg_first_response_ms\tavg_first_body_ms\tactive_workers\tworker_completed_requests\tworker_avg_ms\tworker_avg_first_response_ms\tworker_avg_first_body_ms\tbytes\trepeat_status\tunique_sha_count\tsha256_first\tsha256_last\n";
    std::cout << std::fixed << std::setprecision(3);
    std::string modeText = detectHttpSongBenchMode(cliOptions);
    for (const SongBenchResult &result : results) {
        std::cout << modeText << "\t"
                  << result.endpoint << "\t"
                  << cliOptions.runs << "\t"
                  << cliOptions.workers << "\t"
                  << (cliOptions.httpKeepAlive ? "yes" : "no") << "\t"
                  << getRoundRobinModeText(scoreTexts.size()) << "\t"
                  << scoreTexts.size() << "\t"
                  << joinTextByteLengths(scoreTexts) << "\t"
                  << joinSizeValues(scoreCompletedRuns) << "\t"
                  << result.totalElapsedMilliseconds << "\t"
                  << result.throughputPerSecond << "\t"
                  << result.firstMilliseconds << "\t"
                  << result.averageMilliseconds << "\t"
                  << result.averageWarmMilliseconds << "\t"
                  << result.minimumMilliseconds << "\t"
                  << result.maximumMilliseconds << "\t"
                  << result.averageFirstResponseMilliseconds << "\t"
                  << result.averageFirstBodyMilliseconds << "\t"
                  << result.activeWorkers << "\t"
                  << joinSizeValues(result.completedRequestsByWorker) << "\t"
                  << joinDoubleValues(result.averageMillisecondsByWorker) << "\t"
                  << joinDoubleValues(result.averageFirstResponseMillisecondsByWorker) << "\t"
                  << joinDoubleValues(result.averageFirstBodyMillisecondsByWorker) << "\t"
                  << result.bytes << "\t"
                  << result.repeatStatus << "\t"
                  << result.uniqueShaCount << "\t"
                  << result.firstSha256 << "\t"
                  << result.lastSha256 << "\n";
    }
    return 0;
}

static int runHttpBenchCommand(const CliOptions &cliOptions) {
    std::vector<std::string> benchTexts = getBenchTexts(cliOptions);
    if (benchTexts.empty() || benchTexts.front().empty()) {
        throw std::runtime_error("--text が必要です");
    }
    if (cliOptions.port <= 0) {
        throw std::runtime_error("--port は 1 以上が必要です");
    }
    std::vector<uint32_t> benchSpeakerIds = getBenchSpeakerIds(cliOptions);
    std::vector<std::string> benchHttpPaths = getBenchHttpPaths(cliOptions);
    std::atomic<size_t> nextRunIndex{0};
    std::atomic<size_t> completedRequestCount{0};
    std::atomic<size_t> failedRequestCount{0};
    std::atomic<size_t> bodyBytesSize{0};
    std::atomic<uint64_t> totalBodyBytesSize{0};
    std::atomic<bool> hasBenchError{false};
    std::string firstErrorMessage;
    std::mutex firstErrorMessageMutex;
    std::vector<size_t> completedRequestsByWorker(cliOptions.workers, 0);
    std::vector<size_t> failedRequestsByWorker(cliOptions.workers, 0);
    std::vector<uint64_t> totalBodyBytesByWorker(cliOptions.workers, 0);
    std::vector<size_t> completedRequestsBySpeaker(benchSpeakerIds.size(), 0);
    std::vector<size_t> failedRequestsBySpeaker(benchSpeakerIds.size(), 0);
    std::vector<size_t> completedRequestsByText(benchTexts.size(), 0);
    std::vector<size_t> failedRequestsByText(benchTexts.size(), 0);
    std::vector<size_t> completedRequestsByPath(benchHttpPaths.size(), 0);
    std::vector<size_t> failedRequestsByPath(benchHttpPaths.size(), 0);
    std::vector<size_t> completedRequestsByCombination(getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size()), 0);
    std::vector<size_t> failedRequestsByCombination(getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size()), 0);
    std::mutex speakerCountMutex;
    std::vector<double> totalElapsedMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> totalFirstResponseMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> totalFirstBodyMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<std::thread> benchThreads;
    benchThreads.reserve(cliOptions.workers);
    auto requestStartTime = std::chrono::steady_clock::now();
    for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
        benchThreads.emplace_back([&, workerIndex]() {
            size_t workerCompletedRequests = 0;
            size_t workerFailedRequests = 0;
            uint64_t workerTotalBodyBytes = 0;
            LitevoxSocket socketDescriptor = static_cast<LitevoxSocket>(-1);
            std::string pendingResponseText;
            std::vector<size_t> workerCompletedRequestsBySpeaker(benchSpeakerIds.size(), 0);
            std::vector<size_t> workerFailedRequestsBySpeaker(benchSpeakerIds.size(), 0);
            std::vector<size_t> workerCompletedRequestsByText(benchTexts.size(), 0);
            std::vector<size_t> workerFailedRequestsByText(benchTexts.size(), 0);
            std::vector<size_t> workerCompletedRequestsByPath(benchHttpPaths.size(), 0);
            std::vector<size_t> workerFailedRequestsByPath(benchHttpPaths.size(), 0);
            std::vector<size_t> workerCompletedRequestsByCombination(completedRequestsByCombination.size(), 0);
            std::vector<size_t> workerFailedRequestsByCombination(failedRequestsByCombination.size(), 0);
            double workerTotalElapsedMilliseconds = 0.0;
            double workerTotalFirstResponseMilliseconds = 0.0;
            double workerTotalFirstBodyMilliseconds = 0.0;
            while (true) {
                size_t runIndex = nextRunIndex.fetch_add(1);
                if (runIndex >= cliOptions.runs) {
                    break;
                }
                BenchCaseSelection selection = getBenchCaseSelection(runIndex, benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size());
                std::string targetPath = createHttpBenchTargetPath(cliOptions, benchHttpPaths[selection.pathIndex], benchTexts[selection.textIndex], benchSpeakerIds[selection.speakerIndex]);
                try {
                    HttpBenchResponse benchResponse;
                    if (cliOptions.httpKeepAlive) {
                        if (!isValidSocket(socketDescriptor)) {
                            socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                            pendingResponseText.clear();
                        }
                        benchResponse = requestHttpBenchTargetKeepAlive(cliOptions, socketDescriptor, pendingResponseText, targetPath);
                    } else {
                        benchResponse = requestHttpBenchTarget(cliOptions, targetPath);
                    }
                    if (benchResponse.statusCode == 200) {
                        completedRequestCount.fetch_add(1);
                        bodyBytesSize.store(benchResponse.bodyBytes);
                        totalBodyBytesSize.fetch_add(static_cast<uint64_t>(benchResponse.bodyBytes));
                        workerCompletedRequests++;
                        workerTotalBodyBytes += static_cast<uint64_t>(benchResponse.bodyBytes);
                        workerTotalElapsedMilliseconds += benchResponse.elapsedMilliseconds;
                        workerTotalFirstResponseMilliseconds += benchResponse.firstResponseMilliseconds;
                        workerTotalFirstBodyMilliseconds += benchResponse.firstBodyMilliseconds;
                        workerCompletedRequestsBySpeaker[selection.speakerIndex]++;
                        workerCompletedRequestsByText[selection.textIndex]++;
                        workerCompletedRequestsByPath[selection.pathIndex]++;
                        workerCompletedRequestsByCombination[selection.combinationIndex]++;
                    } else {
                        failedRequestCount.fetch_add(1);
                        workerFailedRequests++;
                        workerFailedRequestsBySpeaker[selection.speakerIndex]++;
                        workerFailedRequestsByText[selection.textIndex]++;
                        workerFailedRequestsByPath[selection.pathIndex]++;
                        workerFailedRequestsByCombination[selection.combinationIndex]++;
                    }
                } catch (const std::exception &caughtException) {
                    if (isValidSocket(socketDescriptor)) {
                        closeSocket(socketDescriptor);
                        socketDescriptor = static_cast<LitevoxSocket>(-1);
                    }
                    pendingResponseText.clear();
                    failedRequestCount.fetch_add(1);
                    workerFailedRequests++;
                    workerFailedRequestsBySpeaker[selection.speakerIndex]++;
                    workerFailedRequestsByText[selection.textIndex]++;
                    workerFailedRequestsByPath[selection.pathIndex]++;
                    workerFailedRequestsByCombination[selection.combinationIndex]++;
                    if (!hasBenchError.exchange(true)) {
                        std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                        firstErrorMessage = caughtException.what();
                    }
                } catch (...) {
                    if (isValidSocket(socketDescriptor)) {
                        closeSocket(socketDescriptor);
                        socketDescriptor = static_cast<LitevoxSocket>(-1);
                    }
                    pendingResponseText.clear();
                    failedRequestCount.fetch_add(1);
                    workerFailedRequests++;
                    workerFailedRequestsBySpeaker[selection.speakerIndex]++;
                    workerFailedRequestsByText[selection.textIndex]++;
                    workerFailedRequestsByPath[selection.pathIndex]++;
                    workerFailedRequestsByCombination[selection.combinationIndex]++;
                    if (!hasBenchError.exchange(true)) {
                        std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                        firstErrorMessage = "HTTP bench 実行中に不明なエラーが発生しました";
                    }
                }
            }
            if (isValidSocket(socketDescriptor)) {
                closeSocket(socketDescriptor);
            }
            completedRequestsByWorker[workerIndex] = workerCompletedRequests;
            failedRequestsByWorker[workerIndex] = workerFailedRequests;
            totalBodyBytesByWorker[workerIndex] = workerTotalBodyBytes;
            totalElapsedMillisecondsByWorker[workerIndex] = workerTotalElapsedMilliseconds;
            totalFirstResponseMillisecondsByWorker[workerIndex] = workerTotalFirstResponseMilliseconds;
            totalFirstBodyMillisecondsByWorker[workerIndex] = workerTotalFirstBodyMilliseconds;
            std::lock_guard<std::mutex> speakerCountLock(speakerCountMutex);
            for (size_t speakerIndex = 0; speakerIndex < benchSpeakerIds.size(); speakerIndex++) {
                completedRequestsBySpeaker[speakerIndex] += workerCompletedRequestsBySpeaker[speakerIndex];
                failedRequestsBySpeaker[speakerIndex] += workerFailedRequestsBySpeaker[speakerIndex];
            }
            for (size_t textIndex = 0; textIndex < benchTexts.size(); textIndex++) {
                completedRequestsByText[textIndex] += workerCompletedRequestsByText[textIndex];
                failedRequestsByText[textIndex] += workerFailedRequestsByText[textIndex];
            }
            for (size_t pathIndex = 0; pathIndex < benchHttpPaths.size(); pathIndex++) {
                completedRequestsByPath[pathIndex] += workerCompletedRequestsByPath[pathIndex];
                failedRequestsByPath[pathIndex] += workerFailedRequestsByPath[pathIndex];
            }
            for (size_t combinationIndex = 0; combinationIndex < completedRequestsByCombination.size(); combinationIndex++) {
                completedRequestsByCombination[combinationIndex] += workerCompletedRequestsByCombination[combinationIndex];
                failedRequestsByCombination[combinationIndex] += workerFailedRequestsByCombination[combinationIndex];
            }
        });
    }
    for (std::thread &benchThread : benchThreads) {
        benchThread.join();
    }
    auto requestEndTime = std::chrono::steady_clock::now();
    size_t completedRequests = completedRequestCount.load();
    size_t failedRequests = failedRequestCount.load();
    if (completedRequests == 0 && failedRequests > 0 && !firstErrorMessage.empty()) {
        throw std::runtime_error(firstErrorMessage);
    }
    double requestMilliseconds = getElapsedMilliseconds(requestStartTime, requestEndTime);
    double requestThroughputPerSecond = requestMilliseconds > 0.0 ? static_cast<double>(completedRequests + failedRequests) * 1000.0 / requestMilliseconds : 0.0;
    double completedThroughputPerSecond = requestMilliseconds > 0.0 ? static_cast<double>(completedRequests) * 1000.0 / requestMilliseconds : 0.0;
    double totalElapsedMilliseconds = 0.0;
    double totalFirstResponseMilliseconds = 0.0;
    double totalFirstBodyMilliseconds = 0.0;
    std::vector<double> averageElapsedMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> averageFirstResponseMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> averageFirstBodyMillisecondsByWorker(cliOptions.workers, 0.0);
    for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
        totalElapsedMilliseconds += totalElapsedMillisecondsByWorker[workerIndex];
        totalFirstResponseMilliseconds += totalFirstResponseMillisecondsByWorker[workerIndex];
        totalFirstBodyMilliseconds += totalFirstBodyMillisecondsByWorker[workerIndex];
        if (completedRequestsByWorker[workerIndex] == 0) {
            continue;
        }
        double completedRequestsPerWorker = static_cast<double>(completedRequestsByWorker[workerIndex]);
        averageElapsedMillisecondsByWorker[workerIndex] = totalElapsedMillisecondsByWorker[workerIndex] / completedRequestsPerWorker;
        averageFirstResponseMillisecondsByWorker[workerIndex] = totalFirstResponseMillisecondsByWorker[workerIndex] / completedRequestsPerWorker;
        averageFirstBodyMillisecondsByWorker[workerIndex] = totalFirstBodyMillisecondsByWorker[workerIndex] / completedRequestsPerWorker;
    }
    double averageElapsedMilliseconds = completedRequests > 0 ? totalElapsedMilliseconds / static_cast<double>(completedRequests) : 0.0;
    double averageFirstResponseMilliseconds = completedRequests > 0 ? totalFirstResponseMilliseconds / static_cast<double>(completedRequests) : 0.0;
    double averageFirstBodyMilliseconds = completedRequests > 0 ? totalFirstBodyMilliseconds / static_cast<double>(completedRequests) : 0.0;
    size_t combinationCycle = getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size());
    std::string targetPath = benchHttpPaths.size() > 1 || benchTexts.size() > 1
        ? "(mixed)"
        : createHttpBenchTargetPath(cliOptions, benchHttpPaths.front(), benchTexts.front(), benchSpeakerIds.front());
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "host\t" << cliOptions.httpHost << "\n";
    std::cout << "port\t" << cliOptions.port << "\n";
    std::cout << "path\t" << targetPath << "\n";
    std::cout << "elapsed_ms\t" << requestMilliseconds << "\n";
    std::cout << "runs\t" << cliOptions.runs << "\n";
    std::cout << "workers\t" << cliOptions.workers << "\n";
    std::cout << "workload_mode\t" << getCartesianModeText(combinationCycle) << "\n";
    std::cout << "combination_cycle\t" << combinationCycle << "\n";
    std::cout << "speaker_mode\t" << getRoundRobinModeText(benchSpeakerIds.size()) << "\n";
    std::cout << "speakers\t" << joinSpeakerIds(benchSpeakerIds) << "\n";
    std::cout << "text_mode\t" << getRoundRobinModeText(benchTexts.size()) << "\n";
    std::cout << "text_count\t" << benchTexts.size() << "\n";
    std::cout << "text_utf8_bytes\t" << joinTextByteLengths(benchTexts) << "\n";
    std::cout << "http_path_mode\t" << getRoundRobinModeText(benchHttpPaths.size()) << "\n";
    std::cout << "http_paths\t" << joinStringValues(benchHttpPaths) << "\n";
    std::cout << "keep_alive\t" << (cliOptions.httpKeepAlive ? "yes" : "no") << "\n";
    std::cout << "completed_requests\t" << completedRequests << "\n";
    std::cout << "failed_requests\t" << failedRequests << "\n";
    std::cout << "speaker_completed_requests\t" << joinSpeakerCounts(benchSpeakerIds, completedRequestsBySpeaker) << "\n";
    std::cout << "speaker_failed_requests\t" << joinSpeakerCounts(benchSpeakerIds, failedRequestsBySpeaker) << "\n";
    std::cout << "text_completed_requests\t" << joinSizeValues(completedRequestsByText) << "\n";
    std::cout << "text_failed_requests\t" << joinSizeValues(failedRequestsByText) << "\n";
    std::cout << "path_completed_requests\t" << joinStringCounts(benchHttpPaths, completedRequestsByPath) << "\n";
    std::cout << "path_failed_requests\t" << joinStringCounts(benchHttpPaths, failedRequestsByPath) << "\n";
    std::cout << "combination_completed_requests\t" << joinCombinationCounts(benchSpeakerIds, benchTexts.size(), benchHttpPaths.size(), completedRequestsByCombination) << "\n";
    std::cout << "combination_failed_requests\t" << joinCombinationCounts(benchSpeakerIds, benchTexts.size(), benchHttpPaths.size(), failedRequestsByCombination) << "\n";
    std::cout << "body_bytes\t" << bodyBytesSize.load() << "\n";
    std::cout << "total_body_bytes\t" << totalBodyBytesSize.load() << "\n";
    std::cout << "request_rps\t" << requestThroughputPerSecond << "\n";
    std::cout << "completed_rps\t" << completedThroughputPerSecond << "\n";
    std::cout << "avg_request_ms\t" << averageElapsedMilliseconds << "\n";
    std::cout << "avg_first_response_ms\t" << averageFirstResponseMilliseconds << "\n";
    std::cout << "avg_first_body_ms\t" << averageFirstBodyMilliseconds << "\n";
    std::cout << "active_workers\t" << countActiveBenchWorkers(completedRequestsByWorker) << "\n";
    std::cout << "worker_completed_requests\t" << joinSizeValues(completedRequestsByWorker) << "\n";
    std::cout << "worker_failed_requests\t" << joinSizeValues(failedRequestsByWorker) << "\n";
    std::cout << "worker_total_body_bytes\t" << joinUint64Values(totalBodyBytesByWorker) << "\n";
    std::cout << "worker_avg_request_ms\t" << joinDoubleValues(averageElapsedMillisecondsByWorker) << "\n";
    std::cout << "worker_avg_first_response_ms\t" << joinDoubleValues(averageFirstResponseMillisecondsByWorker) << "\n";
    std::cout << "worker_avg_first_body_ms\t" << joinDoubleValues(averageFirstBodyMillisecondsByWorker) << "\n";
    std::cout << "max_rss_bytes\t" << getPeakResidentBytes() << "\n";
    return 0;
}

static std::string chooseApiSessionFileExtension(const std::string &contentType, AudioStreamFormat audioStreamFormat) {
    if (contentType.find("application/json") != std::string::npos) {
        return ".json";
    }
    if (contentType.find("audio/wav") != std::string::npos || contentType.find("audio/wave") != std::string::npos) {
        return ".wav";
    }
    if (contentType.find("audio/pcm") != std::string::npos || contentType.find("application/octet-stream") != std::string::npos) {
        return audioStreamFormat == AudioStreamFormat::Pcm ? ".pcm" : ".wav";
    }
    return ".bin";
}

struct ApiSessionRequest {
    std::string method = "GET";
    std::string targetPath;
    std::vector<uint8_t> bodyBytes;
    std::string contentType;
};

static std::string readApiSessionInputText(const std::string &lineText) {
    if (lineText.size() > 1 && lineText.front() == '@') {
        return readTextFile(lineText.substr(1));
    }
    return lineText;
}

static std::vector<std::string> splitApiSessionInputFields(const std::string &lineText) {
    std::vector<std::string> fields;
    size_t fieldStart = 0;
    while (fieldStart <= lineText.size()) {
        size_t separatorPosition = lineText.find('\t', fieldStart);
        if (separatorPosition == std::string::npos) {
            fields.push_back(trimAscii(lineText.substr(fieldStart)));
            break;
        }
        fields.push_back(trimAscii(lineText.substr(fieldStart, separatorPosition - fieldStart)));
        fieldStart = separatorPosition + 1;
    }
    return fields;
}

static std::string joinApiSessionInputFields(const std::vector<std::string> &inputFields) {
    std::ostringstream textStream;
    for (size_t fieldIndex = 0; fieldIndex < inputFields.size(); fieldIndex++) {
        if (fieldIndex > 0) {
            textStream << '\t';
        }
        textStream << inputFields[fieldIndex];
    }
    return textStream.str();
}

static ApiSessionLineSpec parseApiSessionLineSpec(const CliOptions &cliOptions, const std::string &lineText) {
    ApiSessionLineSpec lineSpec;
    std::vector<std::string> inputFields = splitApiSessionInputFields(lineText);
    if (!inputFields.empty() && !inputFields.front().empty() && inputFields.front().front() == '/') {
        lineSpec.httpPath = inputFields.front();
        lineSpec.inputFields.assign(inputFields.begin() + 1, inputFields.end());
    } else {
        lineSpec.httpPath = cliOptions.httpPath;
        lineSpec.inputFields = std::move(inputFields);
    }
    return lineSpec;
}

static bool isSongApiSessionPath(const std::string &httpPath) {
    std::string normalizedPath = getHttpBenchPathname(httpPath);
    return normalizedPath == "/sing_frame_audio_query"
        || normalizedPath == "/sing_frame_f0"
        || normalizedPath == "/sing_frame_volume"
        || normalizedPath == "/frame_synthesis";
}

static ApiSessionRequest createApiSessionRequest(const CliOptions &cliOptions, const ApiSessionLineSpec &lineSpec) {
    ApiSessionRequest request;
    if (isSongApiSessionPath(lineSpec.httpPath)) {
        std::string normalizedPath = getHttpBenchPathname(lineSpec.httpPath);
        request.method = "POST";
        request.targetPath = createHttpSongBenchTargetPath(lineSpec.httpPath, cliOptions.speaker, cliOptions.audioStreamFormat);
        if ((normalizedPath == "/sing_frame_f0" || normalizedPath == "/sing_frame_volume") && lineSpec.inputFields.size() == 2) {
            request.bodyBytes = makeBodyBytes(createSongBenchRequestBody(readApiSessionInputText(lineSpec.inputFields[0]), readApiSessionInputText(lineSpec.inputFields[1])));
        } else {
            if ((normalizedPath == "/sing_frame_audio_query" || normalizedPath == "/frame_synthesis") && lineSpec.inputFields.size() > 1) {
                throw std::runtime_error("song api-session の入力列数が不正です");
            }
            request.bodyBytes = makeBodyBytes(readApiSessionInputText(joinApiSessionInputFields(lineSpec.inputFields)));
        }
        request.contentType = "application/json";
        return request;
    }
    std::string textValue = readApiSessionInputText(joinApiSessionInputFields(lineSpec.inputFields));
    request.targetPath = createHttpBenchTargetPath(cliOptions, lineSpec.httpPath, textValue, cliOptions.speaker);
    return request;
}

static bool isApiSessionSongScoreText(const std::string &jsonText) {
    return !extractJsonArrayField(jsonText, "notes").empty();
}

static bool isApiSessionFrameAudioQueryText(const std::string &jsonText) {
    return !extractJsonArrayField(jsonText, "phonemes").empty();
}

static bool hasApiSessionJsonField(const std::string &jsonText, const std::string &fieldName) {
    return findJsonFieldValuePosition(jsonText, fieldName) != std::string::npos;
}

static HttpBenchResponse executeApiSessionRequest(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const ApiSessionRequest &request) {
    return request.method == "POST"
        ? requestHttpBenchPostKeepAlive(cliOptions, socketDescriptor, pendingResponseText, request.targetPath, request.bodyBytes, request.contentType)
        : requestHttpBenchTargetKeepAlive(cliOptions, socketDescriptor, pendingResponseText, request.targetPath);
}

static std::string requestApiSessionSongFrameAudioQuery(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const std::string &scoreText, double &elapsedMilliseconds, double &firstResponseMilliseconds, double &firstBodyMilliseconds) {
    ApiSessionRequest request;
    request.method = "POST";
    request.targetPath = createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat);
    request.bodyBytes = makeBodyBytes(scoreText);
    request.contentType = "application/json";
    HttpBenchResponse response = executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
    if (response.statusCode != 200) {
        throw std::runtime_error("song api-session の sing_frame_audio_query が失敗しました: status=" + std::to_string(response.statusCode));
    }
    elapsedMilliseconds += response.elapsedMilliseconds;
    firstResponseMilliseconds += response.firstResponseMilliseconds;
    firstBodyMilliseconds += response.firstBodyMilliseconds;
    return std::string(response.bodyBytesData.begin(), response.bodyBytesData.end());
}

static HttpBenchResponse executeApiSessionSongRequest(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const ApiSessionLineSpec &lineSpec) {
    std::string normalizedPath = getHttpBenchPathname(lineSpec.httpPath);
    const std::vector<std::string> &inputFields = lineSpec.inputFields;
    if (normalizedPath == "/sing_frame_audio_query") {
        return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
    }
    if (normalizedPath == "/sing_frame_f0" || normalizedPath == "/sing_frame_volume") {
        if (inputFields.size() > 2) {
            throw std::runtime_error("song api-session の入力列数が不正です");
        }
        if (inputFields.size() == 2) {
            return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
        }
        std::string inputText = readApiSessionInputText(joinApiSessionInputFields(inputFields));
        if (isApiSessionSongScoreText(inputText) && !hasApiSessionJsonField(inputText, "frame_audio_query")) {
            double elapsedMilliseconds = 0.0;
            double firstResponseMilliseconds = 0.0;
            double firstBodyMilliseconds = 0.0;
            std::string frameAudioQueryText = requestApiSessionSongFrameAudioQuery(cliOptions, socketDescriptor, pendingResponseText, inputText, elapsedMilliseconds, firstResponseMilliseconds, firstBodyMilliseconds);
            ApiSessionRequest request;
            request.method = "POST";
            request.targetPath = createHttpSongBenchTargetPath(lineSpec.httpPath, cliOptions.speaker, cliOptions.audioStreamFormat);
            request.bodyBytes = makeBodyBytes(createSongBenchRequestBody(inputText, frameAudioQueryText));
            request.contentType = "application/json";
            HttpBenchResponse response = executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
            response.elapsedMilliseconds += elapsedMilliseconds;
            response.firstResponseMilliseconds += firstResponseMilliseconds;
            response.firstBodyMilliseconds += firstBodyMilliseconds;
            return response;
        }
        return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
    }
    if (normalizedPath == "/frame_synthesis") {
        if (inputFields.size() > 1) {
            throw std::runtime_error("song api-session の入力列数が不正です");
        }
        std::string inputText = readApiSessionInputText(joinApiSessionInputFields(inputFields));
        ApiSessionRequest request;
        request.method = "POST";
        request.targetPath = createHttpSongBenchTargetPath(lineSpec.httpPath, cliOptions.speaker, cliOptions.audioStreamFormat);
        request.contentType = "application/json";
        if (hasApiSessionJsonField(inputText, "frame_audio_query")) {
            request.bodyBytes = makeBodyBytes(extractJsonObjectField(inputText, "frame_audio_query"));
            return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
        }
        if (isApiSessionSongScoreText(inputText) && !isApiSessionFrameAudioQueryText(inputText)) {
            double elapsedMilliseconds = 0.0;
            double firstResponseMilliseconds = 0.0;
            double firstBodyMilliseconds = 0.0;
            std::string frameAudioQueryText = requestApiSessionSongFrameAudioQuery(cliOptions, socketDescriptor, pendingResponseText, inputText, elapsedMilliseconds, firstResponseMilliseconds, firstBodyMilliseconds);
            request.bodyBytes = makeBodyBytes(frameAudioQueryText);
            HttpBenchResponse response = executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
            response.elapsedMilliseconds += elapsedMilliseconds;
            response.firstResponseMilliseconds += firstResponseMilliseconds;
            response.firstBodyMilliseconds += firstBodyMilliseconds;
            return response;
        }
        request.bodyBytes = makeBodyBytes(inputText);
        return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
    }
    return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
}

static std::vector<std::string> getOptionalSongScoreTexts(const CliOptions &cliOptions) {
    if (cliOptions.scorePath.empty()) {
        if (!cliOptions.benchScorePaths.empty()) {
            throw std::runtime_error("--add-score の前に --score が必要です");
        }
        return {};
    }
    return getBenchScoreTexts(cliOptions);
}

static std::vector<std::string> createApiSessionDefaultSongBodies(const CliOptions &cliOptions) {
    std::string normalizedPath = getHttpBenchPathname(cliOptions.httpPath);
    std::vector<std::string> scoreTexts = getOptionalSongScoreTexts(cliOptions);
    std::vector<std::string> frameAudioQueryTexts = getBenchFrameAudioQueryTexts(cliOptions, scoreTexts.empty() ? 1 : scoreTexts.size());
    if (normalizedPath == "/sing_frame_audio_query") {
        return scoreTexts;
    }
    if (normalizedPath == "/sing_frame_f0" || normalizedPath == "/sing_frame_volume") {
        if (scoreTexts.empty()) {
            return {};
        }
        if (!frameAudioQueryTexts.empty()) {
            std::vector<std::string> requestBodies;
            requestBodies.reserve(scoreTexts.size());
            for (size_t scoreIndex = 0; scoreIndex < scoreTexts.size(); scoreIndex++) {
                requestBodies.push_back(scoreTexts[scoreIndex] + "\t" + frameAudioQueryTexts[scoreIndex]);
            }
            return requestBodies;
        }
        return scoreTexts;
    }
    if (normalizedPath == "/frame_synthesis") {
        if (!frameAudioQueryTexts.empty()) {
            return frameAudioQueryTexts;
        }
        fs::path frameAudioQueryPath = cliOptions.frameAudioQueryPath.empty() ? cliOptions.audioQueryPath : cliOptions.frameAudioQueryPath;
        if (!frameAudioQueryPath.empty()) {
            return {readTextFile(frameAudioQueryPath)};
        }
        return scoreTexts;
    }
    return {};
}

static int runApiSessionCommand(const CliOptions &cliOptions) {
    if (cliOptions.port <= 0) {
        throw std::runtime_error("--port は 1 以上が必要です");
    }
    fs::path outputDirectory = cliOptions.outputPath;
    if (outputDirectory.empty() || outputDirectory == "-") {
        outputDirectory = "api-session-out";
    }
    fs::create_directories(outputDirectory);
    LitevoxSocket socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
    std::string pendingResponseText;
    size_t completedCount = 0;
    try {
        std::vector<std::string> inputLines;
        std::string lineText;
        while (std::getline(std::cin, lineText)) {
            std::string textValue = trimAscii(lineText);
            if (textValue.empty()) {
                continue;
            }
            inputLines.push_back(textValue);
        }
        if (inputLines.empty()) {
            std::vector<std::string> defaultSongBodies = createApiSessionDefaultSongBodies(cliOptions);
            for (const std::string &defaultSongBody : defaultSongBodies) {
                if (!defaultSongBody.empty()) {
                    inputLines.push_back(defaultSongBody);
                }
            }
        }
        for (const std::string &inputLine : inputLines) {
            ApiSessionLineSpec lineSpec = parseApiSessionLineSpec(cliOptions, inputLine);
            HttpBenchResponse response = isSongApiSessionPath(lineSpec.httpPath)
                ? executeApiSessionSongRequest(cliOptions, socketDescriptor, pendingResponseText, lineSpec)
                : executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
            if (response.statusCode != 200) {
                throw std::runtime_error("api-session request が失敗しました: status=" + std::to_string(response.statusCode));
            }
            std::string extension = chooseApiSessionFileExtension(response.contentType, cliOptions.audioStreamFormat);
            std::ostringstream fileNameStream;
            fileNameStream << std::setw(4) << std::setfill('0') << (completedCount + 1);
            fs::path outputFilePath = outputDirectory / (fileNameStream.str() + extension);
            writeBinaryFile(outputFilePath, response.bodyBytesData);
            std::cout << (completedCount + 1) << "\t" << outputFilePath.string() << "\t" << response.bodyBytesData.size() << "\t" << std::fixed << std::setprecision(3) << response.elapsedMilliseconds << "\t" << response.firstBodyMilliseconds << "\n";
            completedCount++;
        }
    } catch (...) {
        closeSocket(socketDescriptor);
        throw;
    }
    closeSocket(socketDescriptor);
    return 0;
}

static int runCommand(const CliOptions &cliOptions) {
    if (cliOptions.commandMode == CommandMode::Help) {
        printUsage();
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::OpenApi) {
        std::cout << createCliOpenApiJson(cliOptions);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::ApiSession) {
        return runApiSessionCommand(cliOptions);
    }
    if ((cliOptions.commandMode == CommandMode::Inspect || cliOptions.commandMode == CommandMode::Inventory || cliOptions.commandMode == CommandMode::Assets || cliOptions.commandMode == CommandMode::ModelDump || cliOptions.commandMode == CommandMode::VvBin || cliOptions.commandMode == CommandMode::VvBinOrt || cliOptions.commandMode == CommandMode::VvBinModels || cliOptions.commandMode == CommandMode::VvBinOperators || cliOptions.commandMode == CommandMode::VvBinExportOnnx || cliOptions.commandMode == CommandMode::VvBinCompareOnnx || cliOptions.commandMode == CommandMode::VvBinChain || cliOptions.commandMode == CommandMode::VvBinSynth || cliOptions.commandMode == CommandMode::Extract || cliOptions.commandMode == CommandMode::Validate) && !cliOptions.hasExplicitModels) {
        throw std::runtime_error("MODEL.vvm または --models が必要です");
    }
    if (cliOptions.commandMode == CommandMode::OrtInspect) {
        std::cout << createOrtInspectText(cliOptions.runtimePaths.onnxruntimeLibraryPath);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::NativeOnnx) {
        std::cout << createNativeOnnxInspectText(cliOptions.runtimePaths.onnxruntimeLibraryPath, cliOptions.inspectPath, cliOptions.nativeOnnxInputDirectory, cliOptions.runtimePaths.cpuNumThreads);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::OnnxRandomSeed) {
        if (!cliOptions.hasOnnxSeed) {
            throw std::runtime_error("--seed が必要です");
        }
        std::cout << patchNativeOnnxRandomSeed(cliOptions.inspectPath, cliOptions.outputPath, static_cast<float>(cliOptions.onnxSeed));
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinOrt) {
        std::cout << createNativeOnnxVvmInspectText(cliOptions.runtimePaths.onnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.nativeOnnxInputDirectory, cliOptions.runtimePaths.cpuNumThreads);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinModels) {
        std::cout << createNativeOnnxVvmModelInfoText(cliOptions.runtimePaths.onnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.runtimePaths.cpuNumThreads);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinOperators) {
        std::cout << createNativeOnnxVvmOperatorText(cliOptions.runtimePaths.onnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.runtimePaths.cpuNumThreads);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinExportOnnx) {
        if (cliOptions.onnxOutputDirectory.empty()) {
            throw std::runtime_error("--extract-onnx DIR が必要です");
        }
        std::cout << exportNativeOnnxVvmOnnxFiles(cliOptions.runtimePaths.onnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.onnxOutputDirectory, cliOptions.runtimePaths.cpuNumThreads, cliOptions.hasOnnxSeed, static_cast<float>(cliOptions.onnxSeed));
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinCompareOnnx) {
        fs::path compareOnnxruntimeLibraryPath = cliOptions.compareOnnxruntimeLibraryPath.empty() ? cliOptions.runtimePaths.onnxruntimeLibraryPath : cliOptions.compareOnnxruntimeLibraryPath;
        std::cout << createNativeOnnxVvmCompareText(cliOptions.runtimePaths.onnxruntimeLibraryPath, compareOnnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.nativeOnnxInputDirectory, cliOptions.audioQueryPath, cliOptions.speaker, cliOptions.modelAssetName, cliOptions.runtimePaths.cpuNumThreads, cliOptions.runs);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinChain) {
        std::cout << createNativeOnnxVvmChainText(cliOptions.runtimePaths.onnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.nativeOnnxInputDirectory, cliOptions.audioQueryPath, cliOptions.speaker, cliOptions.runtimePaths.cpuNumThreads);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBinSynth) {
        if (cliOptions.audioQueryPath.empty()) {
            throw std::runtime_error("--audio-query が必要です");
        }
        std::vector<uint8_t> wavBytes = synthesizeNativeOnnxVvmAudioQuery(cliOptions.runtimePaths.onnxruntimeLibraryPath, collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.audioQueryPath, cliOptions.speaker, cliOptions.runtimePaths.cpuNumThreads);
        if (cliOptions.outputPath == "-") {
            std::cout.write(reinterpret_cast<const char *>(wavBytes.data()), static_cast<std::streamsize>(wavBytes.size()));
        } else {
            writeBinaryFile(cliOptions.outputPath, wavBytes);
            std::cout << cliOptions.outputPath.string() << "\n";
        }
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::Inspect) {
        std::cout << createArchiveInspectionText(collectArchiveSummaries(cliOptions.runtimePaths.modelPaths));
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::Inventory) {
        std::cout << createArchiveInventoryText(collectArchiveSummaries(cliOptions.runtimePaths.modelPaths));
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::Assets) {
        std::cout << createModelAssetTable(collectModelAssetsFromModelRoots(cliOptions.runtimePaths.modelPaths));
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::ModelDump) {
        if (cliOptions.shouldExtractResources) {
            std::cout << createResourceExtractionText(cliOptions);
            return 0;
        }
        if (cliOptions.shouldExtractEngineAssets) {
            std::cout << createEngineAssetExtractionText(cliOptions);
            return 0;
        }
        if (cliOptions.shouldExtractRuntime) {
            std::cout << createRuntimeExtractionText(cliOptions);
            return 0;
        }
        if (cliOptions.shouldExtractOnnxruntime) {
            std::cout << createOnnxruntimeExtractionText(cliOptions);
            return 0;
        }
        std::vector<VvmArchiveSummary> archiveSummaries = collectArchiveSummaries(cliOptions.runtimePaths.modelPaths);
        if (cliOptions.shouldExtractOnnx) {
            if (cliOptions.onnxOutputDirectory.empty()) {
                throw std::runtime_error("--extract-onnx DIR が必要です");
            }
            std::cout << exportOnnxModelFiles(archiveSummaries, cliOptions.onnxOutputDirectory);
            return 0;
        }
        std::cout << createModelDumpText(archiveSummaries, cliOptions.previewBytes, cliOptions.shouldScanModels);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::VvBin) {
        std::cout << createVvBinInspectText(collectArchiveSummaries(cliOptions.runtimePaths.modelPaths), cliOptions.previewBytes, cliOptions.shouldFullScan);
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::Cache) {
        return runCacheCommand(cliOptions);
    }
    if (cliOptions.commandMode == CommandMode::Extract) {
        return runExtractCommand(cliOptions);
    }
    if (cliOptions.commandMode == CommandMode::Validate) {
        std::cout << createArchiveValidationText(collectArchiveSummaries(cliOptions.runtimePaths.modelPaths));
        return 0;
    }
    if (cliOptions.commandMode == CommandMode::Bench) {
        return runBenchCommand(cliOptions);
    }
    if (cliOptions.commandMode == CommandMode::BenchSong) {
        return runSongBenchCommand(cliOptions);
    }
    if (cliOptions.commandMode == CommandMode::BenchHttp) {
        return runHttpBenchCommand(cliOptions);
    }
    if (cliOptions.commandMode == CommandMode::BenchHttpSong) {
        return runHttpSongBenchCommand(cliOptions);
    }
    if (cliOptions.commandMode == CommandMode::Server && cliOptions.workers > 1) {
        std::vector<RuntimeWorker> runtimeWorkers;
        std::map<uint32_t, size_t> sharedLoadedStyleCounts;
        std::mutex sharedLoadedStylesMutex;
        std::map<uint32_t, uint64_t> sharedStyleUnloadGenerations;
        std::mutex sharedStyleUnloadMutex;
        std::mutex sharedUserDictMutex;
        std::mutex sharedPresetMutex;
        std::mutex sharedSettingMutex;
        std::mutex sharedLibraryMutex;
        std::vector<std::unique_ptr<RuntimeHolder>> runtimeHolders = createRuntimeHolders(cliOptions, &sharedLoadedStyleCounts, &sharedLoadedStylesMutex, &sharedStyleUnloadGenerations, &sharedStyleUnloadMutex, &sharedUserDictMutex, &sharedPresetMutex, &sharedSettingMutex, &sharedLibraryMutex);
        runtimeWorkers.reserve(cliOptions.workers);
        for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
            runtimeWorkers.push_back(RuntimeWorker{runtimeHolders[workerIndex]->runtimeState});
        }
        serveRuntimePool(runtimeWorkers, cliOptions.port);
        return 0;
    }
    RuntimeState *runtimeState = new RuntimeState(createRuntimeState(cliOptions.runtimePaths, cliOptions.shouldPreload));
    runtimeState->workerIndex = 0;
    runtimeState->workerCount = 1;
    try {
        if (cliOptions.commandMode == CommandMode::Server) {
            serve(*runtimeState, cliOptions.port);
        } else if (cliOptions.commandMode == CommandMode::Synthesis) {
            if (cliOptions.audioQueryPath.empty()) {
                throw std::runtime_error("--audio-query が必要です");
            }
            std::vector<uint8_t> wavBytes = synthesizeAudioQuery(*runtimeState, readTextFile(cliOptions.audioQueryPath), cliOptions.speaker);
            AudioStreamPayload audioStreamPayload = createAudioStreamPayload(wavBytes, cliOptions.audioStreamFormat);
            if (cliOptions.outputPath == "-") {
                std::cout.write(reinterpret_cast<const char *>(audioStreamPayload.audioBytes.data()), static_cast<std::streamsize>(audioStreamPayload.audioBytes.size()));
            } else {
                writeBinaryFile(cliOptions.outputPath, audioStreamPayload.audioBytes);
                std::cout << cliOptions.outputPath.string() << "\n";
            }
        } else if (cliOptions.commandMode == CommandMode::Tts || cliOptions.commandMode == CommandMode::Stream) {
            if (cliOptions.text.empty()) {
                throw std::runtime_error("--text が必要です");
            }
            if (cliOptions.outputPath == "-") {
                RuntimeAudioStreamOptions streamOptions;
                streamOptions.audioStreamFormat = cliOptions.audioStreamFormat;
                streamOptions.chunkSamples = cliOptions.chunkSamples;
                auto writeStdoutChunk = [](const uint8_t *audioBytes, size_t byteCount) {
                    std::cout.write(reinterpret_cast<const char *>(audioBytes), static_cast<std::streamsize>(byteCount));
                };
                if (cliOptions.isKana) {
                    streamKana(*runtimeState, cliOptions.text, cliOptions.speaker, streamOptions, writeStdoutChunk);
                } else {
                    streamText(*runtimeState, cliOptions.text, cliOptions.speaker, streamOptions, writeStdoutChunk);
                }
            } else {
                std::vector<uint8_t> wavBytes = cliOptions.isKana
                    ? synthesizeKana(*runtimeState, cliOptions.text, cliOptions.speaker)
                    : synthesizeText(*runtimeState, cliOptions.text, cliOptions.speaker);
                AudioStreamPayload audioStreamPayload = createAudioStreamPayload(wavBytes, cliOptions.audioStreamFormat);
                writeBinaryFile(cliOptions.outputPath, audioStreamPayload.audioBytes);
                std::cout << cliOptions.outputPath.string() << "\n";
            }
        } else if (cliOptions.commandMode == CommandMode::Query) {
            if (cliOptions.text.empty()) {
                throw std::runtime_error("--text が必要です");
            }
            if (cliOptions.isKana) {
                std::cout << createAudioQueryFromKana(*runtimeState, cliOptions.text, cliOptions.speaker) << "\n";
            } else {
                std::cout << createAudioQuery(*runtimeState, cliOptions.text, cliOptions.speaker) << "\n";
            }
        } else if (cliOptions.commandMode == CommandMode::SingQuery) {
            if (cliOptions.scorePath.empty()) {
                throw std::runtime_error("--score が必要です");
            }
            writeCliTextOutput(cliOptions.outputPath, createSingFrameAudioQuery(*runtimeState, readTextFile(cliOptions.scorePath), cliOptions.speaker));
        } else if (cliOptions.commandMode == CommandMode::SingF0) {
            if (cliOptions.scorePath.empty()) {
                throw std::runtime_error("--score が必要です");
            }
            if (cliOptions.frameAudioQueryPath.empty()) {
                throw std::runtime_error("--frame-audio-query が必要です");
            }
            writeCliTextOutput(cliOptions.outputPath, createSingFrameF0(*runtimeState, readTextFile(cliOptions.scorePath), readTextFile(cliOptions.frameAudioQueryPath), cliOptions.speaker));
        } else if (cliOptions.commandMode == CommandMode::SingVolume) {
            if (cliOptions.scorePath.empty()) {
                throw std::runtime_error("--score が必要です");
            }
            if (cliOptions.frameAudioQueryPath.empty()) {
                throw std::runtime_error("--frame-audio-query が必要です");
            }
            writeCliTextOutput(cliOptions.outputPath, createSingFrameVolume(*runtimeState, readTextFile(cliOptions.scorePath), readTextFile(cliOptions.frameAudioQueryPath), cliOptions.speaker));
        } else if (cliOptions.commandMode == CommandMode::FrameSynthesis) {
            fs::path frameAudioQueryPath = cliOptions.frameAudioQueryPath.empty() ? cliOptions.audioQueryPath : cliOptions.frameAudioQueryPath;
            if (frameAudioQueryPath.empty()) {
                throw std::runtime_error("--frame-audio-query が必要です");
            }
            std::vector<uint8_t> wavBytes = synthesizeFrameAudioQuery(*runtimeState, readTextFile(frameAudioQueryPath), cliOptions.speaker);
            AudioStreamPayload audioStreamPayload = createAudioStreamPayload(wavBytes, cliOptions.audioStreamFormat);
            if (cliOptions.outputPath == "-") {
                std::cout.write(reinterpret_cast<const char *>(audioStreamPayload.audioBytes.data()), static_cast<std::streamsize>(audioStreamPayload.audioBytes.size()));
            } else {
                writeBinaryFile(cliOptions.outputPath, audioStreamPayload.audioBytes);
                std::cout << cliOptions.outputPath.string() << "\n";
            }
        } else if (cliOptions.commandMode == CommandMode::Models) {
            std::cout << createModelTable(*runtimeState);
        } else if (cliOptions.commandMode == CommandMode::Inspect) {
            size_t styleCount = 0;
            for (const VoiceModelRecord &voiceModel : runtimeState->voiceModels) {
                styleCount += voiceModel.styles.size();
            }
            std::cout << "models\t" << runtimeState->voiceModels.size() << "\n";
            std::cout << "styles\t" << styleCount << "\n";
            std::cout << createModelTable(*runtimeState);
        } else if (cliOptions.commandMode == CommandMode::Speakers) {
            std::cout << createSpeakersJson(runtimeState->combinedMetasJson, getCoreBackendCapabilities(runtimeState->coreBackend).supportsMorphing, createCharacterSupportedFeaturesJsons(runtimeState->characterResources)) << "\n";
        } else if (cliOptions.commandMode == CommandMode::Deps) {
            std::cout << createRuntimeDependencyTable(*runtimeState);
        } else if (cliOptions.commandMode == CommandMode::Devices) {
            std::cout << createSupportedDevicesJson(*runtimeState) << "\n";
        } else if (cliOptions.commandMode == CommandMode::Version) {
            std::cout << getRuntimeCoreVersion(*runtimeState) << "\n";
        } else if (cliOptions.commandMode == CommandMode::RuntimeInfo) {
            std::cout << createRuntimeInfoJson(*runtimeState) << "\n";
        } else if (cliOptions.commandMode == CommandMode::Trace) {
            RuntimeTraceOptions traceOptions;
            traceOptions.outputDirectory = cliOptions.outputPath;
            traceOptions.text = cliOptions.text;
            traceOptions.styleId = cliOptions.speaker;
            traceOptions.isKana = cliOptions.isKana;
            if (traceOptions.outputDirectory.empty() || traceOptions.outputDirectory == "out.wav" || traceOptions.outputDirectory == "-") {
                throw std::runtime_error("trace には --out DIR が必要です");
            }
            std::cout << writeRuntimeTrace(*runtimeState, traceOptions).string() << "\n";
        }
    } catch (...) {
        throw;
    }
    return 0;
}

int runCli(int argc, char **argv) {
    CliOptions cliOptions = parseCliOptions(argc, argv);
    bool shouldSetOrtSeedEnvironment = false;
    bool shouldSetNativeSingSeedEnvironment = false;
    if (cliOptions.hasOnnxSeed) {
        if (cliOptions.commandMode == CommandMode::NativeOnnx
            || cliOptions.commandMode == CommandMode::VvBinOrt
            || cliOptions.commandMode == CommandMode::VvBinCompareOnnx) {
            shouldSetOrtSeedEnvironment = true;
        } else if (cliOptions.commandMode != CommandMode::OnnxRandomSeed
                   && cliOptions.commandMode != CommandMode::VvBinExportOnnx) {
            shouldSetNativeSingSeedEnvironment = true;
        }
    }
    if (shouldSetOrtSeedEnvironment) {
        std::string seedText = std::to_string(cliOptions.onnxSeed);
        setEnvironmentVariable("LITEVOX_ORT_SEED", seedText);
    }
    if (shouldSetNativeSingSeedEnvironment) {
        std::string seedText = std::to_string(cliOptions.onnxSeed);
        setEnvironmentVariable("LITEVOX_NATIVE_SING_SEED", seedText);
    }
    if (!cliOptions.nativeSingTeacherMode.empty()) {
        setEnvironmentVariable("LITEVOX_NATIVE_SING_TEACHER_MODE", cliOptions.nativeSingTeacherMode);
    }
    if (cliOptions.runtimePaths.backendMode == "minimal-ort" || cliOptions.runtimePaths.backendMode == "native") {
        const char *exportOnnxruntimeText = std::getenv("LITEVOX_VV_BIN_ONNXRUNTIME");
        if (!exportOnnxruntimeText || exportOnnxruntimeText[0] == '\0') {
            fs::path bundledOnnxruntimePath = getFirstNamedPath(cliOptions.runtimePaths.rootDirectory, getVoicevoxOnnxruntimeLibraryFileNames());
            bool shouldSetExportOnnxruntime = cliOptions.runtimePaths.backendMode == "minimal-ort";
            if (!shouldSetExportOnnxruntime && fs::exists(bundledOnnxruntimePath)) {
                shouldSetExportOnnxruntime = cliOptions.runtimePaths.onnxruntimeLibraryPath != bundledOnnxruntimePath
                    || cliOptions.runtimePaths.accelerationMode == "gpu"
                    || cliOptions.runtimePaths.nativeModelMode == "exported-onnx"
                    || cliOptions.runtimePaths.nativeModelMode == "exported_onnx"
                    || cliOptions.runtimePaths.nativeModelMode == "onnx";
            }
            if (shouldSetExportOnnxruntime && fs::exists(bundledOnnxruntimePath)) {
                std::string bundledOnnxruntimeText = bundledOnnxruntimePath.string();
                setEnvironmentVariable("LITEVOX_VV_BIN_ONNXRUNTIME", bundledOnnxruntimeText);
            }
        }
    }
    return runCommand(cliOptions);
}
