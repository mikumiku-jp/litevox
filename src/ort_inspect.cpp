#include "dynamic_library.hpp"
#include "ort_inspect.hpp"

#include "utility.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

struct OrtApiBase {
    const void *(*getApi)(uint32_t version);
    const char *(*getVersionString)();
};

static constexpr uint32_t ortInspectApiVersion = 17;
static constexpr size_t ortInspectApiIndexReleaseStatus = 93;
static constexpr size_t ortInspectApiIndexGetAvailableProviders = 125;
static constexpr size_t ortInspectApiIndexReleaseAvailableProviders = 126;
static constexpr size_t ortInspectApiIndexGetTrainingApi = 219;
static constexpr size_t ortInspectTrainingApiIndexSetSeed = 22;

using OrtGetApiBaseFunction = const OrtApiBase *(*)();
using OrtGetTrainingApiFunction = const void *(*)(uint32_t version);
using OrtStatus = void;
using OrtGetAvailableProvidersFunction = OrtStatus *(*)(char ***, int *);
using OrtReleaseAvailableProvidersFunction = OrtStatus *(*)(char **, int);
using OrtReleaseStatusFunction = void (*)(OrtStatus *);

template <typename FunctionType>
static FunctionType loadOrtInspectApiFunction(const void *api, size_t functionIndex) {
    const void *const *apiFunctions = reinterpret_cast<const void *const *>(api);
    return reinterpret_cast<FunctionType>(const_cast<void *>(apiFunctions[functionIndex]));
}

struct OrtRuntimeProbe {
    std::string runtimeVersion;
    bool hasTrainingApi = false;
    bool hasSetSeed = false;
    bool hasCoreMlAppendExecutionProvider = false;
    std::vector<std::string> availableProviders;
};

static std::vector<uint8_t> readOrtBinaryFile(const fs::path &libraryPath) {
    std::ifstream inputStream(libraryPath, std::ios::binary);
    if (!inputStream) {
        throw std::runtime_error("ONNX Runtime を読めません: " + libraryPath.string());
    }
    inputStream.seekg(0, std::ios::end);
    std::streamoff fileSize = inputStream.tellg();
    if (fileSize < 0) {
        throw std::runtime_error("ONNX Runtime サイズを読めません: " + libraryPath.string());
    }
    inputStream.seekg(0, std::ios::beg);
    std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
    if (!fileBytes.empty()) {
        inputStream.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
        if (!inputStream) {
            throw std::runtime_error("ONNX Runtime 読み込みに失敗しました: " + libraryPath.string());
        }
    }
    return fileBytes;
}

static bool isPrintableAscii(uint8_t fileByte) {
    return fileByte >= 0x20 && fileByte <= 0x7e;
}

static std::vector<std::string> extractOrtStrings(const std::vector<uint8_t> &fileBytes, size_t minimumLength) {
    std::vector<std::string> strings;
    std::string currentText;
    for (uint8_t fileByte : fileBytes) {
        if (isPrintableAscii(fileByte)) {
            currentText.push_back(static_cast<char>(fileByte));
            continue;
        }
        if (currentText.size() >= minimumLength) {
            strings.push_back(currentText);
        }
        currentText.clear();
    }
    if (currentText.size() >= minimumLength) {
        strings.push_back(currentText);
    }
    return strings;
}

