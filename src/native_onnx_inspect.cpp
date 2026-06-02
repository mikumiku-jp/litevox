#include "native_onnx_internal.hpp"

#include "dynamic_library.hpp"
#include "json_utility.hpp"
#include "model_asset.hpp"
#include "native_audio_query.hpp"
#include "streaming_audio.hpp"
#include "utility.hpp"

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

std::string createNativeOnnxInspectText(const fs::path &onnxruntimeLibraryPath, const fs::path &modelPath, const fs::path &inputDirectory, uint16_t cpuThreadCount) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::ostringstream inspectStream;
        inspectStream << "field\tvalue\n";
        inspectStream << "onnxruntime\t" << onnxruntimeLibraryPath.string() << "\n";
        inspectStream << "api_version\t" << ortApiVersion << "\n";
        inspectStream << "ort_version\t" << getNativeOnnxVersion(nativeOnnxApi) << "\n";
        inspectStream << "cpu_threads\t" << cpuThreadCount << "\n";
        if (!inputDirectory.empty()) {
            inspectStream << "trace_inputs\t" << inputDirectory.string() << "\n";
        }
        if (modelPath.empty()) {
            inspectStream << "session_status\tnot_requested\n";
        } else {
            appendNativeOnnxSessionInfo(inspectStream, nativeOnnxApi, modelPath, inputDirectory, cpuThreadCount);
        }
        closeNativeOnnxApi(nativeOnnxApi);
        return inspectStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string patchNativeOnnxRandomSeed(const fs::path &modelPath, const fs::path &outputPath, float seedValue) {
    if (modelPath.empty()) {
        throw std::runtime_error("MODEL.onnx が必要です");
    }
    if (outputPath.empty() || outputPath == "-") {
        throw std::runtime_error("--out OUT.onnx が必要です");
    }
    std::vector<uint8_t> modelBytes = readNativeOnnxBinaryFile(modelPath);
    size_t rewrittenNodeCount = 0;
    std::vector<uint8_t> rewrittenModelBytes = rewriteNativeOnnxModelRandomSeed(modelBytes.data(), modelBytes.size(), seedValue, rewrittenNodeCount);
    writeBinaryFile(outputPath, rewrittenModelBytes);
    std::ostringstream patchStream;
    patchStream << "field\tvalue\n";
    patchStream << "model_path\t" << modelPath.string() << "\n";
    patchStream << "output_path\t" << outputPath.string() << "\n";
    patchStream << "seed\t" << std::setprecision(9) << static_cast<double>(seedValue) << "\n";
    patchStream << "rewritten_nodes\t" << rewrittenNodeCount << "\n";
    patchStream << "input_bytes\t" << modelBytes.size() << "\n";
    patchStream << "output_bytes\t" << rewrittenModelBytes.size() << "\n";
    return patchStream.str();
}