static std::string lowercaseOrtText(std::string text) {
    for (char &character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

static bool containsTextIgnoreCase(const std::string &text, const std::string &needleText) {
    return lowercaseOrtText(text).find(lowercaseOrtText(needleText)) != std::string::npos;
}

static bool containsAnyString(const std::vector<std::string> &strings, const std::string &needleText) {
    for (const std::string &stringText : strings) {
        if (containsTextIgnoreCase(stringText, needleText)) {
            return true;
        }
    }
    return false;
}

static std::string findFirstString(const std::vector<std::string> &strings, const std::string &needleText) {
    for (const std::string &stringText : strings) {
        if (containsTextIgnoreCase(stringText, needleText)) {
            return stringText;
        }
    }
    return "";
}

static std::string createYesNo(bool isPresent) {
    return isPresent ? "yes" : "no";
}

static std::string sanitizeOrtField(std::string text) {
    for (char &character : text) {
        if (character == '\t' || character == '\n' || character == '\r') {
            character = ' ';
        }
    }
    return text;
}

static OrtRuntimeProbe probeOrtRuntime(const fs::path &libraryPath) {
    OrtRuntimeProbe runtimeProbe;
    void *libraryHandle = openDynamicLibrary(libraryPath);
    if (!libraryHandle) {
        return runtimeProbe;
    }
    OrtGetApiBaseFunction getApiBase = reinterpret_cast<OrtGetApiBaseFunction>(loadDynamicLibrarySymbol(libraryHandle, "OrtGetApiBase"));
    if (!getApiBase) {
        closeDynamicLibrary(libraryHandle);
        return runtimeProbe;
    }
    runtimeProbe.hasCoreMlAppendExecutionProvider = loadDynamicLibrarySymbol(libraryHandle, "OrtSessionOptionsAppendExecutionProvider_CoreML") != nullptr;
    const OrtApiBase *apiBase = getApiBase();
    if (!apiBase) {
        closeDynamicLibrary(libraryHandle);
        return runtimeProbe;
    }
    if (apiBase->getVersionString) {
        runtimeProbe.runtimeVersion = apiBase->getVersionString();
    }
    const void *api = apiBase->getApi ? apiBase->getApi(ortInspectApiVersion) : nullptr;
    if (!api) {
        closeDynamicLibrary(libraryHandle);
        return runtimeProbe;
    }
    OrtGetAvailableProvidersFunction getAvailableProviders = loadOrtInspectApiFunction<OrtGetAvailableProvidersFunction>(api, ortInspectApiIndexGetAvailableProviders);
    OrtReleaseAvailableProvidersFunction releaseAvailableProviders = loadOrtInspectApiFunction<OrtReleaseAvailableProvidersFunction>(api, ortInspectApiIndexReleaseAvailableProviders);
    OrtReleaseStatusFunction releaseStatus = loadOrtInspectApiFunction<OrtReleaseStatusFunction>(api, ortInspectApiIndexReleaseStatus);
    if (getAvailableProviders && releaseAvailableProviders && releaseStatus) {
        char **providerPointers = nullptr;
        int providerCount = 0;
        OrtStatus *status = getAvailableProviders(&providerPointers, &providerCount);
        if (!status && providerPointers) {
            for (int providerIndex = 0; providerIndex < providerCount; providerIndex++) {
                if (providerPointers[providerIndex]) {
                    runtimeProbe.availableProviders.emplace_back(providerPointers[providerIndex]);
                }
            }
        }
        if (status) {
            releaseStatus(status);
        }
        if (providerPointers) {
            OrtStatus *releaseProvidersStatus = releaseAvailableProviders(providerPointers, providerCount);
            if (releaseProvidersStatus) {
                releaseStatus(releaseProvidersStatus);
            }
        }
    }
    OrtGetTrainingApiFunction getTrainingApi = loadOrtInspectApiFunction<OrtGetTrainingApiFunction>(api, ortInspectApiIndexGetTrainingApi);
    if (!getTrainingApi) {
        closeDynamicLibrary(libraryHandle);
        return runtimeProbe;
    }
    const void *trainingApi = getTrainingApi(ortInspectApiVersion);
    runtimeProbe.hasTrainingApi = trainingApi != nullptr;
    if (trainingApi) {
        runtimeProbe.hasSetSeed = loadOrtInspectApiFunction<void *>(trainingApi, ortInspectTrainingApiIndexSetSeed) != nullptr;
    }
    closeDynamicLibrary(libraryHandle);
    return runtimeProbe;
}

static std::string joinOrtValues(const std::vector<std::string> &values) {
    std::ostringstream stream;
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            stream << ",";
        }
        stream << values[valueIndex];
    }
    return stream.str();
}

static bool hasOrtProvider(const std::vector<std::string> &providerNames, const std::string &providerName) {
    return std::find(providerNames.begin(), providerNames.end(), providerName) != providerNames.end();
}

static std::string classifyOrtBinary(const std::vector<std::string> &strings) {
    bool hasVoicevoxBuildInfo = containsAnyString(strings, "VOICEVOX ORT Build Info");
    bool hasCryptosystem = containsAnyString(strings, "crates/cryptosystem/src/lib.rs");
    bool hasRegisterCustomOps = containsAnyString(strings, "RegisterCustomOps");
    if (hasVoicevoxBuildInfo && hasCryptosystem && hasRegisterCustomOps) {
        return "voicevox_private_ort_with_vv_bin_crypto";
    }
    if (hasVoicevoxBuildInfo) {
        return "voicevox_ort";
    }
    return "generic_ort_like";
}

std::string createOrtInspectText(const fs::path &libraryPath) {
    ensurePathExists(libraryPath, "ONNX Runtime");
    std::vector<uint8_t> fileBytes = readOrtBinaryFile(libraryPath);
    std::vector<std::string> strings = extractOrtStrings(fileBytes, 4);
    OrtRuntimeProbe runtimeProbe = probeOrtRuntime(libraryPath);
    std::ostringstream inspectStream;
    inspectStream << "field\tvalue\n";
    inspectStream << "path\t" << libraryPath.string() << "\n";
    inspectStream << "bytes\t" << fileBytes.size() << "\n";
    inspectStream << "runtime_version\t" << runtimeProbe.runtimeVersion << "\n";
    inspectStream << "install_name\t" << sanitizeOrtField(findFirstString(strings, "@rpath/libvoicevox_onnxruntime")) << "\n";
    inspectStream << "build_info\t" << sanitizeOrtField(findFirstString(strings, "VOICEVOX ORT Build Info")) << "\n";
    inspectStream << "source_path\t" << sanitizeOrtField(findFirstString(strings, "/Users/runner/work/onnxruntime-builder")) << "\n";
    inspectStream << "cryptosystem_crate\t" << createYesNo(containsAnyString(strings, "crates/cryptosystem/src/lib.rs")) << "\n";
    inspectStream << "flatbuffers\t" << createYesNo(containsAnyString(strings, "FlatBuffers")) << "\n";
    inspectStream << "protobuf\t" << createYesNo(containsAnyString(strings, "protobuf")) << "\n";
    inspectStream << "register_custom_ops\t" << createYesNo(containsAnyString(strings, "RegisterCustomOps")) << "\n";
    inspectStream << "training_api\t" << createYesNo(runtimeProbe.hasTrainingApi) << "\n";
    inspectStream << "training_set_seed\t" << createYesNo(runtimeProbe.hasSetSeed) << "\n";
    inspectStream << "coreml_append_symbol\t" << createYesNo(runtimeProbe.hasCoreMlAppendExecutionProvider) << "\n";
    inspectStream << "available_provider_count\t" << runtimeProbe.availableProviders.size() << "\n";
    inspectStream << "available_providers\t" << sanitizeOrtField(joinOrtValues(runtimeProbe.availableProviders)) << "\n";
    inspectStream << "available_cpu_provider\t" << createYesNo(hasOrtProvider(runtimeProbe.availableProviders, "CPUExecutionProvider")) << "\n";
    inspectStream << "available_cuda_provider\t" << createYesNo(hasOrtProvider(runtimeProbe.availableProviders, "CUDAExecutionProvider")) << "\n";
    inspectStream << "available_dml_provider\t" << createYesNo(hasOrtProvider(runtimeProbe.availableProviders, "DmlExecutionProvider")) << "\n";
    inspectStream << "available_coreml_provider\t" << createYesNo(hasOrtProvider(runtimeProbe.availableProviders, "CoreMLExecutionProvider")) << "\n";
    inspectStream << "cpu_provider_marker\t" << createYesNo(containsAnyString(strings, "ExecutionProvider_CPU")) << "\n";
    inspectStream << "cuda_provider_marker\t" << createYesNo(containsAnyString(strings, "ExecutionProvider_Cuda")) << "\n";
    inspectStream << "dml_provider_marker\t" << createYesNo(containsAnyString(strings, "DirectML")) << "\n";
    inspectStream << "coreml_provider_marker\t" << createYesNo(containsAnyString(strings, "CoreML")) << "\n";
    inspectStream << "classification\t" << classifyOrtBinary(strings) << "\n";
    return inspectStream.str();
}
