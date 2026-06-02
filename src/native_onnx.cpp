#include "native_onnx.hpp"

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

namespace fs = std::filesystem;

struct OrtStatus;
struct OrtEnv;
struct OrtSessionOptions;
struct OrtSession;
struct OrtAllocator;
struct OrtTypeInfo;
struct OrtTensorTypeAndShapeInfo;
struct OrtMemoryInfo;
struct OrtValue;

struct OrtApiBase {
    const void *(*getApi)(uint32_t version);
    const char *(*getVersionString)();
};

static constexpr uint32_t ortApiVersion = 17;
static constexpr int32_t ortLoggingLevelWarning = 2;
static constexpr int32_t ortGraphOptimizationLevelBasic = 1;
static constexpr int64_t nativeOnnxPhonemeSize = 45;
static constexpr size_t nativeOnnxDecoderPaddingFrames = 38;
static constexpr size_t nativeOnnxDecoderMinimumChunkFrames = 128;
static constexpr size_t nativeOnnxSamplesPerFrame = 256;
static constexpr uint32_t nativeOnnxDefaultSamplingRate = 24000;

static constexpr size_t ortApiIndexGetErrorMessage = 2;
static constexpr size_t ortApiIndexCreateEnv = 3;
static constexpr size_t ortApiIndexCreateSession = 7;
static constexpr size_t ortApiIndexRun = 9;
static constexpr size_t ortApiIndexCreateSessionFromArray = 8;
static constexpr size_t ortApiIndexCreateSessionOptions = 10;
static constexpr size_t ortApiIndexSetOptimizedModelFilePath = 11;
static constexpr size_t ortApiIndexSetSessionGraphOptimizationLevel = 23;
static constexpr size_t ortApiIndexSetIntraOpNumThreads = 24;
static constexpr size_t ortApiIndexSetInterOpNumThreads = 25;
static constexpr size_t ortApiIndexSessionGetInputCount = 30;
static constexpr size_t ortApiIndexSessionGetOutputCount = 31;
static constexpr size_t ortApiIndexSessionGetInputTypeInfo = 33;
static constexpr size_t ortApiIndexSessionGetOutputTypeInfo = 34;
static constexpr size_t ortApiIndexCastTypeInfoToTensorInfo = 55;
static constexpr size_t ortApiIndexGetTensorElementType = 60;
static constexpr size_t ortApiIndexGetDimensionsCount = 61;
static constexpr size_t ortApiIndexGetDimensions = 62;
static constexpr size_t ortApiIndexGetTensorShapeElementCount = 64;
static constexpr size_t ortApiIndexGetTensorTypeAndShape = 65;
static constexpr size_t ortApiIndexCreateTensorWithDataAsOrtValue = 49;
static constexpr size_t ortApiIndexGetTensorMutableData = 51;
static constexpr size_t ortApiIndexCreateCpuMemoryInfo = 69;
static constexpr size_t ortApiIndexSessionGetInputName = 36;
static constexpr size_t ortApiIndexSessionGetOutputName = 37;
static constexpr size_t ortApiIndexAllocatorFree = 76;
static constexpr size_t ortApiIndexGetAllocatorWithDefaultOptions = 78;
static constexpr size_t ortApiIndexReleaseEnv = 92;
static constexpr size_t ortApiIndexReleaseStatus = 93;
static constexpr size_t ortApiIndexReleaseMemoryInfo = 94;
static constexpr size_t ortApiIndexReleaseSession = 95;
static constexpr size_t ortApiIndexReleaseValue = 96;
static constexpr size_t ortApiIndexReleaseTypeInfo = 98;
static constexpr size_t ortApiIndexReleaseTensorTypeAndShapeInfo = 99;
static constexpr size_t ortApiIndexReleaseSessionOptions = 100;
static constexpr size_t ortApiIndexGetAvailableProviders = 125;
static constexpr size_t ortApiIndexReleaseAvailableProviders = 126;
static constexpr size_t ortApiIndexAddSessionConfigEntry = 130;
static constexpr size_t ortApiIndexSessionOptionsAppendExecutionProvider = 192;
static constexpr size_t ortApiIndexGetTrainingApi = 219;
static constexpr size_t ortTrainingApiIndexSetSeed = 22;
static constexpr uint32_t nativeOnnxCoreMlFlags = 0x008;

using OrtGetApiBaseFunction = const OrtApiBase *(*)();
using OrtGetErrorMessageFunction = const char *(*)(const OrtStatus *);
using OrtCreateEnvFunction = OrtStatus *(*)(int32_t, const char *, OrtEnv **);
#if defined(_WIN32)
using OrtPathChar = wchar_t;
#else
using OrtPathChar = char;
#endif
using OrtCreateSessionFunction = OrtStatus *(*)(const OrtEnv *, const OrtPathChar *, const OrtSessionOptions *, OrtSession **);
using OrtRunFunction = OrtStatus *(*)(OrtSession *, const void *, const char *const *, const OrtValue *const *, size_t, const char *const *, size_t, OrtValue **);
using OrtCreateSessionFromArrayFunction = OrtStatus *(*)(const OrtEnv *, const void *, size_t, const OrtSessionOptions *, OrtSession **);
using OrtCreateSessionOptionsFunction = OrtStatus *(*)(OrtSessionOptions **);
using OrtSetOptimizedModelFilePathFunction = OrtStatus *(*)(OrtSessionOptions *, const OrtPathChar *);
using OrtSetSessionGraphOptimizationLevelFunction = OrtStatus *(*)(OrtSessionOptions *, int32_t);
using OrtSetThreadCountFunction = OrtStatus *(*)(OrtSessionOptions *, int);
using OrtAddSessionConfigEntryFunction = OrtStatus *(*)(OrtSessionOptions *, const char *, const char *);
using OrtSessionOptionsAppendExecutionProviderFunction = OrtStatus *(*)(OrtSessionOptions *, const char *, const char *const *, const char *const *, size_t);
using OrtSessionOptionsAppendExecutionProviderCoreMLFunction = OrtStatus *(*)(OrtSessionOptions *, uint32_t);
using OrtSessionGetCountFunction = OrtStatus *(*)(const OrtSession *, size_t *);
using OrtSessionGetNameFunction = OrtStatus *(*)(const OrtSession *, size_t, OrtAllocator *, char **);
using OrtSessionGetTypeInfoFunction = OrtStatus *(*)(const OrtSession *, size_t, OrtTypeInfo **);
using OrtCastTypeInfoToTensorInfoFunction = OrtStatus *(*)(const OrtTypeInfo *, const OrtTensorTypeAndShapeInfo **);
using OrtGetTensorElementTypeFunction = OrtStatus *(*)(const OrtTensorTypeAndShapeInfo *, int32_t *);
using OrtGetDimensionsCountFunction = OrtStatus *(*)(const OrtTensorTypeAndShapeInfo *, size_t *);
using OrtGetDimensionsFunction = OrtStatus *(*)(const OrtTensorTypeAndShapeInfo *, int64_t *, size_t);
using OrtGetTensorShapeElementCountFunction = OrtStatus *(*)(const OrtTensorTypeAndShapeInfo *, size_t *);
using OrtGetTensorTypeAndShapeFunction = OrtStatus *(*)(const OrtValue *, OrtTensorTypeAndShapeInfo **);
using OrtCreateTensorWithDataAsOrtValueFunction = OrtStatus *(*)(const OrtMemoryInfo *, void *, size_t, const int64_t *, size_t, int32_t, OrtValue **);
using OrtGetTensorMutableDataFunction = OrtStatus *(*)(OrtValue *, void **);
using OrtCreateCpuMemoryInfoFunction = OrtStatus *(*)(int32_t, int32_t, OrtMemoryInfo **);
using OrtAllocatorFreeFunction = OrtStatus *(*)(OrtAllocator *, void *);
using OrtGetAllocatorWithDefaultOptionsFunction = OrtStatus *(*)(OrtAllocator **);
using OrtGetAvailableProvidersFunction = OrtStatus *(*)(char ***, int *);
using OrtReleaseAvailableProvidersFunction = OrtStatus *(*)(char **, int);
using OrtGetTrainingApiFunction = const void *(*)(uint32_t version);
using OrtTrainingSetSeedFunction = OrtStatus *(*)(int64_t);
using OrtReleaseEnvFunction = void (*)(OrtEnv *);
using OrtReleaseStatusFunction = void (*)(OrtStatus *);
using OrtReleaseMemoryInfoFunction = void (*)(OrtMemoryInfo *);
using OrtReleaseSessionFunction = void (*)(OrtSession *);
using OrtReleaseValueFunction = void (*)(OrtValue *);
using OrtReleaseTypeInfoFunction = void (*)(OrtTypeInfo *);
using OrtReleaseTensorTypeAndShapeInfoFunction = void (*)(OrtTensorTypeAndShapeInfo *);
using OrtReleaseSessionOptionsFunction = void (*)(OrtSessionOptions *);

struct NativeOnnxApi {
    void *libraryHandle = nullptr;
    fs::path libraryPath;
    const OrtApiBase *apiBase = nullptr;
    const void *api = nullptr;
    const void *trainingApi = nullptr;
    OrtGetErrorMessageFunction getErrorMessage = nullptr;
    OrtCreateEnvFunction createEnv = nullptr;
    OrtCreateSessionFunction createSession = nullptr;
    OrtRunFunction run = nullptr;
    OrtCreateSessionFromArrayFunction createSessionFromArray = nullptr;
    OrtCreateSessionOptionsFunction createSessionOptions = nullptr;
    OrtSetOptimizedModelFilePathFunction setOptimizedModelFilePath = nullptr;
    OrtSetSessionGraphOptimizationLevelFunction setSessionGraphOptimizationLevel = nullptr;
    OrtSetThreadCountFunction setIntraOpNumThreads = nullptr;
    OrtSetThreadCountFunction setInterOpNumThreads = nullptr;
    OrtAddSessionConfigEntryFunction addSessionConfigEntry = nullptr;
    OrtSessionOptionsAppendExecutionProviderFunction appendExecutionProvider = nullptr;
    OrtSessionOptionsAppendExecutionProviderCoreMLFunction appendExecutionProviderCoreML = nullptr;
    OrtSessionGetCountFunction sessionGetInputCount = nullptr;
    OrtSessionGetCountFunction sessionGetOutputCount = nullptr;
    OrtSessionGetNameFunction sessionGetInputName = nullptr;
    OrtSessionGetNameFunction sessionGetOutputName = nullptr;
    OrtSessionGetTypeInfoFunction sessionGetInputTypeInfo = nullptr;
    OrtSessionGetTypeInfoFunction sessionGetOutputTypeInfo = nullptr;
    OrtCastTypeInfoToTensorInfoFunction castTypeInfoToTensorInfo = nullptr;
    OrtGetTensorElementTypeFunction getTensorElementType = nullptr;
    OrtGetDimensionsCountFunction getDimensionsCount = nullptr;
    OrtGetDimensionsFunction getDimensions = nullptr;
    OrtGetTensorShapeElementCountFunction getTensorShapeElementCount = nullptr;
    OrtGetTensorTypeAndShapeFunction getTensorTypeAndShape = nullptr;
    OrtCreateTensorWithDataAsOrtValueFunction createTensorWithDataAsOrtValue = nullptr;
    OrtGetTensorMutableDataFunction getTensorMutableData = nullptr;
    OrtCreateCpuMemoryInfoFunction createCpuMemoryInfo = nullptr;
    OrtAllocatorFreeFunction allocatorFree = nullptr;
    OrtGetAllocatorWithDefaultOptionsFunction getAllocatorWithDefaultOptions = nullptr;
    OrtGetAvailableProvidersFunction getAvailableProviders = nullptr;
    OrtReleaseAvailableProvidersFunction releaseAvailableProviders = nullptr;
    OrtTrainingSetSeedFunction setSeed = nullptr;
    OrtReleaseEnvFunction releaseEnv = nullptr;
    OrtReleaseStatusFunction releaseStatus = nullptr;
    OrtReleaseMemoryInfoFunction releaseMemoryInfo = nullptr;
    OrtReleaseSessionFunction releaseSession = nullptr;
    OrtReleaseValueFunction releaseValue = nullptr;
    OrtReleaseTypeInfoFunction releaseTypeInfo = nullptr;
    OrtReleaseTensorTypeAndShapeInfoFunction releaseTensorTypeAndShapeInfo = nullptr;
    OrtReleaseSessionOptionsFunction releaseSessionOptions = nullptr;
    bool hasConfiguredSeed = false;
    int64_t configuredSeed = 0;
};

static std::vector<uint8_t> readNativeOnnxBinaryFile(const fs::path &filePath) {
    std::ifstream inputStream(filePath, std::ios::binary);
    if (!inputStream) {
        throw std::runtime_error("ONNX model を読めません: " + filePath.string());
    }
    inputStream.seekg(0, std::ios::end);
    std::streamoff fileSize = inputStream.tellg();
    if (fileSize < 0) {
        throw std::runtime_error("ONNX model サイズを読めません: " + filePath.string());
    }
    inputStream.seekg(0, std::ios::beg);
    std::vector<uint8_t> fileBytes(static_cast<size_t>(fileSize));
    if (!fileBytes.empty()) {
        inputStream.read(reinterpret_cast<char *>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));
        if (!inputStream) {
            throw std::runtime_error("ONNX model 読み込みに失敗しました: " + filePath.string());
        }
    }
    return fileBytes;
}

static std::string readNativeOnnxTextFile(const fs::path &filePath) {
    std::ifstream inputStream(filePath, std::ios::binary);
    if (!inputStream) {
        throw std::runtime_error("trace json を読めません: " + filePath.string());
    }
    std::ostringstream textStream;
    textStream << inputStream.rdbuf();
    return textStream.str();
}

static std::string extractNativeOnnxJsonStringField(const std::string &jsonText, const std::string &fieldName) {
    std::string markerText = "\"" + fieldName + "\"";
    size_t markerPosition = jsonText.find(markerText);
    if (markerPosition == std::string::npos) {
        return "";
    }
    size_t colonPosition = jsonText.find(':', markerPosition + markerText.size());
    if (colonPosition == std::string::npos) {
        return "";
    }
    size_t quotePosition = jsonText.find('"', colonPosition + 1);
    if (quotePosition == std::string::npos) {
        return "";
    }
    std::string valueText;
    for (size_t characterPosition = quotePosition + 1; characterPosition < jsonText.size(); characterPosition++) {
        char character = jsonText[characterPosition];
        if (character == '"') {
            return valueText;
        }
        if (character == '\\' && characterPosition + 1 < jsonText.size()) {
            characterPosition++;
            valueText.push_back(jsonText[characterPosition]);
            continue;
        }
        valueText.push_back(character);
    }
    return "";
}

static double extractNativeOnnxJsonNumberField(const std::string &jsonText, const std::string &fieldName, double fallbackNumber) {
    std::string markerText = "\"" + fieldName + "\"";
    size_t markerPosition = jsonText.find(markerText);
    if (markerPosition == std::string::npos) {
        return fallbackNumber;
    }
    size_t colonPosition = jsonText.find(':', markerPosition + markerText.size());
    if (colonPosition == std::string::npos) {
        return fallbackNumber;
    }
    size_t numberStartPosition = jsonText.find_first_of("-0123456789", colonPosition + 1);
    if (numberStartPosition == std::string::npos) {
        return fallbackNumber;
    }
    size_t numberEndPosition = numberStartPosition;
    while (numberEndPosition < jsonText.size()) {
        char character = jsonText[numberEndPosition];
        if (!(std::isdigit(static_cast<unsigned char>(character)) || character == '-' || character == '+' || character == '.' || character == 'e' || character == 'E')) {
            break;
        }
        numberEndPosition++;
    }
    return std::stod(jsonText.substr(numberStartPosition, numberEndPosition - numberStartPosition));
}

static bool extractNativeOnnxJsonBoolField(const std::string &jsonText, const std::string &fieldName, bool fallbackValue) {
    std::string markerText = "\"" + fieldName + "\"";
    size_t markerPosition = jsonText.find(markerText);
    if (markerPosition == std::string::npos) {
        return fallbackValue;
    }
    size_t colonPosition = jsonText.find(':', markerPosition + markerText.size());
    if (colonPosition == std::string::npos) {
        return fallbackValue;
    }
    size_t valuePosition = colonPosition + 1;
    while (valuePosition < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[valuePosition]))) {
        valuePosition++;
    }
    if (jsonText.compare(valuePosition, 4, "true") == 0) {
        return true;
    }
    if (jsonText.compare(valuePosition, 5, "false") == 0) {
        return false;
    }
    return fallbackValue;
}

static bool extractNativeOnnxJsonFloatField(const std::string &jsonText, const std::string &fieldName, float &numberValue) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos || valuePosition >= jsonText.size()) {
        return false;
    }
    if (jsonText.compare(valuePosition, 4, "null") == 0) {
        return false;
    }
    size_t numberEndPosition = valuePosition;
    while (numberEndPosition < jsonText.size()) {
        char character = jsonText[numberEndPosition];
        if (!(std::isdigit(static_cast<unsigned char>(character)) || character == '-' || character == '+' || character == '.' || character == 'e' || character == 'E')) {
            break;
        }
        numberEndPosition++;
    }
    if (numberEndPosition == valuePosition) {
        return false;
    }
    numberValue = std::stof(jsonText.substr(valuePosition, numberEndPosition - valuePosition));
    return true;
}

static int64_t parseNativeOnnxJsonInteger(const std::string &numberText) {
    if (numberText.empty()) {
        throw std::runtime_error("整数が空です");
    }
    size_t consumedLength = 0;
    int64_t numberValue = std::stoll(numberText, &consumedLength);
    if (consumedLength != numberText.size()) {
        throw std::runtime_error("整数が不正です: " + numberText);
    }
    return numberValue;
}

static std::vector<int64_t> extractNativeOnnxJsonShapeField(const std::string &jsonText) {
    std::string markerText = "\"shape\"";
    size_t markerPosition = jsonText.find(markerText);
    if (markerPosition == std::string::npos) {
        return {};
    }
    size_t openPosition = jsonText.find('[', markerPosition + markerText.size());
    size_t closePosition = jsonText.find(']', openPosition == std::string::npos ? markerPosition : openPosition);
    if (openPosition == std::string::npos || closePosition == std::string::npos || closePosition < openPosition) {
        throw std::runtime_error("shape が不正です");
    }
    std::vector<int64_t> dimensions;
    std::string currentNumber;
    for (size_t characterPosition = openPosition + 1; characterPosition < closePosition; characterPosition++) {
        char character = jsonText[characterPosition];
        if (std::isdigit(static_cast<unsigned char>(character)) || character == '-') {
            currentNumber.push_back(character);
            continue;
        }
        if (!currentNumber.empty()) {
            dimensions.push_back(parseNativeOnnxJsonInteger(currentNumber));
            currentNumber.clear();
        }
    }
    if (!currentNumber.empty()) {
        dimensions.push_back(parseNativeOnnxJsonInteger(currentNumber));
    }
    return dimensions;
}

template <typename FunctionType>
static FunctionType loadNativeOnnxApiFunction(const void *api, size_t functionIndex) {
    const void *const *apiFunctions = reinterpret_cast<const void *const *>(api);
    void *functionPointer = const_cast<void *>(apiFunctions[functionIndex]);
    return reinterpret_cast<FunctionType>(functionPointer);
}

static bool tryReadNativeOnnxSeedFromEnvironment(int64_t &seedValue) {
    const char *seedText = std::getenv("LITEVOX_ORT_SEED");
    if (!seedText || seedText[0] == '\0') {
        return false;
    }
    errno = 0;
    char *endPointer = nullptr;
    long long parsedSeed = std::strtoll(seedText, &endPointer, 10);
    if (errno != 0 || !endPointer || endPointer == seedText || *endPointer != '\0') {
        throw std::runtime_error("LITEVOX_ORT_SEED が不正です");
    }
    seedValue = static_cast<int64_t>(parsedSeed);
    return true;
}

static NativeOnnxApi loadNativeOnnxApi(const fs::path &onnxruntimeLibraryPath) {
    ensurePathExists(onnxruntimeLibraryPath, "onnxruntime library");
    NativeOnnxApi nativeOnnxApi;
    nativeOnnxApi.libraryPath = onnxruntimeLibraryPath;
    nativeOnnxApi.libraryHandle = openDynamicLibrary(onnxruntimeLibraryPath);
    if (!nativeOnnxApi.libraryHandle) {
        throw std::runtime_error(std::string("ONNX Runtime を開けません: ") + getDynamicLibraryErrorText());
    }
    void *getApiBasePointer = loadDynamicLibrarySymbol(nativeOnnxApi.libraryHandle, "OrtGetApiBase");
    if (!getApiBasePointer) {
        throw std::runtime_error("OrtGetApiBase がありません");
    }
    OrtGetApiBaseFunction getApiBase = reinterpret_cast<OrtGetApiBaseFunction>(getApiBasePointer);
    nativeOnnxApi.apiBase = getApiBase();
    if (!nativeOnnxApi.apiBase) {
        throw std::runtime_error("OrtApiBase を取得できません");
    }
    nativeOnnxApi.api = nativeOnnxApi.apiBase->getApi(ortApiVersion);
    if (!nativeOnnxApi.api) {
        throw std::runtime_error("OrtApi v17 を取得できません");
    }
    nativeOnnxApi.getErrorMessage = loadNativeOnnxApiFunction<OrtGetErrorMessageFunction>(nativeOnnxApi.api, ortApiIndexGetErrorMessage);
    nativeOnnxApi.createEnv = loadNativeOnnxApiFunction<OrtCreateEnvFunction>(nativeOnnxApi.api, ortApiIndexCreateEnv);
    nativeOnnxApi.createSession = loadNativeOnnxApiFunction<OrtCreateSessionFunction>(nativeOnnxApi.api, ortApiIndexCreateSession);
    nativeOnnxApi.run = loadNativeOnnxApiFunction<OrtRunFunction>(nativeOnnxApi.api, ortApiIndexRun);
    nativeOnnxApi.createSessionFromArray = loadNativeOnnxApiFunction<OrtCreateSessionFromArrayFunction>(nativeOnnxApi.api, ortApiIndexCreateSessionFromArray);
    nativeOnnxApi.createSessionOptions = loadNativeOnnxApiFunction<OrtCreateSessionOptionsFunction>(nativeOnnxApi.api, ortApiIndexCreateSessionOptions);
    nativeOnnxApi.setOptimizedModelFilePath = loadNativeOnnxApiFunction<OrtSetOptimizedModelFilePathFunction>(nativeOnnxApi.api, ortApiIndexSetOptimizedModelFilePath);
    nativeOnnxApi.setSessionGraphOptimizationLevel = loadNativeOnnxApiFunction<OrtSetSessionGraphOptimizationLevelFunction>(nativeOnnxApi.api, ortApiIndexSetSessionGraphOptimizationLevel);
    nativeOnnxApi.setIntraOpNumThreads = loadNativeOnnxApiFunction<OrtSetThreadCountFunction>(nativeOnnxApi.api, ortApiIndexSetIntraOpNumThreads);
    nativeOnnxApi.setInterOpNumThreads = loadNativeOnnxApiFunction<OrtSetThreadCountFunction>(nativeOnnxApi.api, ortApiIndexSetInterOpNumThreads);
    nativeOnnxApi.addSessionConfigEntry = loadNativeOnnxApiFunction<OrtAddSessionConfigEntryFunction>(nativeOnnxApi.api, ortApiIndexAddSessionConfigEntry);
    nativeOnnxApi.appendExecutionProvider = loadNativeOnnxApiFunction<OrtSessionOptionsAppendExecutionProviderFunction>(nativeOnnxApi.api, ortApiIndexSessionOptionsAppendExecutionProvider);
    nativeOnnxApi.appendExecutionProviderCoreML = reinterpret_cast<OrtSessionOptionsAppendExecutionProviderCoreMLFunction>(loadDynamicLibrarySymbol(nativeOnnxApi.libraryHandle, "OrtSessionOptionsAppendExecutionProvider_CoreML"));
    nativeOnnxApi.sessionGetInputCount = loadNativeOnnxApiFunction<OrtSessionGetCountFunction>(nativeOnnxApi.api, ortApiIndexSessionGetInputCount);
    nativeOnnxApi.sessionGetOutputCount = loadNativeOnnxApiFunction<OrtSessionGetCountFunction>(nativeOnnxApi.api, ortApiIndexSessionGetOutputCount);
    nativeOnnxApi.sessionGetInputName = loadNativeOnnxApiFunction<OrtSessionGetNameFunction>(nativeOnnxApi.api, ortApiIndexSessionGetInputName);
    nativeOnnxApi.sessionGetOutputName = loadNativeOnnxApiFunction<OrtSessionGetNameFunction>(nativeOnnxApi.api, ortApiIndexSessionGetOutputName);
    nativeOnnxApi.sessionGetInputTypeInfo = loadNativeOnnxApiFunction<OrtSessionGetTypeInfoFunction>(nativeOnnxApi.api, ortApiIndexSessionGetInputTypeInfo);
    nativeOnnxApi.sessionGetOutputTypeInfo = loadNativeOnnxApiFunction<OrtSessionGetTypeInfoFunction>(nativeOnnxApi.api, ortApiIndexSessionGetOutputTypeInfo);
    nativeOnnxApi.castTypeInfoToTensorInfo = loadNativeOnnxApiFunction<OrtCastTypeInfoToTensorInfoFunction>(nativeOnnxApi.api, ortApiIndexCastTypeInfoToTensorInfo);
    nativeOnnxApi.getTensorElementType = loadNativeOnnxApiFunction<OrtGetTensorElementTypeFunction>(nativeOnnxApi.api, ortApiIndexGetTensorElementType);
    nativeOnnxApi.getDimensionsCount = loadNativeOnnxApiFunction<OrtGetDimensionsCountFunction>(nativeOnnxApi.api, ortApiIndexGetDimensionsCount);
    nativeOnnxApi.getDimensions = loadNativeOnnxApiFunction<OrtGetDimensionsFunction>(nativeOnnxApi.api, ortApiIndexGetDimensions);
    nativeOnnxApi.getTensorShapeElementCount = loadNativeOnnxApiFunction<OrtGetTensorShapeElementCountFunction>(nativeOnnxApi.api, ortApiIndexGetTensorShapeElementCount);
    nativeOnnxApi.getTensorTypeAndShape = loadNativeOnnxApiFunction<OrtGetTensorTypeAndShapeFunction>(nativeOnnxApi.api, ortApiIndexGetTensorTypeAndShape);
    nativeOnnxApi.createTensorWithDataAsOrtValue = loadNativeOnnxApiFunction<OrtCreateTensorWithDataAsOrtValueFunction>(nativeOnnxApi.api, ortApiIndexCreateTensorWithDataAsOrtValue);
    nativeOnnxApi.getTensorMutableData = loadNativeOnnxApiFunction<OrtGetTensorMutableDataFunction>(nativeOnnxApi.api, ortApiIndexGetTensorMutableData);
    nativeOnnxApi.createCpuMemoryInfo = loadNativeOnnxApiFunction<OrtCreateCpuMemoryInfoFunction>(nativeOnnxApi.api, ortApiIndexCreateCpuMemoryInfo);
    nativeOnnxApi.allocatorFree = loadNativeOnnxApiFunction<OrtAllocatorFreeFunction>(nativeOnnxApi.api, ortApiIndexAllocatorFree);
    nativeOnnxApi.getAllocatorWithDefaultOptions = loadNativeOnnxApiFunction<OrtGetAllocatorWithDefaultOptionsFunction>(nativeOnnxApi.api, ortApiIndexGetAllocatorWithDefaultOptions);
    nativeOnnxApi.getAvailableProviders = loadNativeOnnxApiFunction<OrtGetAvailableProvidersFunction>(nativeOnnxApi.api, ortApiIndexGetAvailableProviders);
    nativeOnnxApi.releaseAvailableProviders = loadNativeOnnxApiFunction<OrtReleaseAvailableProvidersFunction>(nativeOnnxApi.api, ortApiIndexReleaseAvailableProviders);
    OrtGetTrainingApiFunction getTrainingApi = loadNativeOnnxApiFunction<OrtGetTrainingApiFunction>(nativeOnnxApi.api, ortApiIndexGetTrainingApi);
    if (getTrainingApi) {
        nativeOnnxApi.trainingApi = getTrainingApi(ortApiVersion);
        if (nativeOnnxApi.trainingApi) {
            nativeOnnxApi.setSeed = loadNativeOnnxApiFunction<OrtTrainingSetSeedFunction>(nativeOnnxApi.trainingApi, ortTrainingApiIndexSetSeed);
        }
    }
    nativeOnnxApi.releaseEnv = loadNativeOnnxApiFunction<OrtReleaseEnvFunction>(nativeOnnxApi.api, ortApiIndexReleaseEnv);
    nativeOnnxApi.releaseStatus = loadNativeOnnxApiFunction<OrtReleaseStatusFunction>(nativeOnnxApi.api, ortApiIndexReleaseStatus);
    nativeOnnxApi.releaseMemoryInfo = loadNativeOnnxApiFunction<OrtReleaseMemoryInfoFunction>(nativeOnnxApi.api, ortApiIndexReleaseMemoryInfo);
    nativeOnnxApi.releaseSession = loadNativeOnnxApiFunction<OrtReleaseSessionFunction>(nativeOnnxApi.api, ortApiIndexReleaseSession);
    nativeOnnxApi.releaseValue = loadNativeOnnxApiFunction<OrtReleaseValueFunction>(nativeOnnxApi.api, ortApiIndexReleaseValue);
    nativeOnnxApi.releaseTypeInfo = loadNativeOnnxApiFunction<OrtReleaseTypeInfoFunction>(nativeOnnxApi.api, ortApiIndexReleaseTypeInfo);
    nativeOnnxApi.releaseTensorTypeAndShapeInfo = loadNativeOnnxApiFunction<OrtReleaseTensorTypeAndShapeInfoFunction>(nativeOnnxApi.api, ortApiIndexReleaseTensorTypeAndShapeInfo);
    nativeOnnxApi.releaseSessionOptions = loadNativeOnnxApiFunction<OrtReleaseSessionOptionsFunction>(nativeOnnxApi.api, ortApiIndexReleaseSessionOptions);
    nativeOnnxApi.hasConfiguredSeed = tryReadNativeOnnxSeedFromEnvironment(nativeOnnxApi.configuredSeed);
    if (nativeOnnxApi.hasConfiguredSeed && !nativeOnnxApi.setSeed) {
        throw std::runtime_error("LITEVOX_ORT_SEED / --seed はこの ONNX Runtime では未対応です");
    }
    return nativeOnnxApi;
}

static void ensureNativeOnnxCall(NativeOnnxApi &nativeOnnxApi, OrtStatus *callStatus, const std::string &operationName);
static std::vector<std::string> collectNativeOnnxAvailableProviders(NativeOnnxApi &nativeOnnxApi);
static void applyNativeOnnxSeedIfConfigured(NativeOnnxApi &nativeOnnxApi);
static void configureNativeOnnxSessionOptions(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, OrtSessionOptions *sessionOptions, uint16_t cpuThreadCount, bool shouldUseVvBinConfig);

static void closeNativeOnnxApi(NativeOnnxApi &nativeOnnxApi) {
    if (nativeOnnxApi.libraryHandle) {
        closeDynamicLibrary(nativeOnnxApi.libraryHandle);
        nativeOnnxApi.libraryHandle = nullptr;
    }
}

static bool hasNativeOnnxProviderName(const std::vector<std::string> &providerNames, const std::string &providerName) {
    return std::find(providerNames.begin(), providerNames.end(), providerName) != providerNames.end();
}

static std::string consumeNativeOnnxStatusMessage(NativeOnnxApi &nativeOnnxApi, OrtStatus *callStatus) {
    if (!callStatus) {
        return "";
    }
    std::string errorMessage = nativeOnnxApi.getErrorMessage ? nativeOnnxApi.getErrorMessage(callStatus) : "unknown ONNX Runtime error";
    nativeOnnxApi.releaseStatus(callStatus);
    return errorMessage;
}

static void appendNativeOnnxExecutionProvider(NativeOnnxApi &nativeOnnxApi, OrtSessionOptions *sessionOptions, const std::string &providerName) {
    if (providerName.empty() || providerName == "CPUExecutionProvider") {
        return;
    }
    if (providerName == "CoreMLExecutionProvider") {
        if (nativeOnnxApi.appendExecutionProviderCoreML) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.appendExecutionProviderCoreML(sessionOptions, nativeOnnxCoreMlFlags), "CoreML provider 設定");
            return;
        }
        if (nativeOnnxApi.appendExecutionProvider) {
            const char *genericProviderNames[] = {"CoreML", "CoreMLExecutionProvider"};
            std::ostringstream errorStream;
            for (const char *genericProviderName : genericProviderNames) {
                OrtStatus *callStatus = nativeOnnxApi.appendExecutionProvider(sessionOptions, genericProviderName, nullptr, nullptr, 0);
                if (!callStatus) {
                    return;
                }
                if (errorStream.tellp() > 0) {
                    errorStream << " | ";
                }
                errorStream << genericProviderName << ":" << consumeNativeOnnxStatusMessage(nativeOnnxApi, callStatus);
            }
            throw std::runtime_error("CoreML provider 設定に失敗しました: " + errorStream.str());
        }
    }
    throw std::runtime_error("未対応の execution provider です: " + providerName);
}

static bool tryConfigureNativeOnnxExecutionProvider(NativeOnnxApi &nativeOnnxApi, const std::string &providerName, std::string &errorMessage) {
    if (providerName.empty() || providerName == "CPUExecutionProvider") {
        return true;
    }
    OrtSessionOptions *sessionOptions = nullptr;
    try {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionOptions(&sessionOptions), "SessionOptions 作成");
        appendNativeOnnxExecutionProvider(nativeOnnxApi, sessionOptions, providerName);
        nativeOnnxApi.releaseSessionOptions(sessionOptions);
        return true;
    } catch (const std::exception &exception) {
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
        }
        errorMessage = exception.what();
        return false;
    }
}

static std::string selectNativeOnnxExecutionProvider(NativeOnnxApi &nativeOnnxApi, const std::string &requestedAccelerationMode, const std::vector<std::string> &providerNames, bool &hasUsableGpuProvider) {
    hasUsableGpuProvider = false;
    std::string gpuErrorMessage;
    if (hasNativeOnnxProviderName(providerNames, "CoreMLExecutionProvider")) {
        std::string candidateErrorMessage;
        if (tryConfigureNativeOnnxExecutionProvider(nativeOnnxApi, "CoreMLExecutionProvider", candidateErrorMessage)) {
            hasUsableGpuProvider = true;
            if (requestedAccelerationMode == "auto" || requestedAccelerationMode == "gpu") {
                return "CoreMLExecutionProvider";
            }
        } else if (gpuErrorMessage.empty()) {
            gpuErrorMessage = candidateErrorMessage;
        }
    }
    if (requestedAccelerationMode == "gpu") {
        if (!gpuErrorMessage.empty()) {
            throw std::runtime_error("GPU provider を有効化できません: " + gpuErrorMessage);
        }
        throw std::runtime_error("native backend の ONNX Runtime に利用可能な GPU provider がありません");
    }
    return "CPUExecutionProvider";
}

NativeOnnxRuntimeState createNativeOnnxRuntimeState(const fs::path &onnxruntimeLibraryPath, const std::string &requestedAccelerationMode) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    NativeOnnxRuntimeState runtimeState;
    runtimeState.libraryHandle = nativeOnnxApi.libraryHandle;
    runtimeState.libraryPath = onnxruntimeLibraryPath;
    runtimeState.version = nativeOnnxApi.apiBase && nativeOnnxApi.apiBase->getVersionString ? nativeOnnxApi.apiBase->getVersionString() : "";
    runtimeState.availableProviders = collectNativeOnnxAvailableProviders(nativeOnnxApi);
    runtimeState.requestedAccelerationMode = requestedAccelerationMode.empty() ? "auto" : requestedAccelerationMode;
    runtimeState.selectedExecutionProvider = selectNativeOnnxExecutionProvider(nativeOnnxApi, runtimeState.requestedAccelerationMode, runtimeState.availableProviders, runtimeState.hasUsableGpuProvider);
    runtimeState.isGpuExecutionProviderSelected = runtimeState.selectedExecutionProvider != "CPUExecutionProvider";
    runtimeState.apiVersion = ortApiVersion;
    runtimeState.isLoaded = true;
    nativeOnnxApi.libraryHandle = nullptr;
    return runtimeState;
}

void destroyNativeOnnxRuntimeState(NativeOnnxRuntimeState &runtimeState) {
    if (runtimeState.libraryHandle) {
        closeDynamicLibrary(runtimeState.libraryHandle);
    }
    runtimeState.libraryHandle = nullptr;
    runtimeState.libraryPath.clear();
    runtimeState.version.clear();
    runtimeState.availableProviders.clear();
    runtimeState.apiVersion = 0;
    runtimeState.isLoaded = false;
}

static void ensureNativeOnnxCall(NativeOnnxApi &nativeOnnxApi, OrtStatus *callStatus, const std::string &operationName) {
    if (!callStatus) {
        return;
    }
    std::string errorMessage = nativeOnnxApi.getErrorMessage ? nativeOnnxApi.getErrorMessage(callStatus) : "unknown ONNX Runtime error";
    nativeOnnxApi.releaseStatus(callStatus);
    throw std::runtime_error(operationName + " に失敗しました: " + errorMessage);
}

static std::vector<std::string> collectNativeOnnxAvailableProviders(NativeOnnxApi &nativeOnnxApi) {
    std::vector<std::string> providers;
    if (!nativeOnnxApi.getAvailableProviders || !nativeOnnxApi.releaseAvailableProviders) {
        return providers;
    }
    char **providerPointers = nullptr;
    int providerCount = 0;
    ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getAvailableProviders(&providerPointers, &providerCount), "available providers 取得");
    try {
        for (int providerIndex = 0; providerIndex < providerCount; providerIndex++) {
            if (providerPointers[providerIndex]) {
                providers.push_back(providerPointers[providerIndex]);
            }
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.releaseAvailableProviders(providerPointers, providerCount), "available providers 解放");
    } catch (...) {
        nativeOnnxApi.releaseAvailableProviders(providerPointers, providerCount);
        throw;
    }
    return providers;
}

static void applyNativeOnnxSeedIfConfigured(NativeOnnxApi &nativeOnnxApi) {
    if (!nativeOnnxApi.hasConfiguredSeed || !nativeOnnxApi.setSeed) {
        return;
    }
    ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setSeed(nativeOnnxApi.configuredSeed), "training random seed 設定");
}

static void configureNativeOnnxSessionOptions(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, OrtSessionOptions *sessionOptions, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setSessionGraphOptimizationLevel(sessionOptions, ortGraphOptimizationLevelBasic), "graph optimization 設定");
    if (cpuThreadCount > 0) {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setIntraOpNumThreads(sessionOptions, cpuThreadCount), "intra op thread 設定");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setInterOpNumThreads(sessionOptions, cpuThreadCount), "inter op thread 設定");
    }
    if (shouldUseVvBinConfig) {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.addSessionConfigEntry(sessionOptions, "session.use_vv_bin", "1"), "vv_bin session 設定");
    }
    if (runtimeState) {
        appendNativeOnnxExecutionProvider(nativeOnnxApi, sessionOptions, runtimeState->selectedExecutionProvider);
    }
}

static bool hasNativeOnnxProviderName(const NativeOnnxRuntimeState &runtimeState, const std::string &providerName) {
    return hasNativeOnnxProviderName(runtimeState.availableProviders, providerName);
}

bool hasNativeOnnxGpuProvider(const NativeOnnxRuntimeState &runtimeState) {
    return hasNativeOnnxProviderName(runtimeState, "CUDAExecutionProvider")
        || hasNativeOnnxProviderName(runtimeState, "DmlExecutionProvider")
        || hasNativeOnnxProviderName(runtimeState, "CoreMLExecutionProvider")
        || hasNativeOnnxProviderName(runtimeState, "ROCMExecutionProvider")
        || hasNativeOnnxProviderName(runtimeState, "TensorrtExecutionProvider")
        || hasNativeOnnxProviderName(runtimeState, "MIGraphXExecutionProvider");
}

std::string createNativeOnnxSupportedDevicesJson(const NativeOnnxRuntimeState &runtimeState) {
    bool hasCpuProvider = hasNativeOnnxProviderName(runtimeState, "CPUExecutionProvider");
    bool hasCudaProvider = hasNativeOnnxProviderName(runtimeState, "CUDAExecutionProvider");
    bool hasDmlProvider = hasNativeOnnxProviderName(runtimeState, "DmlExecutionProvider");
    return std::string("{\"cpu\":") + (hasCpuProvider ? "true" : "false")
        + ",\"cuda\":" + (hasCudaProvider ? "true" : "false")
        + ",\"dml\":" + (hasDmlProvider ? "true" : "false") + "}";
}

std::string createNativeOnnxProviderInfoJson(const NativeOnnxRuntimeState &runtimeState) {
    std::ostringstream jsonStream;
    jsonStream << "{\"available\":[";
    for (size_t providerIndex = 0; providerIndex < runtimeState.availableProviders.size(); providerIndex++) {
        if (providerIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << quoteJsonString(runtimeState.availableProviders[providerIndex]);
    }
    jsonStream << "],";
    jsonStream << "\"cpu\":" << (hasNativeOnnxProviderName(runtimeState, "CPUExecutionProvider") ? "true" : "false") << ",";
    jsonStream << "\"cuda\":" << (hasNativeOnnxProviderName(runtimeState, "CUDAExecutionProvider") ? "true" : "false") << ",";
    jsonStream << "\"dml\":" << (hasNativeOnnxProviderName(runtimeState, "DmlExecutionProvider") ? "true" : "false") << ",";
    jsonStream << "\"coreml\":" << (hasNativeOnnxProviderName(runtimeState, "CoreMLExecutionProvider") ? "true" : "false") << ",";
    jsonStream << "\"gpu\":" << (hasNativeOnnxGpuProvider(runtimeState) ? "true" : "false") << ",";
    jsonStream << "\"usable_gpu\":" << (runtimeState.hasUsableGpuProvider ? "true" : "false") << ",";
    jsonStream << "\"selected\":" << quoteJsonString(runtimeState.selectedExecutionProvider) << "}";
    return jsonStream.str();
}

static std::string getNativeOnnxVersion(const NativeOnnxApi &nativeOnnxApi) {
    if (!nativeOnnxApi.apiBase || !nativeOnnxApi.apiBase->getVersionString) {
        return "";
    }
    const char *versionText = nativeOnnxApi.apiBase->getVersionString();
    return versionText ? versionText : "";
}

static std::string formatNativeOnnxElementType(int32_t elementType) {
    switch (elementType) {
        case 1:
            return "float32";
        case 2:
            return "uint8";
        case 3:
            return "int8";
        case 4:
            return "uint16";
        case 5:
            return "int16";
        case 6:
            return "int32";
        case 7:
            return "int64";
        case 9:
            return "bool";
        case 10:
            return "float16";
        case 11:
            return "float64";
        case 12:
            return "uint32";
        case 13:
            return "uint64";
        case 16:
            return "bfloat16";
        default:
            return "type_" + std::to_string(elementType);
    }
}

static std::string formatNativeOnnxShape(const std::vector<int64_t> &dimensions) {
    std::ostringstream shapeStream;
    shapeStream << "[";
    for (size_t dimensionIndex = 0; dimensionIndex < dimensions.size(); dimensionIndex++) {
        if (dimensionIndex > 0) {
            shapeStream << ",";
        }
        if (dimensions[dimensionIndex] < 0) {
            shapeStream << "?";
        } else {
            shapeStream << dimensions[dimensionIndex];
        }
    }
    shapeStream << "]";
    return shapeStream.str();
}

static std::string takeNativeOnnxName(NativeOnnxApi &nativeOnnxApi, OrtAllocator *allocator, char *namePointer) {
    if (!namePointer) {
        return "";
    }
    std::string nameText(namePointer);
    ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.allocatorFree(allocator, namePointer), "name 解放");
    return nameText;
}

static void appendNativeOnnxValueInfo(std::ostringstream &inspectStream, NativeOnnxApi &nativeOnnxApi, OrtSession *session, OrtAllocator *allocator, const std::string &prefixText, size_t valueIndex) {
    char *namePointer = nullptr;
    OrtTypeInfo *typeInfo = nullptr;
    try {
        if (prefixText == "input") {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputName(session, valueIndex, allocator, &namePointer), "input name 取得");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputTypeInfo(session, valueIndex, &typeInfo), "input type 取得");
        } else {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputName(session, valueIndex, allocator, &namePointer), "output name 取得");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputTypeInfo(session, valueIndex, &typeInfo), "output type 取得");
        }
        std::string nameText = takeNativeOnnxName(nativeOnnxApi, allocator, namePointer);
        namePointer = nullptr;
        const OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.castTypeInfoToTensorInfo(typeInfo, &tensorInfo), "tensor info 取得");
        int32_t elementType = 0;
        size_t dimensionCount = 0;
        std::vector<int64_t> dimensions;
        if (tensorInfo) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorElementType(tensorInfo, &elementType), "tensor type 取得");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getDimensionsCount(tensorInfo, &dimensionCount), "dimension count 取得");
            dimensions.resize(dimensionCount);
            if (!dimensions.empty()) {
                ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getDimensions(tensorInfo, dimensions.data(), dimensions.size()), "dimension 取得");
            }
        }
        inspectStream << prefixText << "_" << valueIndex << "\t"
                      << nameText << "\t"
                      << formatNativeOnnxElementType(elementType) << "\t"
                      << formatNativeOnnxShape(dimensions) << "\n";
        nativeOnnxApi.releaseTypeInfo(typeInfo);
    } catch (...) {
        if (namePointer) {
            nativeOnnxApi.allocatorFree(allocator, namePointer);
        }
        if (typeInfo) {
            nativeOnnxApi.releaseTypeInfo(typeInfo);
        }
        throw;
    }
}

struct NativeOnnxValueDescriptor {
    std::string name;
    int32_t elementType = 0;
    std::vector<int64_t> dimensions;
};

struct NativeOnnxTraceInput {
    std::string name;
    int32_t elementType = 0;
    std::vector<int64_t> dimensions;
    std::vector<uint8_t> bytes;
};

static const NativeOnnxTraceInput *findNativeOnnxTraceTensor(const std::vector<NativeOnnxTraceInput> &traceTensors, const std::string &tensorName);
enum class NativeOnnxSingTeacherMode {
    Deterministic,
    VvBin,
};
static NativeOnnxSingTeacherMode getNativeOnnxSingTeacherMode();
static float getNativeOnnxDeterministicSingTeacherSeed();
static std::vector<uint8_t> exportNativeOnnxOptimizedModelBytes(NativeOnnxApi &nativeOnnxApi, const ModelAssetRecord &modelAsset, const std::vector<uint8_t> &modelBytes, uint16_t cpuThreadCount, bool shouldUseVvBinConfig);

struct NativeOnnxCachedSession {
    void *libraryHandle = nullptr;
    OrtEnv *env = nullptr;
    OrtSession *session = nullptr;
    OrtMemoryInfo *memoryInfo = nullptr;
    OrtReleaseEnvFunction releaseEnv = nullptr;
    OrtReleaseMemoryInfoFunction releaseMemoryInfo = nullptr;
    OrtReleaseSessionFunction releaseSession = nullptr;
    std::vector<NativeOnnxValueDescriptor> inputDescriptors;
    std::vector<NativeOnnxValueDescriptor> outputDescriptors;

    ~NativeOnnxCachedSession() {
        if (memoryInfo && releaseMemoryInfo) {
            releaseMemoryInfo(memoryInfo);
        }
        if (session && releaseSession) {
            releaseSession(session);
        }
        if (env && releaseEnv) {
            releaseEnv(env);
        }
        if (libraryHandle) {
            closeDynamicLibrary(libraryHandle);
        }
    }
};

static std::mutex nativeOnnxSessionCacheMutex;
static std::condition_variable nativeOnnxSessionCacheCondition;
static std::map<std::string, std::shared_ptr<NativeOnnxCachedSession>> nativeOnnxSessionCache;
static std::set<std::string> nativeOnnxSessionKeysInProgress;
static uint64_t nativeOnnxSessionCacheHits = 0;
static uint64_t nativeOnnxSessionCacheMisses = 0;
static std::mutex nativeOnnxExportedModelCacheMutex;
static std::condition_variable nativeOnnxExportedModelCacheCondition;
static std::map<std::string, fs::path> nativeOnnxExportedModelCache;
static std::set<std::string> nativeOnnxExportedModelKeysInProgress;
static uint64_t nativeOnnxExportedModelMemoryHits = 0;
static uint64_t nativeOnnxExportedModelMemoryMisses = 0;
static uint64_t nativeOnnxExportedModelFileHits = 0;
static uint64_t nativeOnnxExportedModelFileMisses = 0;
static uint64_t nativeOnnxExportedModelFileWrites = 0;
static uint64_t nativeOnnxExportedModelFileReadErrors = 0;
static uint64_t nativeOnnxExportedModelFileWriteErrors = 0;
static std::atomic_size_t nativeOnnxNextTraceId{0};

std::string createNativeOnnxSessionCacheInfoJson() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
    std::ostringstream jsonStream;
    jsonStream << "{\"loaded\":" << (nativeOnnxSessionCache.empty() ? "false" : "true") << ",";
    jsonStream << "\"session_count\":" << nativeOnnxSessionCache.size() << ",";
    jsonStream << "\"cache_hits\":" << nativeOnnxSessionCacheHits << ",";
    jsonStream << "\"cache_misses\":" << nativeOnnxSessionCacheMisses << ",";
    jsonStream << "\"cache_accesses\":" << (nativeOnnxSessionCacheHits + nativeOnnxSessionCacheMisses) << "}";
    return jsonStream.str();
}

std::string createNativeOnnxExportedModelCacheInfoJson() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
    std::ostringstream jsonStream;
    jsonStream << "{\"loaded\":" << (nativeOnnxExportedModelCache.empty() ? "false" : "true") << ",";
    jsonStream << "\"model_count\":" << nativeOnnxExportedModelCache.size() << ",";
    jsonStream << "\"memory_hits\":" << nativeOnnxExportedModelMemoryHits << ",";
    jsonStream << "\"memory_misses\":" << nativeOnnxExportedModelMemoryMisses << ",";
    jsonStream << "\"memory_accesses\":" << (nativeOnnxExportedModelMemoryHits + nativeOnnxExportedModelMemoryMisses) << ",";
    jsonStream << "\"file_hits\":" << nativeOnnxExportedModelFileHits << ",";
    jsonStream << "\"file_misses\":" << nativeOnnxExportedModelFileMisses << ",";
    jsonStream << "\"file_writes\":" << nativeOnnxExportedModelFileWrites << ",";
    jsonStream << "\"file_read_errors\":" << nativeOnnxExportedModelFileReadErrors << ",";
    jsonStream << "\"file_write_errors\":" << nativeOnnxExportedModelFileWriteErrors << "}";
    return jsonStream.str();
}

std::string createNativeOnnxDeterministicSingTeacherInfoJson(bool shouldUseVvBinConfig) {
    NativeOnnxSingTeacherMode teacherMode = getNativeOnnxSingTeacherMode();
    std::ostringstream jsonStream;
    jsonStream << "{\"enabled\":true,";
    if (teacherMode == NativeOnnxSingTeacherMode::Deterministic) {
        float seedValue = getNativeOnnxDeterministicSingTeacherSeed();
        jsonStream << "\"mode\":\"seeded_exported_onnx\",";
        jsonStream << "\"deterministic\":true,";
        jsonStream << "\"seed\":" << std::setprecision(9) << static_cast<double>(seedValue) << "}";
        return jsonStream.str();
    }
    jsonStream << "\"mode\":\"" << (shouldUseVvBinConfig ? "vv_bin" : "exported_onnx") << "\",";
    jsonStream << "\"deterministic\":false,";
    jsonStream << "\"seed\":0}";
    return jsonStream.str();
}

struct NativeOnnxAudioQuerySettings {
    float speedScale = 1.0f;
    float pitchScale = 0.0f;
    float intonationScale = 1.0f;
    float volumeScale = 1.0f;
    float prePhonemeLength = 0.1f;
    float postPhonemeLength = 0.1f;
    uint32_t outputSamplingRate = 24000;
    bool outputStereo = false;
};

struct NativeOnnxMora {
    std::string text;
    std::string consonant;
    std::string vowel;
    float consonantLength = 0.0f;
    float vowelLength = 0.0f;
    float pitch = 0.0f;
    bool hasConsonant = false;
    bool hasConsonantLength = false;
    bool hasVowelLength = false;
    bool hasPitch = false;
};

struct NativeOnnxAccentPhrase {
    std::vector<NativeOnnxMora> moras;
    NativeOnnxMora pauseMora;
    bool hasPauseMora = false;
    bool isInterrogative = false;
    int64_t accent = 1;
};

struct NativeOnnxDecoderChunkInputSet {
    std::vector<NativeOnnxTraceInput> tensors;
    size_t frontCropFrames = 0;
    size_t backCropFrames = 0;
};

struct NativeOnnxScoreNote {
    std::string lyric;
    std::string noteId;
    NativeAudioQueryMora mora;
    int64_t key = -1;
    uint64_t frameLength = 0;
    bool hasKey = false;
    bool hasNoteId = false;
    bool hasMora = false;
};

struct NativeOnnxFramePhoneme {
    std::string phoneme;
    std::string noteId;
    uint64_t frameLength = 0;
    bool hasNoteId = false;
};

struct NativeOnnxFrameAudioQuery {
    std::vector<float> f0Values;
    std::vector<float> volumeValues;
    std::vector<NativeOnnxFramePhoneme> phonemes;
    float volumeScale = 1.0f;
    uint32_t outputSamplingRate = 24000;
    bool outputStereo = false;
};

struct NativeOnnxSongPhonemeFeature {
    std::string phoneme;
    std::string noteId;
    int64_t phonemeCode = 0;
    int64_t key = -1;
    bool hasNoteId = false;
};

struct NativeOnnxSongFrameInputs {
    std::vector<int64_t> phonemeValues;
    std::vector<int64_t> keyValues;
};

static int32_t parseNativeOnnxElementType(const std::string &typeText) {
    if (typeText == "float32" || typeText == "float") {
        return 1;
    }
    if (typeText == "int32") {
        return 6;
    }
    if (typeText == "int64") {
        return 7;
    }
    if (typeText == "bool") {
        return 9;
    }
    throw std::runtime_error("未対応の dtype です: " + typeText);
}

static NativeOnnxValueDescriptor readNativeOnnxValueDescriptor(NativeOnnxApi &nativeOnnxApi, OrtSession *session, OrtAllocator *allocator, bool isInput, size_t valueIndex) {
    char *namePointer = nullptr;
    OrtTypeInfo *typeInfo = nullptr;
    try {
        NativeOnnxValueDescriptor valueDescriptor;
        if (isInput) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputName(session, valueIndex, allocator, &namePointer), "input name 取得");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputTypeInfo(session, valueIndex, &typeInfo), "input type 取得");
        } else {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputName(session, valueIndex, allocator, &namePointer), "output name 取得");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputTypeInfo(session, valueIndex, &typeInfo), "output type 取得");
        }
        valueDescriptor.name = takeNativeOnnxName(nativeOnnxApi, allocator, namePointer);
        namePointer = nullptr;
        const OrtTensorTypeAndShapeInfo *tensorInfo = nullptr;
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.castTypeInfoToTensorInfo(typeInfo, &tensorInfo), "tensor info 取得");
        if (tensorInfo) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorElementType(tensorInfo, &valueDescriptor.elementType), "tensor type 取得");
            size_t dimensionCount = 0;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getDimensionsCount(tensorInfo, &dimensionCount), "dimension count 取得");
            valueDescriptor.dimensions.resize(dimensionCount);
            if (!valueDescriptor.dimensions.empty()) {
                ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getDimensions(tensorInfo, valueDescriptor.dimensions.data(), valueDescriptor.dimensions.size()), "dimension 取得");
            }
        }
        nativeOnnxApi.releaseTypeInfo(typeInfo);
        return valueDescriptor;
    } catch (...) {
        if (namePointer) {
            nativeOnnxApi.allocatorFree(allocator, namePointer);
        }
        if (typeInfo) {
            nativeOnnxApi.releaseTypeInfo(typeInfo);
        }
        throw;
    }
}

static bool isNativeOnnxTraceJsonPath(const fs::path &filePath, const std::string &nameMarker) {
    std::string filename = filePath.filename().string();
    return filename.find(nameMarker) != std::string::npos && filePath.extension() == ".json";
}

static std::vector<NativeOnnxTraceInput> loadNativeOnnxTraceTensors(const fs::path &inputDirectory, const std::string &nameMarker, const std::string &directoryLabel) {
    if (inputDirectory.empty()) {
        return {};
    }
    ensurePathExists(inputDirectory, directoryLabel);
    std::vector<fs::path> jsonPaths;
    for (const fs::directory_entry &directoryEntry : fs::directory_iterator(inputDirectory)) {
        if (directoryEntry.is_regular_file() && isNativeOnnxTraceJsonPath(directoryEntry.path(), nameMarker)) {
            jsonPaths.push_back(directoryEntry.path());
        }
    }
    std::sort(jsonPaths.begin(), jsonPaths.end());
    std::vector<NativeOnnxTraceInput> traceInputs;
    for (const fs::path &jsonPath : jsonPaths) {
        std::string jsonText = readNativeOnnxTextFile(jsonPath);
        std::string binaryName = extractNativeOnnxJsonStringField(jsonText, "binary");
        if (binaryName.empty()) {
            throw std::runtime_error("binary がありません: " + jsonPath.string());
        }
        std::string dtypeText = extractNativeOnnxJsonStringField(jsonText, "dtype");
        std::string nameText = extractNativeOnnxJsonStringField(jsonText, "name");
        NativeOnnxTraceInput traceInput;
        traceInput.name = nameText;
        traceInput.elementType = parseNativeOnnxElementType(dtypeText);
        traceInput.dimensions = extractNativeOnnxJsonShapeField(jsonText);
        traceInput.bytes = readNativeOnnxBinaryFile(inputDirectory / binaryName);
        traceInputs.push_back(std::move(traceInput));
    }
    return traceInputs;
}

static std::vector<NativeOnnxTraceInput> loadNativeOnnxTraceInputs(const fs::path &inputDirectory) {
    return loadNativeOnnxTraceTensors(inputDirectory, "-input-", "trace input directory");
}

static std::vector<NativeOnnxTraceInput> loadNativeOnnxTraceOutputs(const fs::path &inputDirectory) {
    return loadNativeOnnxTraceTensors(inputDirectory, "-output-", "trace output directory");
}

static fs::path getNativeOnnxTensorTraceDirectory() {
    const char *directoryText = std::getenv("LITEVOX_TENSOR_TRACE_DIR");
    if (!directoryText || directoryText[0] == '\0') {
        return {};
    }
    return fs::path(directoryText);
}

static ModelAssetRecord createNativeOnnxPseudoModelAsset(const fs::path &modelPath) {
    ModelAssetRecord modelAsset;
    modelAsset.archivePath = modelPath.parent_path();
    modelAsset.entryName = modelPath.filename().string();
    return modelAsset;
}

static std::string sanitizeNativeOnnxTraceName(const std::string &nameText) {
    std::string sanitizedText;
    sanitizedText.reserve(nameText.size());
    for (char character : nameText) {
        unsigned char unsignedCharacter = static_cast<unsigned char>(character);
        if (std::isalnum(unsignedCharacter) || character == '_' || character == '-') {
            sanitizedText.push_back(character);
        } else {
            sanitizedText.push_back('_');
        }
    }
    if (sanitizedText.empty()) {
        return "tensor";
    }
    return sanitizedText;
}

static std::vector<uint8_t> rewriteNativeOnnxModelRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount);

static std::string createNativeOnnxTraceShapeJson(const std::vector<int64_t> &dimensions) {
    std::ostringstream jsonStream;
    jsonStream << "[";
    for (size_t dimensionIndex = 0; dimensionIndex < dimensions.size(); dimensionIndex++) {
        if (dimensionIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << dimensions[dimensionIndex];
    }
    jsonStream << "]";
    return jsonStream.str();
}

static std::string inferNativeOnnxTraceOperationName(const ModelAssetRecord &modelAsset, const std::vector<NativeOnnxTraceInput> &inputTensors) {
    auto hasInput = [&](const std::string &tensorName) {
        return findNativeOnnxTraceTensor(inputTensors, tensorName) != nullptr;
    };
    if (hasInput("phoneme_list")) {
        return "predict_duration";
    }
    if (hasInput("vowel_phoneme_list")) {
        return "predict_intonation";
    }
    if (hasInput("spec")) {
        return "render_audio_segment";
    }
    if (hasInput("f0") && hasInput("phoneme")) {
        return "decode";
    }
    return sanitizeNativeOnnxTraceName(fs::path(modelAsset.entryName).stem().string());
}

static void writeNativeOnnxTraceTensorFile(const fs::path &traceDirectory, size_t traceId, const std::string &roleText, const NativeOnnxTraceInput &tensor) {
    fs::create_directories(traceDirectory);
    std::ostringstream filePrefixStream;
    filePrefixStream << std::setfill('0') << std::setw(4) << traceId << "-" << roleText << "-" << sanitizeNativeOnnxTraceName(tensor.name);
    fs::path filePrefix = traceDirectory / filePrefixStream.str();
    fs::path binaryPath = filePrefix;
    binaryPath += ".bin";
    fs::path metadataPath = filePrefix;
    metadataPath += ".json";
    writeBinaryFile(binaryPath, tensor.bytes);
    std::ostringstream metadataStream;
    metadataStream << "{";
    metadataStream << "\"trace_id\":" << traceId << ",";
    metadataStream << "\"role\":" << quoteJsonString(roleText) << ",";
    metadataStream << "\"name\":" << quoteJsonString(tensor.name) << ",";
    metadataStream << "\"dtype\":" << quoteJsonString(formatNativeOnnxElementType(tensor.elementType)) << ",";
    metadataStream << "\"shape\":" << createNativeOnnxTraceShapeJson(tensor.dimensions) << ",";
    metadataStream << "\"binary\":" << quoteJsonString(binaryPath.filename().string());
    metadataStream << "}";
    writeTextFile(metadataPath, metadataStream.str());
}

static void writeNativeOnnxTensorTrace(const ModelAssetRecord &modelAsset, const std::vector<NativeOnnxTraceInput> &inputTensors, const std::vector<NativeOnnxTraceInput> &outputTensors) {
    fs::path traceDirectory = getNativeOnnxTensorTraceDirectory();
    if (traceDirectory.empty()) {
        return;
    }
    size_t traceId = nativeOnnxNextTraceId.fetch_add(1);
    for (const NativeOnnxTraceInput &inputTensor : inputTensors) {
        writeNativeOnnxTraceTensorFile(traceDirectory, traceId, "input", inputTensor);
    }
    for (const NativeOnnxTraceInput &outputTensor : outputTensors) {
        writeNativeOnnxTraceTensorFile(traceDirectory, traceId, "output", outputTensor);
    }
    std::ostringstream sessionStream;
    sessionStream << "{";
    sessionStream << "\"trace_id\":" << traceId << ",";
    sessionStream << "\"operation\":" << quoteJsonString(inferNativeOnnxTraceOperationName(modelAsset, inputTensors)) << ",";
    sessionStream << "\"asset\":" << quoteJsonString(modelAsset.entryName) << ",";
    sessionStream << "\"outputs\":" << outputTensors.size();
    sessionStream << "}";
    std::ostringstream sessionNameStream;
    sessionNameStream << std::setfill('0') << std::setw(4) << traceId << "-session.json";
    writeTextFile(traceDirectory / sessionNameStream.str(), sessionStream.str());
}

static NativeOnnxAudioQuerySettings parseNativeOnnxAudioQuerySettings(const std::string &audioQueryText) {
    NativeOnnxAudioQuerySettings audioQuerySettings;
    audioQuerySettings.speedScale = static_cast<float>(extractNativeOnnxJsonNumberField(audioQueryText, "speedScale", audioQuerySettings.speedScale));
    audioQuerySettings.pitchScale = static_cast<float>(extractNativeOnnxJsonNumberField(audioQueryText, "pitchScale", audioQuerySettings.pitchScale));
    audioQuerySettings.intonationScale = static_cast<float>(extractNativeOnnxJsonNumberField(audioQueryText, "intonationScale", audioQuerySettings.intonationScale));
    audioQuerySettings.volumeScale = static_cast<float>(extractNativeOnnxJsonNumberField(audioQueryText, "volumeScale", audioQuerySettings.volumeScale));
    audioQuerySettings.prePhonemeLength = static_cast<float>(extractNativeOnnxJsonNumberField(audioQueryText, "prePhonemeLength", audioQuerySettings.prePhonemeLength));
    audioQuerySettings.postPhonemeLength = static_cast<float>(extractNativeOnnxJsonNumberField(audioQueryText, "postPhonemeLength", audioQuerySettings.postPhonemeLength));
    audioQuerySettings.outputSamplingRate = static_cast<uint32_t>(extractNativeOnnxJsonNumberField(audioQueryText, "outputSamplingRate", audioQuerySettings.outputSamplingRate));
    audioQuerySettings.outputStereo = extractNativeOnnxJsonBoolField(audioQueryText, "outputStereo", audioQuerySettings.outputStereo);
    return audioQuerySettings;
}

static fs::path resolveNativeOnnxAudioQueryPath(const fs::path &inputDirectory, const fs::path &audioQueryPath) {
    if (!audioQueryPath.empty()) {
        ensurePathExists(audioQueryPath, "audio query");
        return audioQueryPath;
    }
    if (!inputDirectory.empty()) {
        fs::path tracedAudioQueryPath = inputDirectory.parent_path() / "audio_query.json";
        if (fs::exists(tracedAudioQueryPath)) {
            return tracedAudioQueryPath;
        }
    }
    throw std::runtime_error("--audio-query または trace 由来の audio_query.json が必要です");
}

static const NativeOnnxTraceInput *findNativeOnnxTraceTensor(const std::vector<NativeOnnxTraceInput> &traceTensors, const std::string &tensorName) {
    for (const NativeOnnxTraceInput &traceTensor : traceTensors) {
        if (traceTensor.name == tensorName) {
            return &traceTensor;
        }
    }
    return nullptr;
}

static bool areNativeOnnxDimensionsEqual(const std::vector<int64_t> &leftDimensions, const std::vector<int64_t> &rightDimensions) {
    if (leftDimensions.size() != rightDimensions.size()) {
        return false;
    }
    for (size_t dimensionIndex = 0; dimensionIndex < leftDimensions.size(); dimensionIndex++) {
        if (leftDimensions[dimensionIndex] >= 0 && rightDimensions[dimensionIndex] >= 0 && leftDimensions[dimensionIndex] != rightDimensions[dimensionIndex]) {
            return false;
        }
    }
    return true;
}

static size_t calculatePositiveShapeElementCount(const std::vector<int64_t> &dimensions) {
    size_t elementCount = 1;
    for (int64_t dimension : dimensions) {
        if (dimension <= 0) {
            throw std::runtime_error("固定 shape ではありません");
        }
        elementCount *= static_cast<size_t>(dimension);
    }
    return elementCount;
}

static size_t getNativeOnnxElementByteCount(int32_t elementType) {
    switch (elementType) {
        case 1:
            return sizeof(float);
        case 6:
            return sizeof(int32_t);
        case 7:
            return sizeof(int64_t);
        case 9:
            return sizeof(uint8_t);
        default:
            return 0;
    }
}

static bool canCreateNativeOnnxSmokeInput(const NativeOnnxValueDescriptor &inputDescriptor) {
    if (inputDescriptor.dimensions.empty() || getNativeOnnxElementByteCount(inputDescriptor.elementType) == 0) {
        return false;
    }
    for (int64_t dimension : inputDescriptor.dimensions) {
        if (dimension <= 0) {
            return false;
        }
    }
    return true;
}

static bool canRunNativeOnnxSmokeTest(const std::vector<NativeOnnxValueDescriptor> &inputDescriptors, const std::vector<NativeOnnxValueDescriptor> &outputDescriptors, const std::vector<NativeOnnxTraceInput> &traceInputs) {
    if (inputDescriptors.empty() || outputDescriptors.empty()) {
        return false;
    }
    for (const NativeOnnxValueDescriptor &inputDescriptor : inputDescriptors) {
        const NativeOnnxTraceInput *traceInput = findNativeOnnxTraceTensor(traceInputs, inputDescriptor.name);
        if (traceInput) {
            if (traceInput->elementType != inputDescriptor.elementType || !areNativeOnnxDimensionsEqual(inputDescriptor.dimensions, traceInput->dimensions)) {
                return false;
            }
        } else if (!canCreateNativeOnnxSmokeInput(inputDescriptor)) {
            return false;
        }
    }
    return true;
}

static std::vector<uint8_t> createNativeOnnxInputBytes(const NativeOnnxValueDescriptor &inputDescriptor, size_t inputIndex, size_t elementCount) {
    size_t elementBytes = getNativeOnnxElementByteCount(inputDescriptor.elementType);
    std::vector<uint8_t> inputBytes(elementCount * elementBytes);
    if (inputDescriptor.elementType == 1) {
        std::vector<float> values(elementCount);
        for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
            values[valueIndex] = static_cast<float>(valueIndex + 1 + inputIndex);
        }
        std::memcpy(inputBytes.data(), values.data(), inputBytes.size());
    } else if (inputDescriptor.elementType == 6) {
        std::vector<int32_t> values(elementCount);
        for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
            values[valueIndex] = static_cast<int32_t>(valueIndex + 1 + inputIndex);
        }
        std::memcpy(inputBytes.data(), values.data(), inputBytes.size());
    } else if (inputDescriptor.elementType == 7) {
        std::vector<int64_t> values(elementCount);
        for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
            values[valueIndex] = static_cast<int64_t>(valueIndex + 1 + inputIndex);
        }
        std::memcpy(inputBytes.data(), values.data(), inputBytes.size());
    } else if (inputDescriptor.elementType == 9) {
        for (size_t valueIndex = 0; valueIndex < inputBytes.size(); valueIndex++) {
            inputBytes[valueIndex] = (valueIndex + inputIndex) % 2 == 0 ? 1 : 0;
        }
    }
    return inputBytes;
}

static std::string readNativeOnnxFirstValueText(const NativeOnnxValueDescriptor &outputDescriptor, void *outputData, size_t outputElementCount) {
    if (!outputData || outputElementCount == 0) {
        return "";
    }
    if (outputDescriptor.elementType == 1) {
        return std::to_string(static_cast<float *>(outputData)[0]);
    }
    if (outputDescriptor.elementType == 6) {
        return std::to_string(static_cast<int32_t *>(outputData)[0]);
    }
    if (outputDescriptor.elementType == 7) {
        return std::to_string(static_cast<int64_t *>(outputData)[0]);
    }
    if (outputDescriptor.elementType == 9) {
        return static_cast<uint8_t *>(outputData)[0] ? "true" : "false";
    }
    return "";
}

static std::vector<int64_t> readNativeOnnxTensorDimensions(NativeOnnxApi &nativeOnnxApi, const OrtTensorTypeAndShapeInfo *tensorShapeInfo) {
    size_t dimensionCount = 0;
    ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getDimensionsCount(tensorShapeInfo, &dimensionCount), "output dimension count 取得");
    std::vector<int64_t> dimensions(dimensionCount);
    if (!dimensions.empty()) {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getDimensions(tensorShapeInfo, dimensions.data(), dimensions.size()), "output dimension 取得");
    }
    return dimensions;
}

static std::string compareNativeOnnxTraceOutput(const NativeOnnxValueDescriptor &outputDescriptor, const std::vector<int64_t> &outputDimensions, const void *outputPointer, size_t outputByteCount, const std::vector<NativeOnnxTraceInput> &traceOutputs) {
    const NativeOnnxTraceInput *traceOutput = findNativeOnnxTraceTensor(traceOutputs, outputDescriptor.name);
    if (!traceOutput) {
        return "not_found";
    }
    if (traceOutput->elementType != outputDescriptor.elementType) {
        return "type_mismatch";
    }
    if (!areNativeOnnxDimensionsEqual(outputDimensions, traceOutput->dimensions)) {
        return "shape_mismatch";
    }
    if (outputByteCount != traceOutput->bytes.size()) {
        return "byte_size_mismatch";
    }
    if (!outputPointer && outputByteCount > 0) {
        return "output_null";
    }
    if (outputByteCount == 0 || std::memcmp(outputPointer, traceOutput->bytes.data(), outputByteCount) == 0) {
        return "exact";
    }
    if (outputDescriptor.elementType == 1) {
        const float *actualValues = static_cast<const float *>(outputPointer);
        const float *expectedValues = reinterpret_cast<const float *>(traceOutput->bytes.data());
        size_t valueCount = outputByteCount / sizeof(float);
        float maxDifference = 0.0f;
        for (size_t valueIndex = 0; valueIndex < valueCount; valueIndex++) {
            float difference = actualValues[valueIndex] > expectedValues[valueIndex] ? actualValues[valueIndex] - expectedValues[valueIndex] : expectedValues[valueIndex] - actualValues[valueIndex];
            if (difference > maxDifference) {
                maxDifference = difference;
            }
        }
        return "float32_diff_max=" + std::to_string(maxDifference);
    }
    return "bytes_mismatch";
}

static const NativeOnnxTraceInput &requireNativeOnnxTensor(const std::vector<NativeOnnxTraceInput> &traceTensors, const std::string &tensorName) {
    const NativeOnnxTraceInput *traceTensor = findNativeOnnxTraceTensor(traceTensors, tensorName);
    if (!traceTensor) {
        throw std::runtime_error("trace tensor がありません: " + tensorName);
    }
    return *traceTensor;
}

template <typename TensorValueType>
static std::vector<TensorValueType> readNativeOnnxTensorValues(const NativeOnnxTraceInput &tensor, int32_t expectedElementType) {
    if (tensor.elementType != expectedElementType) {
        throw std::runtime_error("tensor dtype が一致しません: " + tensor.name);
    }
    if (tensor.bytes.size() % sizeof(TensorValueType) != 0) {
        throw std::runtime_error("tensor byte size が一致しません: " + tensor.name);
    }
    std::vector<TensorValueType> values(tensor.bytes.size() / sizeof(TensorValueType));
    if (!values.empty()) {
        std::memcpy(values.data(), tensor.bytes.data(), tensor.bytes.size());
    }
    return values;
}

template <typename TensorValueType>
static std::vector<uint8_t> createNativeOnnxTensorBytes(const std::vector<TensorValueType> &values) {
    std::vector<uint8_t> tensorBytes(values.size() * sizeof(TensorValueType));
    if (!tensorBytes.empty()) {
        std::memcpy(tensorBytes.data(), values.data(), tensorBytes.size());
    }
    return tensorBytes;
}

static std::string compareNativeOnnxTensors(const NativeOnnxTraceInput &actualTensor, const NativeOnnxTraceInput *expectedTensor) {
    if (!expectedTensor) {
        return "not_found";
    }
    if (actualTensor.elementType != expectedTensor->elementType) {
        return "type_mismatch";
    }
    if (!areNativeOnnxDimensionsEqual(actualTensor.dimensions, expectedTensor->dimensions)) {
        return "shape_mismatch";
    }
    if (actualTensor.bytes.size() != expectedTensor->bytes.size()) {
        return "byte_size_mismatch";
    }
    if (actualTensor.bytes.empty() || std::memcmp(actualTensor.bytes.data(), expectedTensor->bytes.data(), actualTensor.bytes.size()) == 0) {
        return "exact";
    }
    if (actualTensor.elementType == 1) {
        const float *actualValues = reinterpret_cast<const float *>(actualTensor.bytes.data());
        const float *expectedValues = reinterpret_cast<const float *>(expectedTensor->bytes.data());
        size_t valueCount = actualTensor.bytes.size() / sizeof(float);
        float maxDifference = 0.0f;
        for (size_t valueIndex = 0; valueIndex < valueCount; valueIndex++) {
            float difference = actualValues[valueIndex] > expectedValues[valueIndex] ? actualValues[valueIndex] - expectedValues[valueIndex] : expectedValues[valueIndex] - actualValues[valueIndex];
            if (difference > maxDifference) {
                maxDifference = difference;
            }
        }
        return "float32_diff_max=" + std::to_string(maxDifference);
    }
    return "bytes_mismatch";
}

static std::shared_ptr<NativeOnnxCachedSession> createNativeOnnxCachedSession(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<uint8_t> &modelBytes, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::shared_ptr<NativeOnnxCachedSession> cachedSession = std::make_shared<NativeOnnxCachedSession>();
    OrtSessionOptions *sessionOptions = nullptr;
    OrtAllocator *allocator = nullptr;
    try {
        cachedSession->releaseEnv = nativeOnnxApi.releaseEnv;
        cachedSession->releaseMemoryInfo = nativeOnnxApi.releaseMemoryInfo;
        cachedSession->releaseSession = nativeOnnxApi.releaseSession;
        cachedSession->libraryHandle = openDynamicLibrary(nativeOnnxApi.libraryPath);
        if (!cachedSession->libraryHandle) {
            throw std::runtime_error(std::string("ONNX Runtime を保持できません: ") + getDynamicLibraryErrorText());
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createEnv(ortLoggingLevelWarning, "litevox-native-cache", &cachedSession->env), "OrtEnv 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getAllocatorWithDefaultOptions(&allocator), "default allocator 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionOptions(&sessionOptions), "SessionOptions 作成");
        configureNativeOnnxSessionOptions(nativeOnnxApi, runtimeState, sessionOptions, cpuThreadCount, shouldUseVvBinConfig);
        applyNativeOnnxSeedIfConfigured(nativeOnnxApi);
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionFromArray(cachedSession->env, modelBytes.data(), modelBytes.size(), sessionOptions, &cachedSession->session), "ONNX session 作成");
        size_t inputCount = 0;
        size_t outputCount = 0;
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputCount(cachedSession->session, &inputCount), "input count 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputCount(cachedSession->session, &outputCount), "output count 取得");
        cachedSession->inputDescriptors.reserve(inputCount);
        cachedSession->outputDescriptors.reserve(outputCount);
        for (size_t inputIndex = 0; inputIndex < inputCount; inputIndex++) {
            cachedSession->inputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, cachedSession->session, allocator, true, inputIndex));
        }
        for (size_t outputIndex = 0; outputIndex < outputCount; outputIndex++) {
            cachedSession->outputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, cachedSession->session, allocator, false, outputIndex));
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createCpuMemoryInfo(0, 0, &cachedSession->memoryInfo), "CPU memory info 作成");
        nativeOnnxApi.releaseSessionOptions(sessionOptions);
        return cachedSession;
    } catch (...) {
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
        }
        throw;
    }
}

static std::shared_ptr<NativeOnnxCachedSession> createNativeOnnxCachedSession(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const fs::path &modelPath, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::shared_ptr<NativeOnnxCachedSession> cachedSession = std::make_shared<NativeOnnxCachedSession>();
    OrtSessionOptions *sessionOptions = nullptr;
    OrtAllocator *allocator = nullptr;
    try {
        cachedSession->releaseEnv = nativeOnnxApi.releaseEnv;
        cachedSession->releaseMemoryInfo = nativeOnnxApi.releaseMemoryInfo;
        cachedSession->releaseSession = nativeOnnxApi.releaseSession;
        cachedSession->libraryHandle = openDynamicLibrary(nativeOnnxApi.libraryPath);
        if (!cachedSession->libraryHandle) {
            throw std::runtime_error(std::string("ONNX Runtime を保持できません: ") + getDynamicLibraryErrorText());
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createEnv(ortLoggingLevelWarning, "litevox-native-cache", &cachedSession->env), "OrtEnv 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getAllocatorWithDefaultOptions(&allocator), "default allocator 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionOptions(&sessionOptions), "SessionOptions 作成");
        configureNativeOnnxSessionOptions(nativeOnnxApi, runtimeState, sessionOptions, cpuThreadCount, shouldUseVvBinConfig);
        applyNativeOnnxSeedIfConfigured(nativeOnnxApi);
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSession(cachedSession->env, modelPath.c_str(), sessionOptions, &cachedSession->session), "ONNX session 作成");
        size_t inputCount = 0;
        size_t outputCount = 0;
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputCount(cachedSession->session, &inputCount), "input count 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputCount(cachedSession->session, &outputCount), "output count 取得");
        cachedSession->inputDescriptors.reserve(inputCount);
        cachedSession->outputDescriptors.reserve(outputCount);
        for (size_t inputIndex = 0; inputIndex < inputCount; inputIndex++) {
            cachedSession->inputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, cachedSession->session, allocator, true, inputIndex));
        }
        for (size_t outputIndex = 0; outputIndex < outputCount; outputIndex++) {
            cachedSession->outputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, cachedSession->session, allocator, false, outputIndex));
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createCpuMemoryInfo(0, 0, &cachedSession->memoryInfo), "CPU memory info 作成");
        nativeOnnxApi.releaseSessionOptions(sessionOptions);
        return cachedSession;
    } catch (...) {
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
        }
        throw;
    }
}

static std::shared_ptr<NativeOnnxCachedSession> getNativeOnnxCachedSession(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<uint8_t> &modelBytes, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const std::string &sessionCacheKey) {
    {
        std::unique_lock<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        while (true) {
            auto cacheIterator = nativeOnnxSessionCache.find(sessionCacheKey);
            if (cacheIterator != nativeOnnxSessionCache.end()) {
                nativeOnnxSessionCacheHits++;
                return cacheIterator->second;
            }
            if (nativeOnnxSessionKeysInProgress.insert(sessionCacheKey).second) {
                nativeOnnxSessionCacheMisses++;
                break;
            }
            nativeOnnxSessionCacheCondition.wait(cacheLock);
        }
    }
    try {
        std::shared_ptr<NativeOnnxCachedSession> cachedSession = createNativeOnnxCachedSession(nativeOnnxApi, runtimeState, modelBytes, cpuThreadCount, shouldUseVvBinConfig);
        std::lock_guard<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        nativeOnnxSessionCache[sessionCacheKey] = cachedSession;
        nativeOnnxSessionKeysInProgress.erase(sessionCacheKey);
        nativeOnnxSessionCacheCondition.notify_all();
        return cachedSession;
    } catch (...) {
        std::lock_guard<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        nativeOnnxSessionKeysInProgress.erase(sessionCacheKey);
        nativeOnnxSessionCacheCondition.notify_all();
        throw;
    }
}

static std::shared_ptr<NativeOnnxCachedSession> getNativeOnnxCachedSession(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const fs::path &modelPath, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const std::string &sessionCacheKey) {
    {
        std::unique_lock<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        while (true) {
            auto cacheIterator = nativeOnnxSessionCache.find(sessionCacheKey);
            if (cacheIterator != nativeOnnxSessionCache.end()) {
                nativeOnnxSessionCacheHits++;
                return cacheIterator->second;
            }
            if (nativeOnnxSessionKeysInProgress.insert(sessionCacheKey).second) {
                nativeOnnxSessionCacheMisses++;
                break;
            }
            nativeOnnxSessionCacheCondition.wait(cacheLock);
        }
    }
    try {
        std::shared_ptr<NativeOnnxCachedSession> cachedSession = createNativeOnnxCachedSession(nativeOnnxApi, runtimeState, modelPath, cpuThreadCount, shouldUseVvBinConfig);
        std::lock_guard<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        nativeOnnxSessionCache[sessionCacheKey] = cachedSession;
        nativeOnnxSessionKeysInProgress.erase(sessionCacheKey);
        nativeOnnxSessionCacheCondition.notify_all();
        return cachedSession;
    } catch (...) {
        std::lock_guard<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        nativeOnnxSessionKeysInProgress.erase(sessionCacheKey);
        nativeOnnxSessionCacheCondition.notify_all();
        throw;
    }
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxPreparedSession(NativeOnnxApi &nativeOnnxApi, const std::shared_ptr<NativeOnnxCachedSession> &cachedSession, const std::vector<NativeOnnxTraceInput> &inputTensors) {
    std::vector<OrtValue *> inputValues;
    std::vector<OrtValue *> outputValues(cachedSession->outputDescriptors.size(), nullptr);
    try {
        applyNativeOnnxSeedIfConfigured(nativeOnnxApi);
        std::vector<const char *> inputNames;
        std::vector<const OrtValue *> inputPointers;
        inputNames.reserve(cachedSession->inputDescriptors.size());
        inputPointers.reserve(cachedSession->inputDescriptors.size());
        inputValues.reserve(cachedSession->inputDescriptors.size());
        for (const NativeOnnxValueDescriptor &inputDescriptor : cachedSession->inputDescriptors) {
            const NativeOnnxTraceInput &inputTensor = requireNativeOnnxTensor(inputTensors, inputDescriptor.name);
            if (inputTensor.elementType != inputDescriptor.elementType) {
                throw std::runtime_error("input dtype が一致しません: " + inputDescriptor.name);
            }
            if (!areNativeOnnxDimensionsEqual(inputDescriptor.dimensions, inputTensor.dimensions)) {
                throw std::runtime_error("input shape が一致しません: " + inputDescriptor.name);
            }
            OrtValue *inputValue = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createTensorWithDataAsOrtValue(cachedSession->memoryInfo, const_cast<uint8_t *>(inputTensor.bytes.data()), inputTensor.bytes.size(), inputTensor.dimensions.data(), inputTensor.dimensions.size(), inputDescriptor.elementType, &inputValue), "input tensor 作成");
            inputValues.push_back(inputValue);
            inputPointers.push_back(inputValue);
            inputNames.push_back(inputDescriptor.name.c_str());
        }
        std::vector<const char *> outputNames;
        outputNames.reserve(cachedSession->outputDescriptors.size());
        for (const NativeOnnxValueDescriptor &outputDescriptor : cachedSession->outputDescriptors) {
            outputNames.push_back(outputDescriptor.name.c_str());
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.run(cachedSession->session, nullptr, inputNames.data(), inputPointers.data(), inputPointers.size(), outputNames.data(), outputNames.size(), outputValues.data()), "ONNX Run");
        std::vector<NativeOnnxTraceInput> outputTensors;
        outputTensors.reserve(cachedSession->outputDescriptors.size());
        for (size_t outputIndex = 0; outputIndex < outputValues.size(); outputIndex++) {
            OrtTensorTypeAndShapeInfo *outputShapeInfo = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorTypeAndShape(outputValues[outputIndex], &outputShapeInfo), "output tensor info 取得");
            size_t outputElementCount = 0;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorShapeElementCount(outputShapeInfo, &outputElementCount), "output element count 取得");
            std::vector<int64_t> outputDimensions = readNativeOnnxTensorDimensions(nativeOnnxApi, outputShapeInfo);
            void *outputPointer = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorMutableData(outputValues[outputIndex], &outputPointer), "output tensor data 取得");
            size_t outputByteCount = outputElementCount * getNativeOnnxElementByteCount(cachedSession->outputDescriptors[outputIndex].elementType);
            NativeOnnxTraceInput outputTensor;
            outputTensor.name = cachedSession->outputDescriptors[outputIndex].name;
            outputTensor.elementType = cachedSession->outputDescriptors[outputIndex].elementType;
            outputTensor.dimensions = outputDimensions;
            outputTensor.bytes.resize(outputByteCount);
            if (outputByteCount > 0) {
                std::memcpy(outputTensor.bytes.data(), outputPointer, outputByteCount);
            }
            outputTensors.push_back(std::move(outputTensor));
            nativeOnnxApi.releaseTensorTypeAndShapeInfo(outputShapeInfo);
        }
        for (OrtValue *outputValue : outputValues) {
            if (outputValue) {
                nativeOnnxApi.releaseValue(outputValue);
            }
        }
        for (OrtValue *inputValue : inputValues) {
            if (inputValue) {
                nativeOnnxApi.releaseValue(inputValue);
            }
        }
        return outputTensors;
    } catch (...) {
        for (OrtValue *outputValue : outputValues) {
            if (outputValue) {
                nativeOnnxApi.releaseValue(outputValue);
            }
        }
        for (OrtValue *inputValue : inputValues) {
            if (inputValue) {
                nativeOnnxApi.releaseValue(inputValue);
            }
        }
        throw;
    }
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxCachedModelBytes(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<uint8_t> &modelBytes, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const std::string &sessionCacheKey) {
    std::shared_ptr<NativeOnnxCachedSession> cachedSession = getNativeOnnxCachedSession(nativeOnnxApi, runtimeState, modelBytes, cpuThreadCount, shouldUseVvBinConfig, sessionCacheKey);
    return runNativeOnnxPreparedSession(nativeOnnxApi, cachedSession, inputTensors);
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxCachedModelPath(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const fs::path &modelPath, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const std::string &sessionCacheKey) {
    std::shared_ptr<NativeOnnxCachedSession> cachedSession = getNativeOnnxCachedSession(nativeOnnxApi, runtimeState, modelPath, cpuThreadCount, shouldUseVvBinConfig, sessionCacheKey);
    return runNativeOnnxPreparedSession(nativeOnnxApi, cachedSession, inputTensors);
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxModelBytes(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<uint8_t> &modelBytes, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const std::string &sessionCacheKey = "") {
    if (!sessionCacheKey.empty()) {
        return runNativeOnnxCachedModelBytes(nativeOnnxApi, runtimeState, modelBytes, inputTensors, cpuThreadCount, shouldUseVvBinConfig, sessionCacheKey);
    }
    OrtEnv *env = nullptr;
    OrtSessionOptions *sessionOptions = nullptr;
    OrtSession *session = nullptr;
    OrtAllocator *allocator = nullptr;
    OrtMemoryInfo *memoryInfo = nullptr;
    std::vector<OrtValue *> inputValues;
    std::vector<OrtValue *> outputValues;
    try {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createEnv(ortLoggingLevelWarning, "litevox-native-chain", &env), "OrtEnv 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getAllocatorWithDefaultOptions(&allocator), "default allocator 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionOptions(&sessionOptions), "SessionOptions 作成");
        configureNativeOnnxSessionOptions(nativeOnnxApi, runtimeState, sessionOptions, cpuThreadCount, shouldUseVvBinConfig);
        applyNativeOnnxSeedIfConfigured(nativeOnnxApi);
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionFromArray(env, modelBytes.data(), modelBytes.size(), sessionOptions, &session), "ONNX session 作成");
        size_t inputCount = 0;
        size_t outputCount = 0;
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputCount(session, &inputCount), "input count 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputCount(session, &outputCount), "output count 取得");
        std::vector<NativeOnnxValueDescriptor> inputDescriptors;
        std::vector<NativeOnnxValueDescriptor> outputDescriptors;
        inputDescriptors.reserve(inputCount);
        outputDescriptors.reserve(outputCount);
        for (size_t inputIndex = 0; inputIndex < inputCount; inputIndex++) {
            inputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, session, allocator, true, inputIndex));
        }
        for (size_t outputIndex = 0; outputIndex < outputCount; outputIndex++) {
            outputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, session, allocator, false, outputIndex));
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createCpuMemoryInfo(0, 0, &memoryInfo), "CPU memory info 作成");
        std::vector<const char *> inputNames;
        std::vector<const OrtValue *> inputPointers;
        inputNames.reserve(inputDescriptors.size());
        inputPointers.reserve(inputDescriptors.size());
        inputValues.reserve(inputDescriptors.size());
        for (const NativeOnnxValueDescriptor &inputDescriptor : inputDescriptors) {
            const NativeOnnxTraceInput &inputTensor = requireNativeOnnxTensor(inputTensors, inputDescriptor.name);
            if (inputTensor.elementType != inputDescriptor.elementType) {
                throw std::runtime_error("input dtype が一致しません: " + inputDescriptor.name);
            }
            if (!areNativeOnnxDimensionsEqual(inputDescriptor.dimensions, inputTensor.dimensions)) {
                throw std::runtime_error("input shape が一致しません: " + inputDescriptor.name);
            }
            OrtValue *inputValue = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createTensorWithDataAsOrtValue(memoryInfo, const_cast<uint8_t *>(inputTensor.bytes.data()), inputTensor.bytes.size(), inputTensor.dimensions.data(), inputTensor.dimensions.size(), inputDescriptor.elementType, &inputValue), "input tensor 作成");
            inputValues.push_back(inputValue);
            inputPointers.push_back(inputValue);
            inputNames.push_back(inputDescriptor.name.c_str());
        }
        std::vector<const char *> outputNames;
        outputNames.reserve(outputDescriptors.size());
        for (const NativeOnnxValueDescriptor &outputDescriptor : outputDescriptors) {
            outputNames.push_back(outputDescriptor.name.c_str());
        }
        outputValues.resize(outputDescriptors.size(), nullptr);
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.run(session, nullptr, inputNames.data(), inputPointers.data(), inputPointers.size(), outputNames.data(), outputNames.size(), outputValues.data()), "ONNX Run");
        std::vector<NativeOnnxTraceInput> outputTensors;
        outputTensors.reserve(outputDescriptors.size());
        for (size_t outputIndex = 0; outputIndex < outputValues.size(); outputIndex++) {
            OrtTensorTypeAndShapeInfo *outputShapeInfo = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorTypeAndShape(outputValues[outputIndex], &outputShapeInfo), "output tensor info 取得");
            size_t outputElementCount = 0;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorShapeElementCount(outputShapeInfo, &outputElementCount), "output element count 取得");
            std::vector<int64_t> outputDimensions = readNativeOnnxTensorDimensions(nativeOnnxApi, outputShapeInfo);
            void *outputPointer = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorMutableData(outputValues[outputIndex], &outputPointer), "output tensor data 取得");
            size_t outputByteCount = outputElementCount * getNativeOnnxElementByteCount(outputDescriptors[outputIndex].elementType);
            NativeOnnxTraceInput outputTensor;
            outputTensor.name = outputDescriptors[outputIndex].name;
            outputTensor.elementType = outputDescriptors[outputIndex].elementType;
            outputTensor.dimensions = outputDimensions;
            outputTensor.bytes.resize(outputByteCount);
            if (outputByteCount > 0) {
                std::memcpy(outputTensor.bytes.data(), outputPointer, outputByteCount);
            }
            outputTensors.push_back(std::move(outputTensor));
            nativeOnnxApi.releaseTensorTypeAndShapeInfo(outputShapeInfo);
        }
        for (OrtValue *outputValue : outputValues) {
            if (outputValue) {
                nativeOnnxApi.releaseValue(outputValue);
            }
        }
        for (OrtValue *inputValue : inputValues) {
            if (inputValue) {
                nativeOnnxApi.releaseValue(inputValue);
            }
        }
        nativeOnnxApi.releaseMemoryInfo(memoryInfo);
        nativeOnnxApi.releaseSession(session);
        nativeOnnxApi.releaseSessionOptions(sessionOptions);
        nativeOnnxApi.releaseEnv(env);
        return outputTensors;
    } catch (...) {
        for (OrtValue *outputValue : outputValues) {
            if (outputValue) {
                nativeOnnxApi.releaseValue(outputValue);
            }
        }
        for (OrtValue *inputValue : inputValues) {
            if (inputValue) {
                nativeOnnxApi.releaseValue(inputValue);
            }
        }
        if (memoryInfo) {
            nativeOnnxApi.releaseMemoryInfo(memoryInfo);
        }
        if (session) {
            nativeOnnxApi.releaseSession(session);
        }
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
        }
        if (env) {
            nativeOnnxApi.releaseEnv(env);
        }
        throw;
    }
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxModelPath(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const fs::path &modelPath, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const std::string &sessionCacheKey = "") {
    if (!sessionCacheKey.empty()) {
        return runNativeOnnxCachedModelPath(nativeOnnxApi, runtimeState, modelPath, inputTensors, cpuThreadCount, shouldUseVvBinConfig, sessionCacheKey);
    }
    std::shared_ptr<NativeOnnxCachedSession> cachedSession = createNativeOnnxCachedSession(nativeOnnxApi, runtimeState, modelPath, cpuThreadCount, shouldUseVvBinConfig);
    return runNativeOnnxPreparedSession(nativeOnnxApi, cachedSession, inputTensors);
}

static int64_t parseNativeOnnxPhonemeCode(const std::string &phonemeText) {
    if (phonemeText == "pau") return 0;
    if (phonemeText == "A") return 1;
    if (phonemeText == "E") return 2;
    if (phonemeText == "I") return 3;
    if (phonemeText == "N") return 4;
    if (phonemeText == "O") return 5;
    if (phonemeText == "U") return 6;
    if (phonemeText == "a") return 7;
    if (phonemeText == "b") return 8;
    if (phonemeText == "by") return 9;
    if (phonemeText == "ch") return 10;
    if (phonemeText == "cl") return 11;
    if (phonemeText == "d") return 12;
    if (phonemeText == "dy") return 13;
    if (phonemeText == "e") return 14;
    if (phonemeText == "f") return 15;
    if (phonemeText == "g") return 16;
    if (phonemeText == "gw") return 17;
    if (phonemeText == "gy") return 18;
    if (phonemeText == "h") return 19;
    if (phonemeText == "hy") return 20;
    if (phonemeText == "i") return 21;
    if (phonemeText == "j") return 22;
    if (phonemeText == "k") return 23;
    if (phonemeText == "kw") return 24;
    if (phonemeText == "ky") return 25;
    if (phonemeText == "m") return 26;
    if (phonemeText == "my") return 27;
    if (phonemeText == "n") return 28;
    if (phonemeText == "ny") return 29;
    if (phonemeText == "o") return 30;
    if (phonemeText == "p") return 31;
    if (phonemeText == "py") return 32;
    if (phonemeText == "r") return 33;
    if (phonemeText == "ry") return 34;
    if (phonemeText == "s") return 35;
    if (phonemeText == "sh") return 36;
    if (phonemeText == "t") return 37;
    if (phonemeText == "ts") return 38;
    if (phonemeText == "ty") return 39;
    if (phonemeText == "u") return 40;
    if (phonemeText == "v") return 41;
    if (phonemeText == "w") return 42;
    if (phonemeText == "y") return 43;
    if (phonemeText == "z") return 44;
    throw std::runtime_error("未対応の phoneme です: " + phonemeText);
}

static bool isNativeOnnxJsonNumberStart(char character) {
    return character == '-' || character == '+' || std::isdigit(static_cast<unsigned char>(character));
}

static size_t skipNativeOnnxJsonSpaces(const std::string &jsonText, size_t position) {
    while (position < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[position]))) {
        position++;
    }
    return position;
}

static bool isNativeOnnxJsonNullAt(const std::string &jsonText, size_t position) {
    return jsonText.compare(position, 4, "null") == 0;
}

static size_t findNativeOnnxJsonNumberEnd(const std::string &jsonText, size_t position) {
    size_t endPosition = position;
    while (endPosition < jsonText.size()) {
        char character = jsonText[endPosition];
        if (!(std::isdigit(static_cast<unsigned char>(character)) || character == '-' || character == '+' || character == '.' || character == 'e' || character == 'E')) {
            break;
        }
        endPosition++;
    }
    return endPosition;
}

static int64_t parseNativeOnnxJsonInt64At(const std::string &jsonText, size_t position, const std::string &fieldName) {
    position = skipNativeOnnxJsonSpaces(jsonText, position);
    if (position >= jsonText.size() || !isNativeOnnxJsonNumberStart(jsonText[position])) {
        throw std::runtime_error(fieldName + " が数値ではありません");
    }
    size_t endPosition = findNativeOnnxJsonNumberEnd(jsonText, position);
    return std::stoll(jsonText.substr(position, endPosition - position));
}

static uint64_t parseNativeOnnxJsonUint64At(const std::string &jsonText, size_t position, const std::string &fieldName) {
    position = skipNativeOnnxJsonSpaces(jsonText, position);
    if (position >= jsonText.size() || jsonText[position] == '-' || !isNativeOnnxJsonNumberStart(jsonText[position])) {
        throw std::runtime_error(fieldName + " が非負整数ではありません");
    }
    size_t endPosition = findNativeOnnxJsonNumberEnd(jsonText, position);
    return std::stoull(jsonText.substr(position, endPosition - position));
}

static float parseNativeOnnxJsonFloatAt(const std::string &jsonText, size_t position, const std::string &fieldName) {
    position = skipNativeOnnxJsonSpaces(jsonText, position);
    if (position >= jsonText.size() || !isNativeOnnxJsonNumberStart(jsonText[position])) {
        throw std::runtime_error(fieldName + " が数値ではありません");
    }
    size_t endPosition = findNativeOnnxJsonNumberEnd(jsonText, position);
    float parsedNumber = std::stof(jsonText.substr(position, endPosition - position));
    if (!std::isfinite(parsedNumber)) {
        throw std::runtime_error(fieldName + " が有限値ではありません");
    }
    return parsedNumber;
}

static uint64_t requireNativeOnnxJsonUint64Field(const std::string &jsonText, const std::string &fieldName) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos) {
        throw std::runtime_error(fieldName + " がありません");
    }
    return parseNativeOnnxJsonUint64At(jsonText, valuePosition, fieldName);
}

static bool extractNativeOnnxJsonOptionalInt64Field(const std::string &jsonText, const std::string &fieldName, int64_t &numberValue) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos) {
        return false;
    }
    valuePosition = skipNativeOnnxJsonSpaces(jsonText, valuePosition);
    if (isNativeOnnxJsonNullAt(jsonText, valuePosition)) {
        return false;
    }
    numberValue = parseNativeOnnxJsonInt64At(jsonText, valuePosition, fieldName);
    return true;
}

static std::string requireNativeOnnxJsonStringField(const std::string &jsonText, const std::string &fieldName) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos || valuePosition >= jsonText.size() || jsonText[valuePosition] != '"') {
        throw std::runtime_error(fieldName + " がありません");
    }
    return decodeJsonString(jsonText, valuePosition);
}

static bool extractNativeOnnxJsonOptionalStringField(const std::string &jsonText, const std::string &fieldName, std::string &stringValue) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos) {
        return false;
    }
    valuePosition = skipNativeOnnxJsonSpaces(jsonText, valuePosition);
    if (isNativeOnnxJsonNullAt(jsonText, valuePosition)) {
        return false;
    }
    if (valuePosition >= jsonText.size() || jsonText[valuePosition] != '"') {
        throw std::runtime_error(fieldName + " が文字列ではありません");
    }
    stringValue = decodeJsonString(jsonText, valuePosition);
    return true;
}

static std::vector<float> parseNativeOnnxFloatArrayText(const std::string &arrayText, const std::string &fieldName) {
    size_t position = skipNativeOnnxJsonSpaces(arrayText, 0);
    if (position >= arrayText.size() || arrayText[position] != '[') {
        throw std::runtime_error(fieldName + " が配列ではありません");
    }
    position++;
    std::vector<float> numberValues;
    bool isFirstValue = true;
    while (true) {
        position = skipNativeOnnxJsonSpaces(arrayText, position);
        if (position >= arrayText.size()) {
            throw std::runtime_error(fieldName + " 配列が閉じていません");
        }
        if (arrayText[position] == ']') {
            return numberValues;
        }
        if (!isFirstValue) {
            if (arrayText[position] != ',') {
                throw std::runtime_error(fieldName + " 配列の区切りが不正です");
            }
            position = skipNativeOnnxJsonSpaces(arrayText, position + 1);
        }
        numberValues.push_back(parseNativeOnnxJsonFloatAt(arrayText, position, fieldName));
        position = findNativeOnnxJsonNumberEnd(arrayText, position);
        isFirstValue = false;
    }
}

static std::vector<NativeOnnxScoreNote> parseNativeOnnxScore(const std::string &scoreText) {
    std::string notesJson = extractJsonArrayField(scoreText, "notes");
    if (notesJson.empty()) {
        throw std::runtime_error("notes がありません");
    }
    std::vector<NativeOnnxScoreNote> scoreNotes;
    for (const std::string &noteObject : splitJsonObjects(notesJson)) {
        NativeOnnxScoreNote scoreNote;
        scoreNote.hasNoteId = extractNativeOnnxJsonOptionalStringField(noteObject, "id", scoreNote.noteId);
        scoreNote.hasKey = extractNativeOnnxJsonOptionalInt64Field(noteObject, "key", scoreNote.key);
        if (scoreNote.hasKey && (scoreNote.key < 0 || scoreNote.key > 127)) {
            throw std::runtime_error("key が範囲外です");
        }
        scoreNote.frameLength = requireNativeOnnxJsonUint64Field(noteObject, "frame_length");
        scoreNote.lyric = requireNativeOnnxJsonStringField(noteObject, "lyric");
        if (!scoreNote.hasKey && !scoreNote.lyric.empty()) {
            throw std::runtime_error("休符の lyric は空文字が必要です");
        }
        if (scoreNote.hasKey && scoreNote.lyric.empty()) {
            throw std::runtime_error("音符の lyric がありません");
        }
        if (scoreNote.hasKey) {
            scoreNote.mora = createNativeAudioQueryMoraFromText(scoreNote.lyric);
            scoreNote.hasMora = true;
        }
        scoreNotes.push_back(std::move(scoreNote));
    }
    if (scoreNotes.empty()) {
        throw std::runtime_error("notes が空です");
    }
    if (scoreNotes.front().hasKey) {
        throw std::runtime_error("score の先頭は休符が必要です");
    }
    return scoreNotes;
}

static std::vector<NativeOnnxFramePhoneme> parseNativeOnnxFramePhonemes(const std::string &phonemesJson) {
    std::vector<NativeOnnxFramePhoneme> framePhonemes;
    for (const std::string &phonemeObject : splitJsonObjects(phonemesJson)) {
        NativeOnnxFramePhoneme framePhoneme;
        framePhoneme.phoneme = requireNativeOnnxJsonStringField(phonemeObject, "phoneme");
        framePhoneme.frameLength = requireNativeOnnxJsonUint64Field(phonemeObject, "frame_length");
        framePhoneme.hasNoteId = extractNativeOnnxJsonOptionalStringField(phonemeObject, "note_id", framePhoneme.noteId);
        framePhonemes.push_back(std::move(framePhoneme));
    }
    return framePhonemes;
}

static NativeOnnxFrameAudioQuery parseNativeOnnxFrameAudioQuery(const std::string &frameAudioQueryText) {
    NativeOnnxFrameAudioQuery frameAudioQuery;
    std::string f0Json = extractJsonArrayField(frameAudioQueryText, "f0");
    std::string volumeJson = extractJsonArrayField(frameAudioQueryText, "volume");
    std::string phonemesJson = extractJsonArrayField(frameAudioQueryText, "phonemes");
    if (f0Json.empty()) {
        throw std::runtime_error("f0 がありません");
    }
    if (volumeJson.empty()) {
        throw std::runtime_error("volume がありません");
    }
    if (phonemesJson.empty()) {
        throw std::runtime_error("phonemes がありません");
    }
    frameAudioQuery.f0Values = parseNativeOnnxFloatArrayText(f0Json, "f0");
    frameAudioQuery.volumeValues = parseNativeOnnxFloatArrayText(volumeJson, "volume");
    frameAudioQuery.phonemes = parseNativeOnnxFramePhonemes(phonemesJson);
    frameAudioQuery.volumeScale = static_cast<float>(extractNativeOnnxJsonNumberField(frameAudioQueryText, "volumeScale", frameAudioQuery.volumeScale));
    frameAudioQuery.outputSamplingRate = static_cast<uint32_t>(extractNativeOnnxJsonNumberField(frameAudioQueryText, "outputSamplingRate", frameAudioQuery.outputSamplingRate));
    frameAudioQuery.outputStereo = extractNativeOnnxJsonBoolField(frameAudioQueryText, "outputStereo", frameAudioQuery.outputStereo);
    if (!std::isfinite(frameAudioQuery.volumeScale) || frameAudioQuery.volumeScale < 0.0f) {
        throw std::runtime_error("volumeScale が不正です");
    }
    if (frameAudioQuery.outputSamplingRate == 0) {
        throw std::runtime_error("outputSamplingRate が不正です");
    }
    return frameAudioQuery;
}

static size_t calculateNativeOnnxFrameAudioQueryFrameCount(const NativeOnnxFrameAudioQuery &frameAudioQuery) {
    uint64_t frameCount = 0;
    for (const NativeOnnxFramePhoneme &framePhoneme : frameAudioQuery.phonemes) {
        frameCount += framePhoneme.frameLength;
        if (frameCount > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            throw std::runtime_error("frame_length の合計が大きすぎます");
        }
    }
    return static_cast<size_t>(frameCount);
}

static void validateNativeOnnxParsedFrameAudioQuery(const NativeOnnxFrameAudioQuery &frameAudioQuery) {
    size_t frameCount = calculateNativeOnnxFrameAudioQueryFrameCount(frameAudioQuery);
    if (frameAudioQuery.f0Values.size() != frameCount) {
        throw std::runtime_error("f0 の長さが frame_length 合計と一致しません");
    }
    if (frameAudioQuery.volumeValues.size() != frameCount) {
        throw std::runtime_error("volume の長さが frame_length 合計と一致しません");
    }
    for (const NativeOnnxFramePhoneme &framePhoneme : frameAudioQuery.phonemes) {
        parseNativeOnnxPhonemeCode(framePhoneme.phoneme);
    }
}

static std::vector<NativeOnnxSongPhonemeFeature> createNativeOnnxSongPhonemeFeatures(const std::vector<NativeOnnxScoreNote> &scoreNotes) {
    std::vector<NativeOnnxSongPhonemeFeature> phonemeFeatures;
    for (const NativeOnnxScoreNote &scoreNote : scoreNotes) {
        if (!scoreNote.hasKey) {
            NativeOnnxSongPhonemeFeature phonemeFeature;
            phonemeFeature.phoneme = "pau";
            phonemeFeature.phonemeCode = 0;
            phonemeFeature.key = -1;
            phonemeFeature.noteId = scoreNote.noteId;
            phonemeFeature.hasNoteId = scoreNote.hasNoteId;
            phonemeFeatures.push_back(std::move(phonemeFeature));
            continue;
        }
        if (scoreNote.mora.hasConsonant) {
            NativeOnnxSongPhonemeFeature consonantFeature;
            consonantFeature.phoneme = scoreNote.mora.consonant;
            consonantFeature.phonemeCode = parseNativeOnnxPhonemeCode(scoreNote.mora.consonant);
            consonantFeature.key = scoreNote.key;
            consonantFeature.noteId = scoreNote.noteId;
            consonantFeature.hasNoteId = scoreNote.hasNoteId;
            phonemeFeatures.push_back(std::move(consonantFeature));
        }
        NativeOnnxSongPhonemeFeature vowelFeature;
        vowelFeature.phoneme = scoreNote.mora.vowel;
        vowelFeature.phonemeCode = parseNativeOnnxPhonemeCode(scoreNote.mora.vowel);
        vowelFeature.key = scoreNote.key;
        vowelFeature.noteId = scoreNote.noteId;
        vowelFeature.hasNoteId = scoreNote.hasNoteId;
        phonemeFeatures.push_back(std::move(vowelFeature));
    }
    return phonemeFeatures;
}

static std::vector<uint64_t> createNativeOnnxSongPhonemeLengths(const std::vector<NativeOnnxScoreNote> &scoreNotes, const std::vector<int64_t> &consonantLengths) {
    if (scoreNotes.empty() || scoreNotes.size() != consonantLengths.size()) {
        throw std::runtime_error("consonant_lengths の長さが一致しません");
    }
    if (consonantLengths.front() != 0) {
        throw std::runtime_error("consonant_lengths の先頭が 0 ではありません");
    }
    std::vector<uint64_t> phonemeLengths;
    for (size_t noteIndex = 0; noteIndex + 1 < scoreNotes.size(); noteIndex++) {
        uint64_t noteDuration = scoreNotes[noteIndex].frameLength;
        int64_t nextConsonantLength = consonantLengths[noteIndex + 1];
        bool hasNextConsonant = nextConsonantLength != 0;
        uint64_t adjustedConsonantLength = 0;
        if (hasNextConsonant) {
            if (nextConsonantLength < 0 || static_cast<uint64_t>(nextConsonantLength) > noteDuration) {
                adjustedConsonantLength = noteDuration / 2;
            } else {
                adjustedConsonantLength = static_cast<uint64_t>(nextConsonantLength);
            }
        }
        phonemeLengths.push_back(noteDuration - adjustedConsonantLength);
        if (hasNextConsonant) {
            phonemeLengths.push_back(adjustedConsonantLength);
        }
    }
    phonemeLengths.push_back(scoreNotes.back().frameLength);
    return phonemeLengths;
}

static NativeOnnxSongFrameInputs createNativeOnnxSongFrameInputs(const std::vector<NativeOnnxSongPhonemeFeature> &phonemeFeatures, const std::vector<NativeOnnxFramePhoneme> &framePhonemes) {
    if (phonemeFeatures.size() != framePhonemes.size()) {
        throw std::runtime_error("score と frame_audio_query の phoneme 数が一致しません");
    }
    NativeOnnxSongFrameInputs frameInputs;
    for (size_t phonemeIndex = 0; phonemeIndex < phonemeFeatures.size(); phonemeIndex++) {
        const NativeOnnxSongPhonemeFeature &phonemeFeature = phonemeFeatures[phonemeIndex];
        const NativeOnnxFramePhoneme &framePhoneme = framePhonemes[phonemeIndex];
        if (parseNativeOnnxPhonemeCode(framePhoneme.phoneme) != phonemeFeature.phonemeCode) {
            throw std::runtime_error("score と frame_audio_query の phoneme が一致しません");
        }
        if (framePhoneme.frameLength > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            throw std::runtime_error("frame_length が大きすぎます");
        }
        for (uint64_t frameIndex = 0; frameIndex < framePhoneme.frameLength; frameIndex++) {
            frameInputs.phonemeValues.push_back(phonemeFeature.phonemeCode);
            frameInputs.keyValues.push_back(phonemeFeature.key);
        }
    }
    return frameInputs;
}

static NativeOnnxAudioQuerySettings createNativeOnnxSettingsFromFrameAudioQuery(const NativeOnnxFrameAudioQuery &frameAudioQuery) {
    NativeOnnxAudioQuerySettings audioQuerySettings;
    audioQuerySettings.volumeScale = frameAudioQuery.volumeScale;
    audioQuerySettings.outputSamplingRate = frameAudioQuery.outputSamplingRate;
    audioQuerySettings.outputStereo = frameAudioQuery.outputStereo;
    return audioQuerySettings;
}

static std::string createNativeOnnxFloatArrayJson(const std::vector<float> &numberValues) {
    std::ostringstream jsonStream;
    jsonStream << "[";
    jsonStream << std::setprecision(9);
    for (size_t numberIndex = 0; numberIndex < numberValues.size(); numberIndex++) {
        if (numberIndex > 0) {
            jsonStream << ",";
        }
        if (!std::isfinite(numberValues[numberIndex])) {
            throw std::runtime_error("float 配列に有限でない値があります");
        }
        jsonStream << numberValues[numberIndex];
    }
    jsonStream << "]";
    return jsonStream.str();
}

static std::string createNativeOnnxFrameAudioQueryJson(const NativeOnnxFrameAudioQuery &frameAudioQuery) {
    std::ostringstream jsonStream;
    jsonStream << std::setprecision(9);
    jsonStream << "{\"f0\":" << createNativeOnnxFloatArrayJson(frameAudioQuery.f0Values) << ",";
    jsonStream << "\"volume\":" << createNativeOnnxFloatArrayJson(frameAudioQuery.volumeValues) << ",";
    jsonStream << "\"phonemes\":[";
    for (size_t phonemeIndex = 0; phonemeIndex < frameAudioQuery.phonemes.size(); phonemeIndex++) {
        const NativeOnnxFramePhoneme &framePhoneme = frameAudioQuery.phonemes[phonemeIndex];
        if (phonemeIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << "{\"phoneme\":" << quoteJsonString(framePhoneme.phoneme) << ",";
        jsonStream << "\"frame_length\":" << framePhoneme.frameLength << ",";
        jsonStream << "\"note_id\":";
        if (framePhoneme.hasNoteId) {
            jsonStream << quoteJsonString(framePhoneme.noteId);
        } else {
            jsonStream << "null";
        }
        jsonStream << "}";
    }
    jsonStream << "],";
    jsonStream << "\"volumeScale\":" << frameAudioQuery.volumeScale << ",";
    jsonStream << "\"outputSamplingRate\":" << frameAudioQuery.outputSamplingRate << ",";
    jsonStream << "\"outputStereo\":" << (frameAudioQuery.outputStereo ? "true" : "false") << "}";
    return jsonStream.str();
}

static int64_t resolveNativeOnnxInnerVoiceId(const ModelAssetRecord &modelAsset, const std::string &domainName, uint32_t styleId) {
    std::vector<uint8_t> manifestBytes = extractVvmEntryBytes(modelAsset.archivePath, "manifest.json");
    std::string manifestText(manifestBytes.begin(), manifestBytes.end());
    std::string domainObject = extractJsonObjectField(manifestText, domainName);
    if (domainObject.empty()) {
        throw std::runtime_error(domainName + " が manifest にありません");
    }
    std::string mappingObject = extractJsonObjectField(domainObject, "style_id_to_inner_voice_id");
    if (mappingObject.empty()) {
        return static_cast<int64_t>(styleId);
    }
    size_t valuePosition = findJsonFieldValuePosition(mappingObject, std::to_string(styleId));
    if (valuePosition == std::string::npos) {
        throw std::runtime_error(domainName + " の style ID が未対応です: " + std::to_string(styleId));
    }
    return parseNativeOnnxJsonInt64At(mappingObject, valuePosition, "style_id_to_inner_voice_id");
}

static bool isNativeOnnxMoraTailPhoneme(int64_t phonemeValue) {
    return phonemeValue == 0 || phonemeValue == 1 || phonemeValue == 2 || phonemeValue == 3 || phonemeValue == 4 || phonemeValue == 5 || phonemeValue == 6 || phonemeValue == 7 || phonemeValue == 11 || phonemeValue == 14 || phonemeValue == 21 || phonemeValue == 30 || phonemeValue == 40;
}

static NativeOnnxMora parseNativeOnnxMora(const std::string &moraJson) {
    NativeOnnxMora mora;
    mora.text = extractJsonStringField(moraJson, "text");
    mora.consonant = extractJsonStringField(moraJson, "consonant");
    mora.hasConsonant = !mora.consonant.empty();
    mora.hasConsonantLength = extractNativeOnnxJsonFloatField(moraJson, "consonant_length", mora.consonantLength);
    mora.vowel = extractJsonStringField(moraJson, "vowel");
    if (mora.vowel.empty()) {
        throw std::runtime_error("mora vowel がありません");
    }
    mora.hasVowelLength = extractNativeOnnxJsonFloatField(moraJson, "vowel_length", mora.vowelLength);
    mora.hasPitch = extractNativeOnnxJsonFloatField(moraJson, "pitch", mora.pitch);
    return mora;
}

static std::vector<NativeOnnxAccentPhrase> parseNativeOnnxAccentPhrases(const std::string &audioQueryText) {
    std::string accentPhrasesJson = extractJsonArrayField(audioQueryText, "accent_phrases");
    if (accentPhrasesJson.empty()) {
        throw std::runtime_error("accent_phrases がありません");
    }
    std::vector<NativeOnnxAccentPhrase> accentPhrases;
    for (const std::string &phraseJson : splitJsonObjects(accentPhrasesJson)) {
        NativeOnnxAccentPhrase accentPhrase;
        uint32_t accentValue = 1;
        extractJsonNumberField(phraseJson, "accent", accentValue);
        accentPhrase.accent = static_cast<int64_t>(accentValue);
        std::string morasJson = extractJsonArrayField(phraseJson, "moras");
        for (const std::string &moraJson : splitJsonObjects(morasJson)) {
            accentPhrase.moras.push_back(parseNativeOnnxMora(moraJson));
        }
        std::string pauseMoraJson = extractJsonObjectField(phraseJson, "pause_mora");
        if (!pauseMoraJson.empty()) {
            accentPhrase.pauseMora = parseNativeOnnxMora(pauseMoraJson);
            accentPhrase.hasPauseMora = true;
        }
        accentPhrase.isInterrogative = extractNativeOnnxJsonBoolField(phraseJson, "is_interrogative", false);
        accentPhrases.push_back(std::move(accentPhrase));
    }
    return accentPhrases;
}

static NativeOnnxTraceInput createNativeOnnxInt64Tensor(const std::string &tensorName, const std::vector<int64_t> &dimensions, const std::vector<int64_t> &values) {
    NativeOnnxTraceInput tensor;
    tensor.name = tensorName;
    tensor.elementType = 7;
    tensor.dimensions = dimensions;
    tensor.bytes = createNativeOnnxTensorBytes(values);
    return tensor;
}

static void appendNativeOnnxAccentValues(std::vector<int64_t> &accentValues, const NativeOnnxAccentPhrase &accentPhrase, int64_t point) {
    for (size_t moraIndex = 0; moraIndex < accentPhrase.moras.size(); moraIndex++) {
        bool isPoint = static_cast<int64_t>(moraIndex) == point || (point < 0 && static_cast<int64_t>(moraIndex) == static_cast<int64_t>(accentPhrase.moras.size()) + point);
        int64_t value = isPoint ? 1 : 0;
        accentValues.push_back(value);
        if (accentPhrase.moras[moraIndex].hasConsonant) {
            accentValues.push_back(value);
        }
    }
    if (accentPhrase.hasPauseMora) {
        accentValues.push_back(0);
    }
}

static void splitNativeOnnxMoraPhonemes(const std::vector<int64_t> &phonemeValues, std::vector<int64_t> &consonantValues, std::vector<int64_t> &vowelValues, std::vector<int64_t> &vowelIndexes) {
    for (size_t phonemeIndex = 0; phonemeIndex < phonemeValues.size(); phonemeIndex++) {
        if (isNativeOnnxMoraTailPhoneme(phonemeValues[phonemeIndex])) {
            vowelValues.push_back(phonemeValues[phonemeIndex]);
            vowelIndexes.push_back(static_cast<int64_t>(phonemeIndex));
        }
    }
    if (vowelIndexes.empty()) {
        throw std::runtime_error("vowel phoneme がありません");
    }
    consonantValues.push_back(-1);
    for (size_t vowelIndex = 0; vowelIndex + 1 < vowelIndexes.size(); vowelIndex++) {
        int64_t previousIndex = vowelIndexes[vowelIndex];
        int64_t nextIndex = vowelIndexes[vowelIndex + 1];
        if (nextIndex - previousIndex == 1) {
            consonantValues.push_back(-1);
        } else {
            consonantValues.push_back(phonemeValues[static_cast<size_t>(nextIndex - 1)]);
        }
    }
}

static std::vector<NativeOnnxTraceInput> createNativeOnnxFrontendInputs(const std::string &audioQueryText, int64_t innerVoiceId) {
    std::vector<NativeOnnxAccentPhrase> accentPhrases = parseNativeOnnxAccentPhrases(audioQueryText);
    std::vector<int64_t> phonemeValues;
    phonemeValues.push_back(0);
    for (const NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        for (const NativeOnnxMora &mora : accentPhrase.moras) {
            if (mora.hasConsonant) {
                phonemeValues.push_back(parseNativeOnnxPhonemeCode(mora.consonant));
            }
            phonemeValues.push_back(parseNativeOnnxPhonemeCode(mora.vowel));
        }
        if (accentPhrase.hasPauseMora) {
            if (accentPhrase.pauseMora.hasConsonant) {
                phonemeValues.push_back(parseNativeOnnxPhonemeCode(accentPhrase.pauseMora.consonant));
            }
            phonemeValues.push_back(parseNativeOnnxPhonemeCode(accentPhrase.pauseMora.vowel));
        }
    }
    phonemeValues.push_back(0);
    std::vector<int64_t> consonantValues;
    std::vector<int64_t> vowelValues;
    std::vector<int64_t> vowelIndexes;
    splitNativeOnnxMoraPhonemes(phonemeValues, consonantValues, vowelValues, vowelIndexes);
    std::vector<int64_t> baseStartAccentValues{0};
    std::vector<int64_t> baseEndAccentValues{0};
    std::vector<int64_t> baseStartAccentPhraseValues{0};
    std::vector<int64_t> baseEndAccentPhraseValues{0};
    for (const NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        appendNativeOnnxAccentValues(baseStartAccentValues, accentPhrase, accentPhrase.accent != 1 ? 1 : 0);
        appendNativeOnnxAccentValues(baseEndAccentValues, accentPhrase, accentPhrase.accent - 1);
        appendNativeOnnxAccentValues(baseStartAccentPhraseValues, accentPhrase, 0);
        appendNativeOnnxAccentValues(baseEndAccentPhraseValues, accentPhrase, -1);
    }
    baseStartAccentValues.push_back(0);
    baseEndAccentValues.push_back(0);
    baseStartAccentPhraseValues.push_back(0);
    baseEndAccentPhraseValues.push_back(0);
    std::vector<int64_t> startAccentValues;
    std::vector<int64_t> endAccentValues;
    std::vector<int64_t> startAccentPhraseValues;
    std::vector<int64_t> endAccentPhraseValues;
    for (int64_t vowelIndex : vowelIndexes) {
        size_t valueIndex = static_cast<size_t>(vowelIndex);
        startAccentValues.push_back(baseStartAccentValues.at(valueIndex));
        endAccentValues.push_back(baseEndAccentValues.at(valueIndex));
        startAccentPhraseValues.push_back(baseStartAccentPhraseValues.at(valueIndex));
        endAccentPhraseValues.push_back(baseEndAccentPhraseValues.at(valueIndex));
    }
    std::vector<NativeOnnxTraceInput> frontendInputs;
    frontendInputs.push_back(createNativeOnnxInt64Tensor("phoneme_list", {static_cast<int64_t>(phonemeValues.size())}, phonemeValues));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("speaker_id", {1}, {innerVoiceId}));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("length", {}, {static_cast<int64_t>(vowelValues.size())}));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("vowel_phoneme_list", {static_cast<int64_t>(vowelValues.size())}, vowelValues));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("consonant_phoneme_list", {static_cast<int64_t>(consonantValues.size())}, consonantValues));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("start_accent_list", {static_cast<int64_t>(startAccentValues.size())}, startAccentValues));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("end_accent_list", {static_cast<int64_t>(endAccentValues.size())}, endAccentValues));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("start_accent_phrase_list", {static_cast<int64_t>(startAccentPhraseValues.size())}, startAccentPhraseValues));
    frontendInputs.push_back(createNativeOnnxInt64Tensor("end_accent_phrase_list", {static_cast<int64_t>(endAccentPhraseValues.size())}, endAccentPhraseValues));
    return frontendInputs;
}

static std::vector<int64_t> findNativeOnnxVowelIndexes(const std::vector<int64_t> &phonemeValues, const std::vector<int64_t> &vowelValues) {
    std::vector<int64_t> vowelIndexes;
    vowelIndexes.reserve(vowelValues.size());
    size_t searchOffset = 0;
    for (int64_t vowelValue : vowelValues) {
        bool hasFound = false;
        for (size_t phonemeIndex = searchOffset; phonemeIndex < phonemeValues.size(); phonemeIndex++) {
            if (phonemeValues[phonemeIndex] == vowelValue) {
                vowelIndexes.push_back(static_cast<int64_t>(phonemeIndex));
                searchOffset = phonemeIndex + 1;
                hasFound = true;
                break;
            }
        }
        if (!hasFound) {
            throw std::runtime_error("vowel index を復元できません");
        }
    }
    return vowelIndexes;
}

static size_t calculateNativeOnnxFrameCount(float phonemeLength, float speedScale) {
    static constexpr float samplingRate = 24000.0f;
    static constexpr float frameHop = 256.0f;
    float adjustedLength = phonemeLength < 0.01f ? 0.01f : phonemeLength;
    double roundedSamples = std::nearbyint(static_cast<double>(adjustedLength * samplingRate / frameHop));
    double roundedFrames = std::nearbyint(roundedSamples / static_cast<double>(speedScale));
    if (roundedFrames <= 0.0) {
        return 0;
    }
    return static_cast<size_t>(roundedFrames);
}

static bool isNativeOnnxUnvoicedVowel(int64_t phonemeValue) {
    return phonemeValue == 0 || phonemeValue == 1 || phonemeValue == 2 || phonemeValue == 3 || phonemeValue == 5 || phonemeValue == 6 || phonemeValue == 11;
}

static void applyNativeOnnxPitchSettings(std::vector<float> &f0Values, const NativeOnnxAudioQuerySettings &audioQuerySettings) {
    float f0Sum = 0.0f;
    size_t voicedCount = 0;
    for (float &f0Value : f0Values) {
        f0Value *= std::pow(2.0f, audioQuerySettings.pitchScale);
        if (f0Value > 0.0f) {
            f0Sum += f0Value;
            voicedCount++;
        }
    }
    if (voicedCount == 0) {
        return;
    }
    float meanF0 = f0Sum / static_cast<float>(voicedCount);
    for (float &f0Value : f0Values) {
        if (f0Value > 0.0f) {
            f0Value = (f0Value - meanF0) * audioQuerySettings.intonationScale + meanF0;
        }
    }
}

static std::vector<NativeOnnxTraceInput> createNativeOnnxDecoderInputs(const std::vector<NativeOnnxTraceInput> &traceInputs, const std::vector<NativeOnnxTraceInput> &durationOutputs, const std::vector<NativeOnnxTraceInput> &intonationOutputs, const NativeOnnxAudioQuerySettings &audioQuerySettings, bool shouldZeroUnvoicedVowels) {
    const NativeOnnxTraceInput &phonemeListTensor = requireNativeOnnxTensor(traceInputs, "phoneme_list");
    const NativeOnnxTraceInput &vowelPhonemeListTensor = requireNativeOnnxTensor(traceInputs, "vowel_phoneme_list");
    const NativeOnnxTraceInput &speakerIdTensor = requireNativeOnnxTensor(traceInputs, "speaker_id");
    const NativeOnnxTraceInput &phonemeLengthTensor = requireNativeOnnxTensor(durationOutputs, "phoneme_length");
    const NativeOnnxTraceInput &f0ListTensor = requireNativeOnnxTensor(intonationOutputs, "f0_list");
    std::vector<int64_t> phonemeValues = readNativeOnnxTensorValues<int64_t>(phonemeListTensor, 7);
    std::vector<int64_t> vowelValues = readNativeOnnxTensorValues<int64_t>(vowelPhonemeListTensor, 7);
    std::vector<float> phonemeLengthValues = readNativeOnnxTensorValues<float>(phonemeLengthTensor, 1);
    std::vector<float> f0ListValues = readNativeOnnxTensorValues<float>(f0ListTensor, 1);
    if (phonemeValues.size() != phonemeLengthValues.size()) {
        throw std::runtime_error("phoneme_length の長さが一致しません");
    }
    if (!phonemeLengthValues.empty()) {
        phonemeLengthValues.front() = audioQuerySettings.prePhonemeLength;
        phonemeLengthValues.back() = audioQuerySettings.postPhonemeLength;
    }
    std::vector<int64_t> vowelIndexes = findNativeOnnxVowelIndexes(phonemeValues, vowelValues);
    if (vowelIndexes.size() != f0ListValues.size()) {
        throw std::runtime_error("f0_list の長さが一致しません");
    }
    std::vector<float> decoderF0Values = f0ListValues;
    if (!decoderF0Values.empty()) {
        decoderF0Values.front() = 0.0f;
        decoderF0Values.back() = 0.0f;
    }
    for (size_t vowelIndex = 0; vowelIndex < vowelValues.size(); vowelIndex++) {
        if (shouldZeroUnvoicedVowels && isNativeOnnxUnvoicedVowel(vowelValues[vowelIndex])) {
            decoderF0Values[vowelIndex] = 0.0f;
        }
    }
    applyNativeOnnxPitchSettings(decoderF0Values, audioQuerySettings);
    std::vector<float> f0FrameValues;
    std::vector<float> phonemeFrameValues;
    size_t pendingF0FrameCount = 0;
    size_t f0Index = 0;
    size_t vowelIndexCursor = 0;
    for (size_t phonemeIndex = 0; phonemeIndex < phonemeValues.size(); phonemeIndex++) {
        int64_t phonemeValue = phonemeValues[phonemeIndex];
        if (phonemeValue < 0 || phonemeValue >= nativeOnnxPhonemeSize) {
            throw std::runtime_error("phoneme id が範囲外です");
        }
        size_t frameCount = calculateNativeOnnxFrameCount(phonemeLengthValues[phonemeIndex], audioQuerySettings.speedScale);
        for (size_t frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            size_t frameOffset = phonemeFrameValues.size();
            phonemeFrameValues.resize(frameOffset + static_cast<size_t>(nativeOnnxPhonemeSize), 0.0f);
            phonemeFrameValues[frameOffset + static_cast<size_t>(phonemeValue)] = 1.0f;
        }
        pendingF0FrameCount += frameCount;
        if (vowelIndexCursor < vowelIndexes.size() && static_cast<int64_t>(phonemeIndex) == vowelIndexes[vowelIndexCursor]) {
            for (size_t frameIndex = 0; frameIndex < pendingF0FrameCount; frameIndex++) {
                f0FrameValues.push_back(decoderF0Values[f0Index]);
            }
            pendingF0FrameCount = 0;
            f0Index++;
            vowelIndexCursor++;
        }
    }
    if (pendingF0FrameCount != 0 || f0FrameValues.size() * static_cast<size_t>(nativeOnnxPhonemeSize) != phonemeFrameValues.size()) {
        throw std::runtime_error("decoder feature の frame 数が一致しません");
    }
    std::vector<float> paddedF0FrameValues;
    std::vector<float> paddedPhonemeFrameValues;
    paddedF0FrameValues.reserve(f0FrameValues.size() + nativeOnnxDecoderPaddingFrames * 2);
    paddedPhonemeFrameValues.reserve(phonemeFrameValues.size() + nativeOnnxDecoderPaddingFrames * 2 * static_cast<size_t>(nativeOnnxPhonemeSize));
    for (size_t frameIndex = 0; frameIndex < nativeOnnxDecoderPaddingFrames; frameIndex++) {
        paddedF0FrameValues.push_back(0.0f);
        size_t frameOffset = paddedPhonemeFrameValues.size();
        paddedPhonemeFrameValues.resize(frameOffset + static_cast<size_t>(nativeOnnxPhonemeSize), 0.0f);
        paddedPhonemeFrameValues[frameOffset] = 1.0f;
    }
    paddedF0FrameValues.insert(paddedF0FrameValues.end(), f0FrameValues.begin(), f0FrameValues.end());
    paddedPhonemeFrameValues.insert(paddedPhonemeFrameValues.end(), phonemeFrameValues.begin(), phonemeFrameValues.end());
    for (size_t frameIndex = 0; frameIndex < nativeOnnxDecoderPaddingFrames; frameIndex++) {
        paddedF0FrameValues.push_back(0.0f);
        size_t frameOffset = paddedPhonemeFrameValues.size();
        paddedPhonemeFrameValues.resize(frameOffset + static_cast<size_t>(nativeOnnxPhonemeSize), 0.0f);
        paddedPhonemeFrameValues[frameOffset] = 1.0f;
    }
    NativeOnnxTraceInput f0Tensor;
    f0Tensor.name = "f0";
    f0Tensor.elementType = 1;
    f0Tensor.dimensions = {static_cast<int64_t>(paddedF0FrameValues.size()), 1};
    f0Tensor.bytes = createNativeOnnxTensorBytes(paddedF0FrameValues);
    NativeOnnxTraceInput phonemeTensor;
    phonemeTensor.name = "phoneme";
    phonemeTensor.elementType = 1;
    phonemeTensor.dimensions = {static_cast<int64_t>(paddedF0FrameValues.size()), nativeOnnxPhonemeSize};
    phonemeTensor.bytes = createNativeOnnxTensorBytes(paddedPhonemeFrameValues);
    return {f0Tensor, phonemeTensor, speakerIdTensor};
}

static void appendNativeOnnxSmokeRunInfo(std::ostringstream &inspectStream, NativeOnnxApi &nativeOnnxApi, OrtSession *session, const std::vector<NativeOnnxValueDescriptor> &inputDescriptors, const std::vector<NativeOnnxValueDescriptor> &outputDescriptors, const std::vector<NativeOnnxTraceInput> &traceInputs, const std::vector<NativeOnnxTraceInput> &traceOutputs, const ModelAssetRecord *traceModelAsset = nullptr) {
    if (!canRunNativeOnnxSmokeTest(inputDescriptors, outputDescriptors, traceInputs)) {
        inspectStream << "run_status\tskipped\n";
        return;
    }
    OrtMemoryInfo *memoryInfo = nullptr;
    std::vector<std::vector<uint8_t>> inputBuffers;
    std::vector<OrtValue *> inputValues;
    std::vector<OrtValue *> outputValues(outputDescriptors.size(), nullptr);
    std::vector<NativeOnnxTraceInput> actualInputTensors;
    std::vector<NativeOnnxTraceInput> actualOutputTensors;
    inputBuffers.reserve(inputDescriptors.size());
    inputValues.reserve(inputDescriptors.size());
    if (traceModelAsset) {
        actualInputTensors.reserve(inputDescriptors.size());
        actualOutputTensors.reserve(outputDescriptors.size());
    }
    try {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createCpuMemoryInfo(0, 0, &memoryInfo), "CPU memory info 作成");
        std::vector<const char *> inputNames;
        std::vector<const OrtValue *> inputPointers;
        size_t totalInputElements = 0;
        inputNames.reserve(inputDescriptors.size());
        inputPointers.reserve(inputDescriptors.size());
        for (size_t inputIndex = 0; inputIndex < inputDescriptors.size(); inputIndex++) {
            const NativeOnnxValueDescriptor &inputDescriptor = inputDescriptors[inputIndex];
            const NativeOnnxTraceInput *traceInput = findNativeOnnxTraceTensor(traceInputs, inputDescriptor.name);
            std::vector<int64_t> inputDimensions = inputDescriptor.dimensions;
            if (traceInput) {
                inputDimensions = traceInput->dimensions;
                inputBuffers.push_back(traceInput->bytes);
            } else {
                size_t inputElementCount = calculatePositiveShapeElementCount(inputDescriptor.dimensions);
                inputBuffers.push_back(createNativeOnnxInputBytes(inputDescriptor, inputIndex, inputElementCount));
            }
            size_t inputElementCount = calculatePositiveShapeElementCount(inputDimensions);
            totalInputElements += inputElementCount;
            OrtValue *inputValue = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createTensorWithDataAsOrtValue(memoryInfo, inputBuffers.back().data(), inputBuffers.back().size(), inputDimensions.data(), inputDimensions.size(), inputDescriptor.elementType, &inputValue), "input tensor 作成");
            inputValues.push_back(inputValue);
            inputPointers.push_back(inputValue);
            inputNames.push_back(inputDescriptor.name.c_str());
            if (traceModelAsset) {
                NativeOnnxTraceInput actualInput;
                actualInput.name = inputDescriptor.name;
                actualInput.elementType = inputDescriptor.elementType;
                actualInput.dimensions = inputDimensions;
                actualInput.bytes = inputBuffers.back();
                actualInputTensors.push_back(std::move(actualInput));
            }
        }
        std::vector<const char *> outputNames;
        outputNames.reserve(outputDescriptors.size());
        for (const NativeOnnxValueDescriptor &outputDescriptor : outputDescriptors) {
            outputNames.push_back(outputDescriptor.name.c_str());
        }
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.run(session, nullptr, inputNames.data(), inputPointers.data(), inputPointers.size(), outputNames.data(), outputNames.size(), outputValues.data()), "ONNX Run");
        inspectStream << "run_status\tok\n";
        inspectStream << "run_input_count\t" << inputDescriptors.size() << "\n";
        inspectStream << "run_output_count\t" << outputDescriptors.size() << "\n";
        inspectStream << "run_input_elements\t" << totalInputElements << "\n";
        inspectStream << "run_output\tname\ttype\telements\tfirst\ttrace_match\n";
        for (size_t outputIndex = 0; outputIndex < outputValues.size(); outputIndex++) {
            OrtTensorTypeAndShapeInfo *outputShapeInfo = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorTypeAndShape(outputValues[outputIndex], &outputShapeInfo), "output tensor info 取得");
            size_t outputElementCount = 0;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorShapeElementCount(outputShapeInfo, &outputElementCount), "output element count 取得");
            std::vector<int64_t> outputDimensions = readNativeOnnxTensorDimensions(nativeOnnxApi, outputShapeInfo);
            void *outputData = nullptr;
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getTensorMutableData(outputValues[outputIndex], &outputData), "output tensor data 取得");
            size_t outputByteCount = outputElementCount * getNativeOnnxElementByteCount(outputDescriptors[outputIndex].elementType);
            std::string traceMatchText = compareNativeOnnxTraceOutput(outputDescriptors[outputIndex], outputDimensions, outputData, outputByteCount, traceOutputs);
            inspectStream << "output_" << outputIndex << "\t"
                          << outputDescriptors[outputIndex].name << "\t"
                          << formatNativeOnnxElementType(outputDescriptors[outputIndex].elementType) << "\t"
                          << outputElementCount << "\t"
                          << readNativeOnnxFirstValueText(outputDescriptors[outputIndex], outputData, outputElementCount) << "\t"
                          << traceMatchText << "\n";
            if (traceModelAsset) {
                NativeOnnxTraceInput actualOutput;
                actualOutput.name = outputDescriptors[outputIndex].name;
                actualOutput.elementType = outputDescriptors[outputIndex].elementType;
                actualOutput.dimensions = outputDimensions;
                actualOutput.bytes.resize(outputByteCount);
                if (outputByteCount > 0) {
                    std::memcpy(actualOutput.bytes.data(), outputData, outputByteCount);
                }
                actualOutputTensors.push_back(std::move(actualOutput));
            }
            nativeOnnxApi.releaseTensorTypeAndShapeInfo(outputShapeInfo);
        }
        if (traceModelAsset) {
            writeNativeOnnxTensorTrace(*traceModelAsset, actualInputTensors, actualOutputTensors);
        }
        for (OrtValue *outputValue : outputValues) {
            if (outputValue) {
                nativeOnnxApi.releaseValue(outputValue);
            }
        }
        for (OrtValue *inputValue : inputValues) {
            if (inputValue) {
                nativeOnnxApi.releaseValue(inputValue);
            }
        }
        nativeOnnxApi.releaseMemoryInfo(memoryInfo);
    } catch (...) {
        for (OrtValue *outputValue : outputValues) {
            if (outputValue) {
                nativeOnnxApi.releaseValue(outputValue);
            }
        }
        for (OrtValue *inputValue : inputValues) {
            if (inputValue) {
                nativeOnnxApi.releaseValue(inputValue);
            }
        }
        if (memoryInfo) {
            nativeOnnxApi.releaseMemoryInfo(memoryInfo);
        }
        throw;
    }
}

static void appendNativeOnnxSessionInfoFromBytes(std::ostringstream &inspectStream, NativeOnnxApi &nativeOnnxApi, const std::vector<uint8_t> &modelBytes, const fs::path &inputDirectory, uint16_t cpuThreadCount, bool shouldUseVvBinConfig, const ModelAssetRecord *traceModelAsset = nullptr) {
    std::vector<NativeOnnxTraceInput> traceInputs = loadNativeOnnxTraceInputs(inputDirectory);
    std::vector<NativeOnnxTraceInput> traceOutputs = loadNativeOnnxTraceOutputs(inputDirectory);
    OrtEnv *env = nullptr;
    OrtSessionOptions *sessionOptions = nullptr;
    OrtSession *session = nullptr;
    OrtAllocator *allocator = nullptr;
    try {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createEnv(ortLoggingLevelWarning, "litevox-native", &env), "OrtEnv 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.getAllocatorWithDefaultOptions(&allocator), "default allocator 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionOptions(&sessionOptions), "SessionOptions 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setSessionGraphOptimizationLevel(sessionOptions, ortGraphOptimizationLevelBasic), "graph optimization 設定");
        if (cpuThreadCount > 0) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setIntraOpNumThreads(sessionOptions, cpuThreadCount), "intra op thread 設定");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setInterOpNumThreads(sessionOptions, cpuThreadCount), "inter op thread 設定");
        }
        if (shouldUseVvBinConfig) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.addSessionConfigEntry(sessionOptions, "session.use_vv_bin", "1"), "vv_bin session 設定");
        }
        applyNativeOnnxSeedIfConfigured(nativeOnnxApi);
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionFromArray(env, modelBytes.data(), modelBytes.size(), sessionOptions, &session), "ONNX session 作成");
        size_t inputCount = 0;
        size_t outputCount = 0;
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetInputCount(session, &inputCount), "input count 取得");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.sessionGetOutputCount(session, &outputCount), "output count 取得");
        inspectStream << "model_bytes\t" << modelBytes.size() << "\n";
        inspectStream << "trace_input_count\t" << traceInputs.size() << "\n";
        inspectStream << "trace_output_count\t" << traceOutputs.size() << "\n";
        inspectStream << "session_status\tcreated\n";
        inspectStream << "input_count\t" << inputCount << "\n";
        inspectStream << "output_count\t" << outputCount << "\n";
        inspectStream << "value\tname\ttype\tshape\n";
        std::vector<NativeOnnxValueDescriptor> inputDescriptors;
        std::vector<NativeOnnxValueDescriptor> outputDescriptors;
        inputDescriptors.reserve(inputCount);
        outputDescriptors.reserve(outputCount);
        for (size_t inputIndex = 0; inputIndex < inputCount; inputIndex++) {
            appendNativeOnnxValueInfo(inspectStream, nativeOnnxApi, session, allocator, "input", inputIndex);
            inputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, session, allocator, true, inputIndex));
        }
        for (size_t outputIndex = 0; outputIndex < outputCount; outputIndex++) {
            appendNativeOnnxValueInfo(inspectStream, nativeOnnxApi, session, allocator, "output", outputIndex);
            outputDescriptors.push_back(readNativeOnnxValueDescriptor(nativeOnnxApi, session, allocator, false, outputIndex));
        }
        appendNativeOnnxSmokeRunInfo(inspectStream, nativeOnnxApi, session, inputDescriptors, outputDescriptors, traceInputs, traceOutputs, traceModelAsset);
        nativeOnnxApi.releaseSession(session);
        nativeOnnxApi.releaseSessionOptions(sessionOptions);
        nativeOnnxApi.releaseEnv(env);
    } catch (...) {
        if (session) {
            nativeOnnxApi.releaseSession(session);
        }
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
        }
        if (env) {
            nativeOnnxApi.releaseEnv(env);
        }
        throw;
    }
}

static void appendNativeOnnxSessionInfo(std::ostringstream &inspectStream, NativeOnnxApi &nativeOnnxApi, const fs::path &modelPath, const fs::path &inputDirectory, uint16_t cpuThreadCount) {
    std::vector<uint8_t> modelBytes = readNativeOnnxBinaryFile(modelPath);
    inspectStream << "model_path\t" << modelPath.string() << "\n";
    ModelAssetRecord traceModelAsset = createNativeOnnxPseudoModelAsset(modelPath);
    appendNativeOnnxSessionInfoFromBytes(inspectStream, nativeOnnxApi, modelBytes, inputDirectory, cpuThreadCount, false, &traceModelAsset);
}

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

static std::string findNativeOnnxFieldText(const std::string &tableText, const std::string &fieldName) {
    std::istringstream inputStream(tableText);
    std::string lineText;
    const std::string fieldPrefix = fieldName + "\t";
    while (std::getline(inputStream, lineText)) {
        if (lineText.rfind(fieldPrefix, 0) == 0) {
            return lineText.substr(fieldPrefix.size());
        }
    }
    return "-";
}

static void sanitizeNativeOnnxTableCell(std::string &cellText) {
    for (char &character : cellText) {
        if (character == '\t' || character == '\n' || character == '\r') {
            character = ' ';
        }
    }
}

static std::string summarizeNativeOnnxValueLines(const std::string &tableText) {
    std::istringstream inputStream(tableText);
    std::string lineText;
    std::vector<std::string> valueTexts;
    bool isValueTable = false;
    while (std::getline(inputStream, lineText)) {
        if (lineText == "value\tname\ttype\tshape") {
            isValueTable = true;
            continue;
        }
        if (lineText.rfind("run_status\t", 0) == 0) {
            isValueTable = false;
        }
        if (!isValueTable) {
            continue;
        }
        bool isInputLine = lineText.rfind("input_", 0) == 0 && lineText.size() > 6 && std::isdigit(static_cast<unsigned char>(lineText[6]));
        bool isOutputLine = lineText.rfind("output_", 0) == 0 && lineText.size() > 7 && std::isdigit(static_cast<unsigned char>(lineText[7]));
        if (!isInputLine && !isOutputLine) {
            continue;
        }
        sanitizeNativeOnnxTableCell(lineText);
        valueTexts.push_back(lineText);
    }
    std::ostringstream summaryStream;
    for (size_t valueIndex = 0; valueIndex < valueTexts.size(); valueIndex++) {
        if (valueIndex > 0) {
            summaryStream << "; ";
        }
        summaryStream << valueTexts[valueIndex];
    }
    return summaryStream.str();
}

static std::string summarizeNativeOnnxTraceMatches(const std::string &tableText) {
    std::istringstream inputStream(tableText);
    std::string lineText;
    std::vector<std::string> matchTexts;
    bool isRunOutputTable = false;
    while (std::getline(inputStream, lineText)) {
        if (lineText == "run_output\tname\ttype\telements\tfirst\ttrace_match") {
            isRunOutputTable = true;
            continue;
        }
        if (!isRunOutputTable) {
            continue;
        }
        bool isOutputLine = lineText.rfind("output_", 0) == 0 && lineText.size() > 7 && std::isdigit(static_cast<unsigned char>(lineText[7]));
        if (!isOutputLine) {
            continue;
        }
        std::istringstream lineStream(lineText);
        std::string valueIdText;
        std::string nameText;
        std::string typeText;
        std::string elementsText;
        std::string firstText;
        std::string traceMatchText;
        std::getline(lineStream, valueIdText, '\t');
        std::getline(lineStream, nameText, '\t');
        std::getline(lineStream, typeText, '\t');
        std::getline(lineStream, elementsText, '\t');
        std::getline(lineStream, firstText, '\t');
        std::getline(lineStream, traceMatchText, '\t');
        if (!nameText.empty() && !traceMatchText.empty()) {
            matchTexts.push_back(nameText + "=" + traceMatchText);
        }
    }
    if (matchTexts.empty()) {
        return "-";
    }
    std::ostringstream summaryStream;
    for (size_t matchIndex = 0; matchIndex < matchTexts.size(); matchIndex++) {
        if (matchIndex > 0) {
            summaryStream << "; ";
        }
        summaryStream << matchTexts[matchIndex];
    }
    return summaryStream.str();
}

static std::string joinNativeOnnxStringValues(const std::vector<std::string> &values) {
    std::ostringstream valueStream;
    for (size_t valueIndex = 0; valueIndex < values.size(); valueIndex++) {
        if (valueIndex > 0) {
            valueStream << ",";
        }
        valueStream << values[valueIndex];
    }
    return valueStream.str();
}

static std::string createNativeOnnxOperatorKey(const std::string &domainName, const std::string &operationName) {
    if (domainName.empty()) {
        return operationName;
    }
    return domainName + ":" + operationName;
}

static std::string createNativeOnnxTemporaryModelBaseName(const ModelAssetRecord &modelAsset, const std::vector<uint8_t> &modelBytes) {
    std::string fileName = modelAsset.archivePath.stem().string() + "-" + modelAsset.entryName;
    for (char &character : fileName) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_' || character == '.')) {
            character = '_';
        }
    }
    return fileName + "-" + createSha256Hex(modelBytes.data(), modelBytes.size()).substr(0, 16);
}

static fs::path createNativeOnnxTemporaryModelPath(const ModelAssetRecord &modelAsset, const std::vector<uint8_t> &modelBytes) {
    return createTemporaryFilePath(createNativeOnnxTemporaryModelBaseName(modelAsset, modelBytes), ".onnx");
}

static std::vector<uint8_t> exportNativeOnnxOptimizedModelBytes(NativeOnnxApi &nativeOnnxApi, const ModelAssetRecord &modelAsset, const std::vector<uint8_t> &modelBytes, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    OrtEnv *env = nullptr;
    OrtSessionOptions *sessionOptions = nullptr;
    OrtSession *session = nullptr;
    fs::path optimizedModelPath = createNativeOnnxTemporaryModelPath(modelAsset, modelBytes);
    try {
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createEnv(ortLoggingLevelWarning, "litevox-native-export", &env), "OrtEnv 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionOptions(&sessionOptions), "SessionOptions 作成");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setOptimizedModelFilePath(sessionOptions, optimizedModelPath.c_str()), "optimized model path 設定");
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setSessionGraphOptimizationLevel(sessionOptions, ortGraphOptimizationLevelBasic), "graph optimization 設定");
        if (cpuThreadCount > 0) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setIntraOpNumThreads(sessionOptions, cpuThreadCount), "intra op thread 設定");
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.setInterOpNumThreads(sessionOptions, cpuThreadCount), "inter op thread 設定");
        }
        if (shouldUseVvBinConfig) {
            ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.addSessionConfigEntry(sessionOptions, "session.use_vv_bin", "1"), "vv_bin session 設定");
        }
        applyNativeOnnxSeedIfConfigured(nativeOnnxApi);
        ensureNativeOnnxCall(nativeOnnxApi, nativeOnnxApi.createSessionFromArray(env, modelBytes.data(), modelBytes.size(), sessionOptions, &session), "ONNX session 作成");
        if (session) {
            nativeOnnxApi.releaseSession(session);
            session = nullptr;
        }
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
            sessionOptions = nullptr;
        }
        if (env) {
            nativeOnnxApi.releaseEnv(env);
            env = nullptr;
        }
        std::vector<uint8_t> optimizedModelBytes = readNativeOnnxBinaryFile(optimizedModelPath);
        fs::remove(optimizedModelPath);
        return optimizedModelBytes;
    } catch (...) {
        if (session) {
            nativeOnnxApi.releaseSession(session);
        }
        if (sessionOptions) {
            nativeOnnxApi.releaseSessionOptions(sessionOptions);
        }
        if (env) {
            nativeOnnxApi.releaseEnv(env);
        }
        if (!optimizedModelPath.empty() && fs::exists(optimizedModelPath)) {
            fs::remove(optimizedModelPath);
        }
        throw;
    }
}

static uint64_t readNativeOnnxProtoVarint(const uint8_t *messageBytes, size_t messageSize, size_t &offset) {
    uint64_t value = 0;
    int shift = 0;
    while (offset < messageSize && shift < 64) {
        uint8_t byteValue = messageBytes[offset++];
        value |= static_cast<uint64_t>(byteValue & 0x7f) << shift;
        if ((byteValue & 0x80) == 0) {
            return value;
        }
        shift += 7;
    }
    throw std::runtime_error("protobuf varint が不正です");
}

static std::pair<size_t, size_t> readNativeOnnxProtoLengthDelimitedRange(const uint8_t *messageBytes, size_t messageSize, size_t &offset) {
    uint64_t fieldSize = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
    if (fieldSize > messageSize - offset) {
        throw std::runtime_error("protobuf length-delimited field が不正です");
    }
    size_t fieldOffset = offset;
    offset += static_cast<size_t>(fieldSize);
    return {fieldOffset, static_cast<size_t>(fieldSize)};
}

static std::string readNativeOnnxProtoString(const uint8_t *messageBytes, size_t messageSize, size_t &offset) {
    std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
    return std::string(reinterpret_cast<const char *>(messageBytes + fieldRange.first), fieldRange.second);
}

static void skipNativeOnnxProtoField(const uint8_t *messageBytes, size_t messageSize, size_t &offset, uint32_t wireType) {
    if (wireType == 0) {
        readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        return;
    }
    if (wireType == 1) {
        if (messageSize - offset < 8) {
            throw std::runtime_error("protobuf fixed64 が不正です");
        }
        offset += 8;
        return;
    }
    if (wireType == 2) {
        std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
        offset = fieldRange.first + fieldRange.second;
        return;
    }
    if (wireType == 5) {
        if (messageSize - offset < 4) {
            throw std::runtime_error("protobuf fixed32 が不正です");
        }
        offset += 4;
        return;
    }
    throw std::runtime_error("protobuf wire type に未対応です");
}

static void appendNativeOnnxProtoVarint(std::vector<uint8_t> &messageBytes, uint64_t value) {
    while (value >= 0x80) {
        messageBytes.push_back(static_cast<uint8_t>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    messageBytes.push_back(static_cast<uint8_t>(value));
}

static void appendNativeOnnxProtoTag(std::vector<uint8_t> &messageBytes, uint32_t fieldNumber, uint32_t wireType) {
    appendNativeOnnxProtoVarint(messageBytes, (static_cast<uint64_t>(fieldNumber) << 3) | wireType);
}

static void appendNativeOnnxProtoBytes(std::vector<uint8_t> &messageBytes, const uint8_t *fieldBytes, size_t fieldSize) {
    messageBytes.insert(messageBytes.end(), fieldBytes, fieldBytes + fieldSize);
}

static void appendNativeOnnxProtoLengthDelimitedField(std::vector<uint8_t> &messageBytes, uint32_t fieldNumber, const std::vector<uint8_t> &fieldValue) {
    appendNativeOnnxProtoTag(messageBytes, fieldNumber, 2);
    appendNativeOnnxProtoVarint(messageBytes, fieldValue.size());
    appendNativeOnnxProtoBytes(messageBytes, fieldValue.data(), fieldValue.size());
}

static void appendNativeOnnxProtoStringField(std::vector<uint8_t> &messageBytes, uint32_t fieldNumber, const std::string &fieldValue) {
    appendNativeOnnxProtoTag(messageBytes, fieldNumber, 2);
    appendNativeOnnxProtoVarint(messageBytes, fieldValue.size());
    appendNativeOnnxProtoBytes(messageBytes, reinterpret_cast<const uint8_t *>(fieldValue.data()), fieldValue.size());
}

static void appendNativeOnnxProtoFloatField(std::vector<uint8_t> &messageBytes, uint32_t fieldNumber, float fieldValue) {
    appendNativeOnnxProtoTag(messageBytes, fieldNumber, 5);
    uint32_t rawBits = 0;
    std::memcpy(&rawBits, &fieldValue, sizeof(rawBits));
    messageBytes.push_back(static_cast<uint8_t>(rawBits & 0xff));
    messageBytes.push_back(static_cast<uint8_t>((rawBits >> 8) & 0xff));
    messageBytes.push_back(static_cast<uint8_t>((rawBits >> 16) & 0xff));
    messageBytes.push_back(static_cast<uint8_t>((rawBits >> 24) & 0xff));
}

static void appendNativeOnnxProtoIntField(std::vector<uint8_t> &messageBytes, uint32_t fieldNumber, uint64_t fieldValue) {
    appendNativeOnnxProtoTag(messageBytes, fieldNumber, 0);
    appendNativeOnnxProtoVarint(messageBytes, fieldValue);
}

struct NativeOnnxProtoFieldRewrite {
    uint32_t fieldNumber = 0;
    std::string attributeName;
    std::vector<uint8_t> bytes;
};

static std::vector<uint8_t> rewriteNativeOnnxModelRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount);
static std::vector<uint8_t> rewriteNativeOnnxGraphRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount);

static std::vector<uint8_t> createNativeOnnxSeedAttributeBytes(float seedValue) {
    std::vector<uint8_t> attributeBytes;
    appendNativeOnnxProtoStringField(attributeBytes, 1, "seed");
    appendNativeOnnxProtoFloatField(attributeBytes, 2, seedValue);
    appendNativeOnnxProtoIntField(attributeBytes, 20, 1);
    return attributeBytes;
}

static std::vector<uint8_t> rewriteNativeOnnxAttributeRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount, std::string *attributeName) {
    size_t offset = 0;
    std::vector<uint8_t> rewrittenBytes;
    std::string parsedAttributeName;
    while (offset < messageSize) {
        size_t fieldStart = offset;
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 1 && wireType == 2) {
            parsedAttributeName = readNativeOnnxProtoString(messageBytes, messageSize, offset);
            appendNativeOnnxProtoBytes(rewrittenBytes, messageBytes + fieldStart, offset - fieldStart);
            continue;
        }
        if ((fieldNumber == 6 || fieldNumber == 11) && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            std::vector<uint8_t> rewrittenGraphBytes = rewriteNativeOnnxGraphRandomSeed(messageBytes + fieldRange.first, fieldRange.second, seedValue, rewrittenNodeCount);
            appendNativeOnnxProtoLengthDelimitedField(rewrittenBytes, fieldNumber, rewrittenGraphBytes);
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
        appendNativeOnnxProtoBytes(rewrittenBytes, messageBytes + fieldStart, offset - fieldStart);
    }
    if (attributeName) {
        *attributeName = parsedAttributeName;
    }
    return rewrittenBytes;
}

static std::vector<uint8_t> rewriteNativeOnnxNodeRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount) {
    size_t offset = 0;
    std::string operationName;
    std::vector<NativeOnnxProtoFieldRewrite> fieldRewrites;
    while (offset < messageSize) {
        size_t fieldStart = offset;
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 4 && wireType == 2) {
            operationName = readNativeOnnxProtoString(messageBytes, messageSize, offset);
            NativeOnnxProtoFieldRewrite fieldRewrite;
            fieldRewrite.fieldNumber = fieldNumber;
            fieldRewrite.bytes.insert(fieldRewrite.bytes.end(), messageBytes + fieldStart, messageBytes + offset);
            fieldRewrites.push_back(std::move(fieldRewrite));
            continue;
        }
        if (fieldNumber == 5 && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            NativeOnnxProtoFieldRewrite fieldRewrite;
            fieldRewrite.fieldNumber = fieldNumber;
            std::vector<uint8_t> rewrittenAttributeBytes = rewriteNativeOnnxAttributeRandomSeed(messageBytes + fieldRange.first, fieldRange.second, seedValue, rewrittenNodeCount, &fieldRewrite.attributeName);
            appendNativeOnnxProtoLengthDelimitedField(fieldRewrite.bytes, fieldNumber, rewrittenAttributeBytes);
            fieldRewrites.push_back(std::move(fieldRewrite));
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
        NativeOnnxProtoFieldRewrite fieldRewrite;
        fieldRewrite.fieldNumber = fieldNumber;
        fieldRewrite.bytes.insert(fieldRewrite.bytes.end(), messageBytes + fieldStart, messageBytes + offset);
        fieldRewrites.push_back(std::move(fieldRewrite));
    }
    if (operationName != "RandomNormalLike") {
        std::vector<uint8_t> rewrittenBytes;
        for (const NativeOnnxProtoFieldRewrite &fieldRewrite : fieldRewrites) {
            appendNativeOnnxProtoBytes(rewrittenBytes, fieldRewrite.bytes.data(), fieldRewrite.bytes.size());
        }
        return rewrittenBytes;
    }
    rewrittenNodeCount++;
    std::vector<uint8_t> rewrittenBytes;
    bool hasSeedAttribute = false;
    for (const NativeOnnxProtoFieldRewrite &fieldRewrite : fieldRewrites) {
        if (fieldRewrite.fieldNumber == 5 && fieldRewrite.attributeName == "seed") {
            appendNativeOnnxProtoLengthDelimitedField(rewrittenBytes, 5, createNativeOnnxSeedAttributeBytes(seedValue));
            hasSeedAttribute = true;
            continue;
        }
        appendNativeOnnxProtoBytes(rewrittenBytes, fieldRewrite.bytes.data(), fieldRewrite.bytes.size());
    }
    if (!hasSeedAttribute) {
        appendNativeOnnxProtoLengthDelimitedField(rewrittenBytes, 5, createNativeOnnxSeedAttributeBytes(seedValue));
    }
    return rewrittenBytes;
}

static std::vector<uint8_t> rewriteNativeOnnxGraphRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount) {
    size_t offset = 0;
    std::vector<uint8_t> rewrittenBytes;
    while (offset < messageSize) {
        size_t fieldStart = offset;
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 1 && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            std::vector<uint8_t> rewrittenNodeBytes = rewriteNativeOnnxNodeRandomSeed(messageBytes + fieldRange.first, fieldRange.second, seedValue, rewrittenNodeCount);
            appendNativeOnnxProtoLengthDelimitedField(rewrittenBytes, fieldNumber, rewrittenNodeBytes);
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
        appendNativeOnnxProtoBytes(rewrittenBytes, messageBytes + fieldStart, offset - fieldStart);
    }
    return rewrittenBytes;
}

static std::vector<uint8_t> rewriteNativeOnnxModelRandomSeed(const uint8_t *messageBytes, size_t messageSize, float seedValue, size_t &rewrittenNodeCount) {
    size_t offset = 0;
    std::vector<uint8_t> rewrittenBytes;
    while (offset < messageSize) {
        size_t fieldStart = offset;
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 7 && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            std::vector<uint8_t> rewrittenGraphBytes = rewriteNativeOnnxGraphRandomSeed(messageBytes + fieldRange.first, fieldRange.second, seedValue, rewrittenNodeCount);
            appendNativeOnnxProtoLengthDelimitedField(rewrittenBytes, fieldNumber, rewrittenGraphBytes);
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
        appendNativeOnnxProtoBytes(rewrittenBytes, messageBytes + fieldStart, offset - fieldStart);
    }
    return rewrittenBytes;
}

static void collectNativeOnnxGraphOperatorCounts(const uint8_t *messageBytes, size_t messageSize, std::map<std::string, size_t> &operatorCounts);

static void collectNativeOnnxAttributeGraphOperatorCounts(const uint8_t *messageBytes, size_t messageSize, std::map<std::string, size_t> &operatorCounts) {
    size_t offset = 0;
    while (offset < messageSize) {
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if ((fieldNumber == 6 || fieldNumber == 11) && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            collectNativeOnnxGraphOperatorCounts(messageBytes + fieldRange.first, fieldRange.second, operatorCounts);
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
    }
}

static void collectNativeOnnxNodeOperatorCounts(const uint8_t *messageBytes, size_t messageSize, std::map<std::string, size_t> &operatorCounts) {
    size_t offset = 0;
    std::string operationName;
    std::string domainName;
    while (offset < messageSize) {
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 4 && wireType == 2) {
            operationName = readNativeOnnxProtoString(messageBytes, messageSize, offset);
            continue;
        }
        if (fieldNumber == 7 && wireType == 2) {
            domainName = readNativeOnnxProtoString(messageBytes, messageSize, offset);
            continue;
        }
        if (fieldNumber == 5 && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            collectNativeOnnxAttributeGraphOperatorCounts(messageBytes + fieldRange.first, fieldRange.second, operatorCounts);
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
    }
    if (!operationName.empty()) {
        operatorCounts[createNativeOnnxOperatorKey(domainName, operationName)]++;
    }
}

static void collectNativeOnnxGraphOperatorCounts(const uint8_t *messageBytes, size_t messageSize, std::map<std::string, size_t> &operatorCounts) {
    size_t offset = 0;
    while (offset < messageSize) {
        uint64_t tagValue = readNativeOnnxProtoVarint(messageBytes, messageSize, offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 1 && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(messageBytes, messageSize, offset);
            collectNativeOnnxNodeOperatorCounts(messageBytes + fieldRange.first, fieldRange.second, operatorCounts);
            continue;
        }
        skipNativeOnnxProtoField(messageBytes, messageSize, offset, wireType);
    }
}

static std::map<std::string, size_t> collectNativeOnnxOperatorCounts(const std::vector<uint8_t> &optimizedModelBytes) {
    size_t offset = 0;
    std::map<std::string, size_t> operatorCounts;
    while (offset < optimizedModelBytes.size()) {
        uint64_t tagValue = readNativeOnnxProtoVarint(optimizedModelBytes.data(), optimizedModelBytes.size(), offset);
        uint32_t fieldNumber = static_cast<uint32_t>(tagValue >> 3);
        uint32_t wireType = static_cast<uint32_t>(tagValue & 0x7);
        if (fieldNumber == 7 && wireType == 2) {
            std::pair<size_t, size_t> fieldRange = readNativeOnnxProtoLengthDelimitedRange(optimizedModelBytes.data(), optimizedModelBytes.size(), offset);
            collectNativeOnnxGraphOperatorCounts(optimizedModelBytes.data() + fieldRange.first, fieldRange.second, operatorCounts);
            continue;
        }
        skipNativeOnnxProtoField(optimizedModelBytes.data(), optimizedModelBytes.size(), offset, wireType);
    }
    return operatorCounts;
}

static size_t countNativeOnnxOperators(const std::map<std::string, size_t> &operatorCounts) {
    size_t operatorCount = 0;
    for (const auto &operatorEntry : operatorCounts) {
        operatorCount += operatorEntry.second;
    }
    return operatorCount;
}

static std::string summarizeNativeOnnxOperatorCounts(const std::map<std::string, size_t> &operatorCounts) {
    std::vector<std::pair<std::string, size_t>> operatorEntries(operatorCounts.begin(), operatorCounts.end());
    std::sort(operatorEntries.begin(), operatorEntries.end(), [](const auto &leftEntry, const auto &rightEntry) {
        if (leftEntry.second != rightEntry.second) {
            return leftEntry.second > rightEntry.second;
        }
        return leftEntry.first < rightEntry.first;
    });
    std::ostringstream summaryStream;
    for (size_t operatorIndex = 0; operatorIndex < operatorEntries.size(); operatorIndex++) {
        if (operatorIndex > 0) {
            summaryStream << ",";
        }
        summaryStream << operatorEntries[operatorIndex].first << ":" << operatorEntries[operatorIndex].second;
    }
    return summaryStream.str();
}

static std::string extractNativeOnnxOperatorName(const std::string &operatorKey) {
    size_t separatorPosition = operatorKey.rfind(':');
    if (separatorPosition == std::string::npos) {
        return operatorKey;
    }
    return operatorKey.substr(separatorPosition + 1);
}

static std::map<std::string, size_t> collectNativeOnnxRandomOperatorCounts(const std::map<std::string, size_t> &operatorCounts) {
    std::map<std::string, size_t> randomOperatorCounts;
    for (const auto &operatorEntry : operatorCounts) {
        if (extractNativeOnnxOperatorName(operatorEntry.first).rfind("Random", 0) != 0) {
            continue;
        }
        randomOperatorCounts.emplace(operatorEntry.first, operatorEntry.second);
    }
    return randomOperatorCounts;
}

static fs::path createNativeOnnxExportPath(const fs::path &outputDirectory, const ModelAssetRecord &modelAsset) {
    fs::path relativePath = fs::path(modelAsset.entryName);
    relativePath.replace_extension(".onnx");
    return outputDirectory / modelAsset.archivePath.stem() / relativePath;
}

static std::map<std::string, ManifestModelRecord> createNativeOnnxManifestModelMap(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::map<std::string, ManifestModelRecord> manifestModelMap;
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (const ManifestModelRecord &manifestModel : collectManifestModels(archiveSummary)) {
            manifestModelMap[manifestModel.archivePath.string() + "\t" + manifestModel.entryName] = manifestModel;
        }
    }
    return manifestModelMap;
}

static const ManifestModelRecord *findNativeOnnxManifestModelRecord(const std::map<std::string, ManifestModelRecord> &manifestModelMap, const ModelAssetRecord &modelAsset) {
    auto manifestIterator = manifestModelMap.find(modelAsset.archivePath.string() + "\t" + modelAsset.entryName);
    if (manifestIterator == manifestModelMap.end()) {
        return nullptr;
    }
    return &manifestIterator->second;
}

static std::string summarizeNativeOnnxValueDescriptors(const std::vector<NativeOnnxValueDescriptor> &valueDescriptors) {
    std::ostringstream summaryStream;
    for (size_t valueIndex = 0; valueIndex < valueDescriptors.size(); valueIndex++) {
        if (valueIndex > 0) {
            summaryStream << ";";
        }
        summaryStream << valueDescriptors[valueIndex].name
                      << ":"
                      << formatNativeOnnxElementType(valueDescriptors[valueIndex].elementType)
                      << formatNativeOnnxShape(valueDescriptors[valueIndex].dimensions);
    }
    return summaryStream.str();
}

struct NativeOnnxPreparedInputSet {
    std::vector<NativeOnnxTraceInput> tensors;
    size_t traceInputCount = 0;
    size_t syntheticInputCount = 0;
};

struct NativeOnnxCompareBenchmark {
    size_t inputCount = 0;
    size_t outputCount = 0;
    double sessionCreateMilliseconds = 0.0;
    double averageRunMilliseconds = 0.0;
    std::vector<NativeOnnxTraceInput> outputs;
};

static const ModelAssetRecord &requireNativeOnnxModelAsset(const std::vector<ModelAssetRecord> &modelAssets, const std::string &entryName);
static std::vector<NativeOnnxTraceInput> createNativeOnnxDecoderInputsFromAudioQuery(const std::vector<NativeOnnxTraceInput> &frontendInputs, const std::string &audioQueryText, const NativeOnnxAudioQuerySettings &audioQuerySettings);

static NativeOnnxPreparedInputSet createNativeOnnxPreparedInputs(const std::vector<NativeOnnxValueDescriptor> &inputDescriptors, const std::vector<NativeOnnxTraceInput> &traceInputs) {
    NativeOnnxPreparedInputSet preparedInputs;
    preparedInputs.tensors.reserve(inputDescriptors.size());
    for (size_t inputIndex = 0; inputIndex < inputDescriptors.size(); inputIndex++) {
        const NativeOnnxValueDescriptor &inputDescriptor = inputDescriptors[inputIndex];
        const NativeOnnxTraceInput *traceInput = findNativeOnnxTraceTensor(traceInputs, inputDescriptor.name);
        if (traceInput) {
            if (traceInput->elementType != inputDescriptor.elementType) {
                throw std::runtime_error("trace input dtype が一致しません: " + inputDescriptor.name);
            }
            if (!areNativeOnnxDimensionsEqual(inputDescriptor.dimensions, traceInput->dimensions)) {
                throw std::runtime_error("trace input shape が一致しません: " + inputDescriptor.name);
            }
            preparedInputs.tensors.push_back(*traceInput);
            preparedInputs.traceInputCount++;
            continue;
        }
        if (!canCreateNativeOnnxSmokeInput(inputDescriptor)) {
            throw std::runtime_error("trace input が足りず synthetic input も作れません: " + inputDescriptor.name);
        }
        size_t inputElementCount = calculatePositiveShapeElementCount(inputDescriptor.dimensions);
        NativeOnnxTraceInput syntheticInput;
        syntheticInput.name = inputDescriptor.name;
        syntheticInput.elementType = inputDescriptor.elementType;
        syntheticInput.dimensions = inputDescriptor.dimensions;
        syntheticInput.bytes = createNativeOnnxInputBytes(inputDescriptor, inputIndex, inputElementCount);
        preparedInputs.tensors.push_back(std::move(syntheticInput));
        preparedInputs.syntheticInputCount++;
    }
    return preparedInputs;
}

static std::string createNativeOnnxInputModeText(const NativeOnnxPreparedInputSet &preparedInputs) {
    if (preparedInputs.traceInputCount > 0 && preparedInputs.syntheticInputCount == 0) {
        return "trace";
    }
    if (preparedInputs.traceInputCount == 0 && preparedInputs.syntheticInputCount > 0) {
        return "synthetic";
    }
    if (preparedInputs.traceInputCount == 0 && preparedInputs.syntheticInputCount == 0) {
        return "none";
    }
    return "mixed";
}

static std::string summarizeNativeOnnxTensorMatches(const std::vector<NativeOnnxTraceInput> &actualTensors, const std::vector<NativeOnnxTraceInput> &expectedTensors) {
    if (actualTensors.empty()) {
        return "empty";
    }
    std::vector<std::string> mismatchTexts;
    for (const NativeOnnxTraceInput &actualTensor : actualTensors) {
        std::string matchText = compareNativeOnnxTensors(actualTensor, findNativeOnnxTraceTensor(expectedTensors, actualTensor.name));
        if (matchText != "exact") {
            mismatchTexts.push_back(actualTensor.name + ":" + matchText);
        }
    }
    if (mismatchTexts.empty()) {
        return "exact";
    }
    std::ostringstream summaryStream;
    for (size_t mismatchIndex = 0; mismatchIndex < mismatchTexts.size(); mismatchIndex++) {
        if (mismatchIndex > 0) {
            summaryStream << ";";
        }
        summaryStream << mismatchTexts[mismatchIndex];
    }
    return summaryStream.str();
}

static NativeOnnxCompareBenchmark benchmarkNativeOnnxSession(const std::shared_ptr<NativeOnnxCachedSession> &cachedSession, NativeOnnxApi &nativeOnnxApi, const std::vector<NativeOnnxTraceInput> &inputTensors, size_t runCount) {
    NativeOnnxCompareBenchmark benchmark;
    benchmark.inputCount = cachedSession->inputDescriptors.size();
    benchmark.outputCount = cachedSession->outputDescriptors.size();
    if (runCount == 0) {
        return benchmark;
    }
    double totalRunMilliseconds = 0.0;
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        auto runStartedAt = std::chrono::steady_clock::now();
        benchmark.outputs = runNativeOnnxPreparedSession(nativeOnnxApi, cachedSession, inputTensors);
        auto runFinishedAt = std::chrono::steady_clock::now();
        totalRunMilliseconds += std::chrono::duration<double, std::milli>(runFinishedAt - runStartedAt).count();
    }
    benchmark.averageRunMilliseconds = totalRunMilliseconds / static_cast<double>(runCount);
    return benchmark;
}

static std::string formatNativeOnnxMilliseconds(double milliseconds) {
    std::ostringstream valueStream;
    valueStream << std::fixed << std::setprecision(3) << milliseconds;
    return valueStream.str();
}

static bool canCreateNativeOnnxCompareAudioQueryInputs(const ModelAssetRecord &modelAsset) {
    return modelAsset.entryName == "models/pd.bin" || modelAsset.entryName == "models/pi.bin" || modelAsset.entryName == "models/d.bin";
}

static std::vector<NativeOnnxTraceInput> createNativeOnnxCompareAudioQueryInputs(const std::vector<ModelAssetRecord> &modelAssets, const ModelAssetRecord &modelAsset, const std::string &audioQueryText, uint32_t styleId) {
    const ModelAssetRecord &durationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pd.bin");
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(durationAsset, "talk", styleId);
    std::vector<NativeOnnxTraceInput> frontendInputs = createNativeOnnxFrontendInputs(audioQueryText, innerVoiceId);
    if (modelAsset.entryName == "models/pd.bin" || modelAsset.entryName == "models/pi.bin") {
        return frontendInputs;
    }
    if (modelAsset.entryName == "models/d.bin") {
        return createNativeOnnxDecoderInputsFromAudioQuery(frontendInputs, audioQueryText, audioQuerySettings);
    }
    throw std::runtime_error("audio_query から入力を作れない asset です: " + modelAsset.entryName);
}

std::string createNativeOnnxVvmModelInfoText(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, uint16_t cpuThreadCount) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        std::map<std::string, ManifestModelRecord> manifestModelMap = createNativeOnnxManifestModelMap(archiveSummaries);
        std::vector<std::string> providers = collectNativeOnnxAvailableProviders(nativeOnnxApi);
        std::ostringstream inspectStream;
        inspectStream << "field\tvalue\n";
        inspectStream << "onnxruntime\t" << onnxruntimeLibraryPath.string() << "\n";
        inspectStream << "api_version\t" << ortApiVersion << "\n";
        inspectStream << "ort_version\t" << getNativeOnnxVersion(nativeOnnxApi) << "\n";
        inspectStream << "cpu_threads\t" << cpuThreadCount << "\n";
        inspectStream << "providers\t" << joinNativeOnnxStringValues(providers) << "\n";
        inspectStream << "model_count\t" << modelAssets.size() << "\n";
        inspectStream << "vvm\tasset\tdomain\toperation\tmodel_type\tbytes\tinput_count\toutput_count\tinputs\toutputs\n";
        for (const ModelAssetRecord &modelAsset : modelAssets) {
            const ManifestModelRecord *manifestModel = findNativeOnnxManifestModelRecord(manifestModelMap, modelAsset);
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            std::string sessionCacheKey = modelAsset.archivePath.string() + "\t" + modelAsset.entryName + "\t" + std::to_string(cpuThreadCount);
            std::shared_ptr<NativeOnnxCachedSession> cachedSession = getNativeOnnxCachedSession(nativeOnnxApi, nullptr, modelBytes, cpuThreadCount, true, sessionCacheKey);
            std::string inputSummary = summarizeNativeOnnxValueDescriptors(cachedSession->inputDescriptors);
            std::string outputSummary = summarizeNativeOnnxValueDescriptors(cachedSession->outputDescriptors);
            sanitizeNativeOnnxTableCell(inputSummary);
            sanitizeNativeOnnxTableCell(outputSummary);
            inspectStream << modelAsset.archivePath.filename().string() << "\t"
                          << modelAsset.entryName << "\t"
                          << (manifestModel ? manifestModel->domainName : "-") << "\t"
                          << (manifestModel ? manifestModel->operationName : "-") << "\t"
                          << (manifestModel ? manifestModel->modelType : "-") << "\t"
                          << modelAsset.uncompressedSize << "\t"
                          << cachedSession->inputDescriptors.size() << "\t"
                          << cachedSession->outputDescriptors.size() << "\t"
                          << inputSummary << "\t"
                          << outputSummary << "\n";
        }
        closeNativeOnnxApi(nativeOnnxApi);
        return inspectStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxVvmOperatorText(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, uint16_t cpuThreadCount) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        std::map<std::string, ManifestModelRecord> manifestModelMap = createNativeOnnxManifestModelMap(archiveSummaries);
        std::vector<std::string> providers = collectNativeOnnxAvailableProviders(nativeOnnxApi);
        std::map<std::string, size_t> globalOperatorCounts;
        std::ostringstream inspectStream;
        inspectStream << "field\tvalue\n";
        inspectStream << "onnxruntime\t" << onnxruntimeLibraryPath.string() << "\n";
        inspectStream << "api_version\t" << ortApiVersion << "\n";
        inspectStream << "ort_version\t" << getNativeOnnxVersion(nativeOnnxApi) << "\n";
        inspectStream << "cpu_threads\t" << cpuThreadCount << "\n";
        inspectStream << "providers\t" << joinNativeOnnxStringValues(providers) << "\n";
        inspectStream << "model_count\t" << modelAssets.size() << "\n";
        inspectStream << "vvm\tasset\tdomain\toperation\tmodel_type\tonnx_bytes\tnode_count\toperator_kinds\toperators\n";
        for (const ModelAssetRecord &modelAsset : modelAssets) {
            const ManifestModelRecord *manifestModel = findNativeOnnxManifestModelRecord(manifestModelMap, modelAsset);
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            std::vector<uint8_t> optimizedModelBytes = exportNativeOnnxOptimizedModelBytes(nativeOnnxApi, modelAsset, modelBytes, cpuThreadCount, true);
            std::map<std::string, size_t> operatorCounts = collectNativeOnnxOperatorCounts(optimizedModelBytes);
            for (const auto &operatorEntry : operatorCounts) {
                globalOperatorCounts[operatorEntry.first] += operatorEntry.second;
            }
            std::string operatorSummary = summarizeNativeOnnxOperatorCounts(operatorCounts);
            sanitizeNativeOnnxTableCell(operatorSummary);
            inspectStream << modelAsset.archivePath.filename().string() << "\t"
                          << modelAsset.entryName << "\t"
                          << (manifestModel ? manifestModel->domainName : "-") << "\t"
                          << (manifestModel ? manifestModel->operationName : "-") << "\t"
                          << (manifestModel ? manifestModel->modelType : "-") << "\t"
                          << optimizedModelBytes.size() << "\t"
                          << countNativeOnnxOperators(operatorCounts) << "\t"
                          << operatorCounts.size() << "\t"
                          << operatorSummary << "\n";
        }
        inspectStream << "global_operator_kinds\t" << globalOperatorCounts.size() << "\n";
        inspectStream << "global_operator_counts\t" << summarizeNativeOnnxOperatorCounts(globalOperatorCounts) << "\n";
        closeNativeOnnxApi(nativeOnnxApi);
        return inspectStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string exportNativeOnnxVvmOnnxFiles(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &outputDirectory, uint16_t cpuThreadCount, bool shouldPatchRandomSeed, float randomSeedValue) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        std::map<std::string, ManifestModelRecord> manifestModelMap = createNativeOnnxManifestModelMap(archiveSummaries);
        std::ostringstream exportStream;
        exportStream << "vvm\tasset\tdomain\toperation\tmodel_type\tstatus\toutput\tbytes\trandom_seed\trewritten_nodes\n";
        for (const ModelAssetRecord &modelAsset : modelAssets) {
            const ManifestModelRecord *manifestModel = findNativeOnnxManifestModelRecord(manifestModelMap, modelAsset);
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            std::vector<uint8_t> optimizedModelBytes = exportNativeOnnxOptimizedModelBytes(nativeOnnxApi, modelAsset, modelBytes, cpuThreadCount, true);
            size_t rewrittenNodeCount = 0;
            if (shouldPatchRandomSeed) {
                optimizedModelBytes = rewriteNativeOnnxModelRandomSeed(optimizedModelBytes.data(), optimizedModelBytes.size(), randomSeedValue, rewrittenNodeCount);
            }
            fs::path outputPath = createNativeOnnxExportPath(outputDirectory, modelAsset);
            writeBinaryFile(outputPath, optimizedModelBytes);
            exportStream << modelAsset.archivePath.filename().string() << "\t"
                         << modelAsset.entryName << "\t"
                         << (manifestModel ? manifestModel->domainName : "-") << "\t"
                         << (manifestModel ? manifestModel->operationName : "-") << "\t"
                         << (manifestModel ? manifestModel->modelType : "-") << "\t"
                         << "exported\t"
                         << outputPath.string() << "\t"
                         << optimizedModelBytes.size() << "\t";
            if (shouldPatchRandomSeed) {
                exportStream << std::setprecision(9) << static_cast<double>(randomSeedValue);
            } else {
                exportStream << "-";
            }
            exportStream << "\t" << rewrittenNodeCount << "\n";
        }
        closeNativeOnnxApi(nativeOnnxApi);
        return exportStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxVvmCompareText(const fs::path &vvBinOnnxruntimeLibraryPath, const fs::path &exportedOnnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &inputDirectory, const fs::path &audioQueryPath, uint32_t styleId, const std::string &assetFilter, uint16_t cpuThreadCount, size_t runCount) {
    NativeOnnxApi vvBinOnnxApi = loadNativeOnnxApi(vvBinOnnxruntimeLibraryPath);
    NativeOnnxApi exportedOnnxApi = loadNativeOnnxApi(exportedOnnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        std::map<std::string, ManifestModelRecord> manifestModelMap = createNativeOnnxManifestModelMap(archiveSummaries);
        std::vector<NativeOnnxTraceInput> traceInputs = loadNativeOnnxTraceInputs(inputDirectory);
        std::vector<NativeOnnxTraceInput> traceOutputs = loadNativeOnnxTraceOutputs(inputDirectory);
        bool hasAudioQuery = false;
        std::string audioQueryText;
        if (!audioQueryPath.empty()) {
            audioQueryText = readNativeOnnxTextFile(resolveNativeOnnxAudioQueryPath(inputDirectory, audioQueryPath));
            hasAudioQuery = true;
        } else if (!inputDirectory.empty()) {
            fs::path tracedAudioQueryPath = inputDirectory.parent_path() / "audio_query.json";
            if (fs::exists(tracedAudioQueryPath)) {
                audioQueryText = readNativeOnnxTextFile(tracedAudioQueryPath);
                hasAudioQuery = true;
            }
        }
        std::ostringstream compareStream;
        compareStream << "vvm\tasset\tdomain\toperation\tmodel_type\tstochastic\tstochastic_operators\tinput_mode\tinput_count\toutput_count\truns\tvv_bin_onnxruntime\texported_onnx_onnxruntime\tvv_bin_session_ms\tvv_bin_run_avg_ms\texported_session_ms\texported_run_avg_ms\toutput_match\ttrace_match_vv_bin\ttrace_match_exported\tstatus\tdetail\n";
        for (const ModelAssetRecord &modelAsset : modelAssets) {
            if (!assetFilter.empty() && modelAsset.entryName != assetFilter) {
                continue;
            }
            const ManifestModelRecord *manifestModel = findNativeOnnxManifestModelRecord(manifestModelMap, modelAsset);
            std::string domainText = manifestModel ? manifestModel->domainName : "-";
            std::string operationText = manifestModel ? manifestModel->operationName : "-";
            std::string modelTypeText = manifestModel ? manifestModel->modelType : "-";
            std::string inputModeText = "-";
            std::string outputMatchText = "-";
            std::string traceMatchVvBinText = "-";
            std::string traceMatchExportedText = "-";
            std::string stochasticText = "false";
            std::string stochasticOperatorsText = "-";
            std::string statusText = "ok";
            std::string detailText = "ok";
            std::string vvBinSessionText = "-";
            std::string vvBinRunText = "-";
            std::string exportedSessionText = "-";
            std::string exportedRunText = "-";
            size_t inputCount = 0;
            size_t outputCount = 0;
            try {
                std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                    modelAsset.archivePath,
                    modelAsset.entryName,
                    modelAsset.dataOffset,
                    modelAsset.compressedSize,
                    modelAsset.uncompressedSize,
                    modelAsset.compressionMethod);
                auto vvBinSessionStartedAt = std::chrono::steady_clock::now();
                std::shared_ptr<NativeOnnxCachedSession> vvBinSession = createNativeOnnxCachedSession(vvBinOnnxApi, nullptr, modelBytes, cpuThreadCount, true);
                auto vvBinSessionFinishedAt = std::chrono::steady_clock::now();
                std::vector<NativeOnnxTraceInput> sourceInputs = traceInputs;
                if (hasAudioQuery && canCreateNativeOnnxCompareAudioQueryInputs(modelAsset)) {
                    sourceInputs = createNativeOnnxCompareAudioQueryInputs(modelAssets, modelAsset, audioQueryText, styleId);
                }
                NativeOnnxPreparedInputSet preparedInputs = createNativeOnnxPreparedInputs(vvBinSession->inputDescriptors, sourceInputs);
                std::vector<uint8_t> optimizedModelBytes = exportNativeOnnxOptimizedModelBytes(vvBinOnnxApi, modelAsset, modelBytes, cpuThreadCount, true);
                std::map<std::string, size_t> randomOperatorCounts = collectNativeOnnxRandomOperatorCounts(collectNativeOnnxOperatorCounts(optimizedModelBytes));
                stochasticText = randomOperatorCounts.empty() ? "false" : "true";
                stochasticOperatorsText = randomOperatorCounts.empty() ? "-" : summarizeNativeOnnxOperatorCounts(randomOperatorCounts);
                auto exportedSessionStartedAt = std::chrono::steady_clock::now();
                std::shared_ptr<NativeOnnxCachedSession> exportedSession = createNativeOnnxCachedSession(exportedOnnxApi, nullptr, optimizedModelBytes, cpuThreadCount, false);
                auto exportedSessionFinishedAt = std::chrono::steady_clock::now();
                NativeOnnxCompareBenchmark vvBinBenchmark = benchmarkNativeOnnxSession(vvBinSession, vvBinOnnxApi, preparedInputs.tensors, runCount);
                NativeOnnxCompareBenchmark exportedBenchmark = benchmarkNativeOnnxSession(exportedSession, exportedOnnxApi, preparedInputs.tensors, runCount);
                vvBinBenchmark.sessionCreateMilliseconds = std::chrono::duration<double, std::milli>(vvBinSessionFinishedAt - vvBinSessionStartedAt).count();
                exportedBenchmark.sessionCreateMilliseconds = std::chrono::duration<double, std::milli>(exportedSessionFinishedAt - exportedSessionStartedAt).count();
                inputModeText = createNativeOnnxInputModeText(preparedInputs);
                inputCount = vvBinBenchmark.inputCount;
                outputCount = vvBinBenchmark.outputCount;
                vvBinSessionText = formatNativeOnnxMilliseconds(vvBinBenchmark.sessionCreateMilliseconds);
                vvBinRunText = formatNativeOnnxMilliseconds(vvBinBenchmark.averageRunMilliseconds);
                exportedSessionText = formatNativeOnnxMilliseconds(exportedBenchmark.sessionCreateMilliseconds);
                exportedRunText = formatNativeOnnxMilliseconds(exportedBenchmark.averageRunMilliseconds);
                inputModeText = hasAudioQuery && canCreateNativeOnnxCompareAudioQueryInputs(modelAsset) ? "audio_query" : createNativeOnnxInputModeText(preparedInputs);
                outputMatchText = summarizeNativeOnnxTensorMatches(vvBinBenchmark.outputs, exportedBenchmark.outputs);
                if (traceOutputs.empty()) {
                    traceMatchVvBinText = "not_available";
                    traceMatchExportedText = "not_available";
                } else {
                    traceMatchVvBinText = summarizeNativeOnnxTensorMatches(vvBinBenchmark.outputs, traceOutputs);
                    traceMatchExportedText = summarizeNativeOnnxTensorMatches(exportedBenchmark.outputs, traceOutputs);
                }
                if (stochasticText == "true") {
                    detailText = "stochastic:" + stochasticOperatorsText;
                }
            } catch (const std::exception &caughtException) {
                statusText = "failed";
                detailText = caughtException.what();
            }
            sanitizeNativeOnnxTableCell(domainText);
            sanitizeNativeOnnxTableCell(operationText);
            sanitizeNativeOnnxTableCell(modelTypeText);
            sanitizeNativeOnnxTableCell(stochasticText);
            sanitizeNativeOnnxTableCell(stochasticOperatorsText);
            sanitizeNativeOnnxTableCell(inputModeText);
            sanitizeNativeOnnxTableCell(outputMatchText);
            sanitizeNativeOnnxTableCell(traceMatchVvBinText);
            sanitizeNativeOnnxTableCell(traceMatchExportedText);
            sanitizeNativeOnnxTableCell(statusText);
            sanitizeNativeOnnxTableCell(detailText);
            compareStream << modelAsset.archivePath.filename().string() << "\t"
                          << modelAsset.entryName << "\t"
                          << domainText << "\t"
                          << operationText << "\t"
                          << modelTypeText << "\t"
                          << stochasticText << "\t"
                          << stochasticOperatorsText << "\t"
                          << inputModeText << "\t"
                          << inputCount << "\t"
                          << outputCount << "\t"
                          << runCount << "\t"
                          << vvBinOnnxruntimeLibraryPath.string() << "\t"
                          << exportedOnnxruntimeLibraryPath.string() << "\t"
                          << vvBinSessionText << "\t"
                          << vvBinRunText << "\t"
                          << exportedSessionText << "\t"
                          << exportedRunText << "\t"
                          << outputMatchText << "\t"
                          << traceMatchVvBinText << "\t"
                          << traceMatchExportedText << "\t"
                          << statusText << "\t"
                          << detailText << "\n";
        }
        closeNativeOnnxApi(exportedOnnxApi);
        closeNativeOnnxApi(vvBinOnnxApi);
        return compareStream.str();
    } catch (...) {
        closeNativeOnnxApi(exportedOnnxApi);
        closeNativeOnnxApi(vvBinOnnxApi);
        throw;
    }
}

std::string createNativeOnnxVvmInspectText(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &inputDirectory, uint16_t cpuThreadCount) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        std::ostringstream inspectStream;
        inspectStream << "vvm\tasset\tbytes\tsession_status\tinput_count\toutput_count\trun_status\ttrace_match\tdetail\n";
        for (const ModelAssetRecord &modelAsset : modelAssets) {
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            std::ostringstream sessionStream;
            std::string sessionStatus = "created";
            std::string inputCountText = "-";
            std::string outputCountText = "-";
            std::string runStatusText = "-";
            std::string traceMatchText = "-";
            std::string detailText;
            try {
                appendNativeOnnxSessionInfoFromBytes(sessionStream, nativeOnnxApi, modelBytes, inputDirectory, cpuThreadCount, true, &modelAsset);
                std::string sessionText = sessionStream.str();
                inputCountText = findNativeOnnxFieldText(sessionText, "input_count");
                outputCountText = findNativeOnnxFieldText(sessionText, "output_count");
                runStatusText = findNativeOnnxFieldText(sessionText, "run_status");
                traceMatchText = summarizeNativeOnnxTraceMatches(sessionText);
                detailText = summarizeNativeOnnxValueLines(sessionText);
                if (detailText.empty()) {
                    detailText = "ok";
                }
            } catch (const std::exception &caughtException) {
                sessionStatus = "failed";
                detailText = caughtException.what();
            }
            sanitizeNativeOnnxTableCell(detailText);
            inspectStream << modelAsset.archivePath.filename().string() << "\t"
                          << modelAsset.entryName << "\t"
                          << modelAsset.uncompressedSize << "\t"
                          << sessionStatus << "\t"
                          << inputCountText << "\t"
                          << outputCountText << "\t"
                          << runStatusText << "\t"
                          << traceMatchText << "\t"
                          << detailText << "\n";
        }
        closeNativeOnnxApi(nativeOnnxApi);
        return inspectStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

static const ModelAssetRecord &requireNativeOnnxModelAsset(const std::vector<ModelAssetRecord> &modelAssets, const std::string &entryName) {
    for (const ModelAssetRecord &modelAsset : modelAssets) {
        if (modelAsset.entryName == entryName) {
            return modelAsset;
        }
    }
    throw std::runtime_error("model asset がありません: " + entryName);
}

static std::vector<uint8_t> extractNativeOnnxModelAssetBytes(const ModelAssetRecord &modelAsset) {
    return extractVvmEntryBytesAt(
        modelAsset.archivePath,
        modelAsset.entryName,
        modelAsset.dataOffset,
        modelAsset.compressedSize,
        modelAsset.uncompressedSize,
        modelAsset.compressionMethod);
}

static std::string createNativeOnnxModelAssetSessionCacheKey(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::ostringstream keyStream;
    keyStream << nativeOnnxApi.libraryPath.string() << "\t";
    keyStream << modelAsset.archivePath.string() << "\t";
    keyStream << modelAsset.entryName << "\t";
    keyStream << modelAsset.dataOffset << "\t";
    keyStream << modelAsset.compressedSize << "\t";
    keyStream << modelAsset.uncompressedSize << "\t";
    keyStream << modelAsset.crc32 << "\t";
    keyStream << modelAsset.compressionMethod << "\t";
    keyStream << cpuThreadCount << "\t";
    keyStream << (shouldUseVvBinConfig ? "vvbin" : "onnx") << "\t";
    keyStream << (runtimeState ? runtimeState->selectedExecutionProvider : "CPUExecutionProvider");
    return keyStream.str();
}

static fs::path getNativeOnnxVvBinExportLibraryPath(NativeOnnxApi &nativeOnnxApi) {
    const char *libraryPathText = std::getenv("LITEVOX_VV_BIN_ONNXRUNTIME");
    if (!libraryPathText || libraryPathText[0] == '\0') {
        return nativeOnnxApi.libraryPath;
    }
    return fs::path(libraryPathText);
}

static std::string createNativeOnnxFileIdentityText(const fs::path &filePath) {
    std::error_code canonicalError;
    fs::path stablePath = fs::weakly_canonical(filePath, canonicalError);
    std::error_code sizeError;
    uintmax_t fileSize = fs::file_size(filePath, sizeError);
    std::error_code timeError;
    fs::file_time_type lastWriteTime = fs::last_write_time(filePath, timeError);
    std::ostringstream identityStream;
    identityStream << (canonicalError ? filePath.lexically_normal().string() : stablePath.string()) << "\t";
    identityStream << (sizeError ? 0 : fileSize) << "\t";
    identityStream << (timeError ? 0 : static_cast<long long>(lastWriteTime.time_since_epoch().count()));
    return identityStream.str();
}

static std::string createNativeOnnxExportedModelCacheKey(const ModelAssetRecord &modelAsset, const fs::path &exportLibraryPath, uint16_t cpuThreadCount) {
    std::ostringstream keyStream;
    keyStream << "exported_onnx\t";
    keyStream << createNativeOnnxFileIdentityText(exportLibraryPath) << "\t";
    keyStream << createNativeOnnxFileIdentityText(modelAsset.archivePath) << "\t";
    keyStream << modelAsset.entryName << "\t";
    keyStream << modelAsset.dataOffset << "\t";
    keyStream << modelAsset.compressedSize << "\t";
    keyStream << modelAsset.uncompressedSize << "\t";
    keyStream << modelAsset.crc32 << "\t";
    keyStream << modelAsset.compressionMethod << "\t";
    keyStream << cpuThreadCount;
    return keyStream.str();
}

static std::string createNativeOnnxExportedModelCacheBaseName(const ModelAssetRecord &modelAsset) {
    std::string fileName = modelAsset.archivePath.stem().string() + "-" + modelAsset.entryName;
    for (char &character : fileName) {
        if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_' || character == '.')) {
            character = '_';
        }
    }
    return fileName;
}

static fs::path createNativeOnnxExportedModelCachePath(const ModelAssetRecord &modelAsset, const std::string &cacheKey) {
    std::string cacheDigest = createSha256Hex(reinterpret_cast<const uint8_t *>(cacheKey.data()), cacheKey.size());
    return modelAsset.archivePath.parent_path() / ".litevox-exported-onnx-cache" / (createNativeOnnxExportedModelCacheBaseName(modelAsset) + "-" + cacheDigest.substr(0, 16) + ".onnx");
}

static void storeNativeOnnxExportedModelCacheFileHit() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
    nativeOnnxExportedModelFileHits++;
}

static void storeNativeOnnxExportedModelCacheFileMiss() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
    nativeOnnxExportedModelFileMisses++;
}

static void storeNativeOnnxExportedModelCacheFileWrite() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
    nativeOnnxExportedModelFileWrites++;
}

static void storeNativeOnnxExportedModelCacheFileReadError() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
    nativeOnnxExportedModelFileReadErrors++;
}

static void storeNativeOnnxExportedModelCacheFileWriteError() {
    std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
    nativeOnnxExportedModelFileWriteErrors++;
}

static std::vector<uint8_t> createNativeOnnxExportedModelBytes(NativeOnnxApi &nativeOnnxApi, const fs::path &exportLibraryPath, const ModelAssetRecord &modelAsset, uint16_t cpuThreadCount) {
    std::vector<uint8_t> modelBytes = extractNativeOnnxModelAssetBytes(modelAsset);
    if (exportLibraryPath == nativeOnnxApi.libraryPath) {
        return exportNativeOnnxOptimizedModelBytes(nativeOnnxApi, modelAsset, modelBytes, cpuThreadCount, true);
    }
    NativeOnnxApi exportOnnxApi = loadNativeOnnxApi(exportLibraryPath);
    try {
        std::vector<uint8_t> optimizedModelBytes = exportNativeOnnxOptimizedModelBytes(exportOnnxApi, modelAsset, modelBytes, cpuThreadCount, true);
        closeNativeOnnxApi(exportOnnxApi);
        return optimizedModelBytes;
    } catch (...) {
        closeNativeOnnxApi(exportOnnxApi);
        throw;
    }
}

static bool canFallbackToNativeOnnxExportLibrary(NativeOnnxApi &nativeOnnxApi) {
    fs::path exportLibraryPath = getNativeOnnxVvBinExportLibraryPath(nativeOnnxApi);
    return !exportLibraryPath.empty() && exportLibraryPath != nativeOnnxApi.libraryPath && fs::exists(exportLibraryPath);
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxModelAssetViaExportLibraryVvBin(NativeOnnxApi &nativeOnnxApi, const ModelAssetRecord &modelAsset, const std::vector<uint8_t> *modelBytes, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount) {
    fs::path exportLibraryPath = getNativeOnnxVvBinExportLibraryPath(nativeOnnxApi);
    NativeOnnxApi exportOnnxApi = loadNativeOnnxApi(exportLibraryPath);
    try {
        std::vector<uint8_t> fallbackModelBytes = modelBytes ? *modelBytes : extractNativeOnnxModelAssetBytes(modelAsset);
        std::vector<NativeOnnxTraceInput> outputTensors = runNativeOnnxModelBytes(
            exportOnnxApi,
            nullptr,
            fallbackModelBytes,
            inputTensors,
            cpuThreadCount,
            true,
            createNativeOnnxModelAssetSessionCacheKey(exportOnnxApi, nullptr, modelAsset, cpuThreadCount, true));
        closeNativeOnnxApi(exportOnnxApi);
        return outputTensors;
    } catch (...) {
        closeNativeOnnxApi(exportOnnxApi);
        throw;
    }
}

static fs::path getNativeOnnxExportedModelCachePath(NativeOnnxApi &nativeOnnxApi, const ModelAssetRecord &modelAsset, uint16_t cpuThreadCount) {
    fs::path exportLibraryPath = getNativeOnnxVvBinExportLibraryPath(nativeOnnxApi);
    std::string cacheKey = createNativeOnnxExportedModelCacheKey(modelAsset, exportLibraryPath, cpuThreadCount);
    fs::path cachePath = createNativeOnnxExportedModelCachePath(modelAsset, cacheKey);
    {
        std::unique_lock<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
        while (true) {
            auto cacheIterator = nativeOnnxExportedModelCache.find(cacheKey);
            if (cacheIterator != nativeOnnxExportedModelCache.end()) {
                nativeOnnxExportedModelMemoryHits++;
                return cacheIterator->second;
            }
            if (nativeOnnxExportedModelKeysInProgress.find(cacheKey) == nativeOnnxExportedModelKeysInProgress.end()) {
                nativeOnnxExportedModelMemoryMisses++;
                nativeOnnxExportedModelKeysInProgress.insert(cacheKey);
                break;
            }
            nativeOnnxExportedModelCacheCondition.wait(cacheLock);
        }
    }
    try {
        if (fs::exists(cachePath)) {
            storeNativeOnnxExportedModelCacheFileHit();
        } else {
            storeNativeOnnxExportedModelCacheFileMiss();
            std::vector<uint8_t> optimizedModelBytes = createNativeOnnxExportedModelBytes(nativeOnnxApi, exportLibraryPath, modelAsset, cpuThreadCount);
            try {
                writeBinaryFile(cachePath, optimizedModelBytes);
                storeNativeOnnxExportedModelCacheFileWrite();
            } catch (...) {
                storeNativeOnnxExportedModelCacheFileWriteError();
                throw;
            }
        }
        {
            std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
            nativeOnnxExportedModelCache.emplace(cacheKey, cachePath);
            nativeOnnxExportedModelKeysInProgress.erase(cacheKey);
        }
        nativeOnnxExportedModelCacheCondition.notify_all();
        return cachePath;
    } catch (...) {
        {
            std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
            nativeOnnxExportedModelKeysInProgress.erase(cacheKey);
        }
        nativeOnnxExportedModelCacheCondition.notify_all();
        throw;
    }
}

static std::shared_ptr<const std::vector<uint8_t>> getNativeOnnxExportedModelBytes(NativeOnnxApi &nativeOnnxApi, const ModelAssetRecord &modelAsset, uint16_t cpuThreadCount) {
    fs::path cachePath = getNativeOnnxExportedModelCachePath(nativeOnnxApi, modelAsset, cpuThreadCount);
    try {
        std::vector<uint8_t> optimizedModelBytes = readBinaryFile(cachePath);
        if (optimizedModelBytes.empty()) {
            throw std::runtime_error("exported ONNX cache が空です");
        }
        return std::make_shared<const std::vector<uint8_t>>(std::move(optimizedModelBytes));
    } catch (...) {
        storeNativeOnnxExportedModelCacheFileReadError();
        std::error_code removeError;
        fs::remove(cachePath, removeError);
        {
            std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
            fs::path exportLibraryPath = getNativeOnnxVvBinExportLibraryPath(nativeOnnxApi);
            std::string cacheKey = createNativeOnnxExportedModelCacheKey(modelAsset, exportLibraryPath, cpuThreadCount);
            nativeOnnxExportedModelCache.erase(cacheKey);
        }
        fs::path regeneratedCachePath = getNativeOnnxExportedModelCachePath(nativeOnnxApi, modelAsset, cpuThreadCount);
        std::vector<uint8_t> optimizedModelBytes = readBinaryFile(regeneratedCachePath);
        if (optimizedModelBytes.empty()) {
            throw std::runtime_error("exported ONNX cache の再生成結果が空です");
        }
        return std::make_shared<const std::vector<uint8_t>>(std::move(optimizedModelBytes));
    }
}

static NativeOnnxSingTeacherMode getNativeOnnxSingTeacherMode() {
    const char *modeText = std::getenv("LITEVOX_NATIVE_SING_TEACHER_MODE");
    if (!modeText || modeText[0] == '\0' || std::strcmp(modeText, "vv-bin") == 0 || std::strcmp(modeText, "vv_bin") == 0 || std::strcmp(modeText, "stochastic") == 0) {
        return NativeOnnxSingTeacherMode::VvBin;
    }
    if (std::strcmp(modeText, "deterministic") == 0 || std::strcmp(modeText, "seeded_exported_onnx") == 0) {
        return NativeOnnxSingTeacherMode::Deterministic;
    }
    throw std::runtime_error("LITEVOX_NATIVE_SING_TEACHER_MODE が不正です");
}

static float getNativeOnnxDeterministicSingTeacherSeed() {
    const char *seedText = std::getenv("LITEVOX_NATIVE_SING_SEED");
    if (!seedText || seedText[0] == '\0') {
        return 1.0f;
    }
    char *endPointer = nullptr;
    float seedValue = std::strtof(seedText, &endPointer);
    if (!endPointer || endPointer == seedText || *endPointer != '\0' || !std::isfinite(seedValue)) {
        throw std::runtime_error("LITEVOX_NATIVE_SING_SEED が不正です");
    }
    return seedValue;
}

static std::string createNativeOnnxDeterministicSingTeacherCacheKey(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, uint16_t cpuThreadCount, float seedValue) {
    std::ostringstream keyStream;
    keyStream << createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, false) << "\tseeded_sing_teacher\t";
    keyStream << std::setprecision(9) << static_cast<double>(seedValue);
    return keyStream.str();
}

static std::mutex nativeOnnxDeterministicSingTeacherModelCacheMutex;
static std::condition_variable nativeOnnxDeterministicSingTeacherModelCacheCondition;
static std::map<std::string, fs::path> nativeOnnxDeterministicSingTeacherModelCache;
static std::set<std::string> nativeOnnxDeterministicSingTeacherModelKeysInProgress;

void clearNativeOnnxCaches() {
    {
        std::lock_guard<std::mutex> cacheLock(nativeOnnxSessionCacheMutex);
        nativeOnnxSessionCache.clear();
        nativeOnnxSessionKeysInProgress.clear();
        nativeOnnxSessionCacheHits = 0;
        nativeOnnxSessionCacheMisses = 0;
    }
    {
        std::lock_guard<std::mutex> cacheLock(nativeOnnxExportedModelCacheMutex);
        nativeOnnxExportedModelCache.clear();
        nativeOnnxExportedModelKeysInProgress.clear();
        nativeOnnxExportedModelMemoryHits = 0;
        nativeOnnxExportedModelMemoryMisses = 0;
        nativeOnnxExportedModelFileHits = 0;
        nativeOnnxExportedModelFileMisses = 0;
        nativeOnnxExportedModelFileWrites = 0;
        nativeOnnxExportedModelFileReadErrors = 0;
        nativeOnnxExportedModelFileWriteErrors = 0;
    }
    {
        std::lock_guard<std::mutex> cacheLock(nativeOnnxDeterministicSingTeacherModelCacheMutex);
        nativeOnnxDeterministicSingTeacherModelCache.clear();
        nativeOnnxDeterministicSingTeacherModelKeysInProgress.clear();
    }
}

static fs::path createNativeOnnxDeterministicSingTeacherModelPath(const ModelAssetRecord &modelAsset, const std::string &cacheKey) {
    std::string cacheDigest = createSha256Hex(reinterpret_cast<const uint8_t *>(cacheKey.data()), cacheKey.size());
    return modelAsset.archivePath.parent_path() / ".litevox-exported-onnx-cache" / (createNativeOnnxExportedModelCacheBaseName(modelAsset) + "-seeded-" + cacheDigest.substr(0, 16) + ".onnx");
}

static fs::path getNativeOnnxDeterministicSingTeacherModelPath(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, uint16_t cpuThreadCount, float seedValue) {
    std::string cacheKey = createNativeOnnxDeterministicSingTeacherCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, seedValue);
    fs::path cachePath = createNativeOnnxDeterministicSingTeacherModelPath(modelAsset, cacheKey);
    {
        std::unique_lock<std::mutex> cacheLock(nativeOnnxDeterministicSingTeacherModelCacheMutex);
        while (true) {
            auto cacheIterator = nativeOnnxDeterministicSingTeacherModelCache.find(cacheKey);
            if (cacheIterator != nativeOnnxDeterministicSingTeacherModelCache.end()) {
                return cacheIterator->second;
            }
            if (nativeOnnxDeterministicSingTeacherModelKeysInProgress.find(cacheKey) == nativeOnnxDeterministicSingTeacherModelKeysInProgress.end()) {
                nativeOnnxDeterministicSingTeacherModelKeysInProgress.insert(cacheKey);
                break;
            }
            nativeOnnxDeterministicSingTeacherModelCacheCondition.wait(cacheLock);
        }
    }
    try {
        if (!fs::exists(cachePath)) {
            std::shared_ptr<const std::vector<uint8_t>> optimizedModelBytes = getNativeOnnxExportedModelBytes(nativeOnnxApi, modelAsset, cpuThreadCount);
            size_t rewrittenNodeCount = 0;
            std::vector<uint8_t> seededModelBytes = rewriteNativeOnnxModelRandomSeed(optimizedModelBytes->data(), optimizedModelBytes->size(), seedValue, rewrittenNodeCount);
            writeBinaryFile(cachePath, seededModelBytes);
        }
        {
            std::lock_guard<std::mutex> cacheLock(nativeOnnxDeterministicSingTeacherModelCacheMutex);
            nativeOnnxDeterministicSingTeacherModelCache.emplace(cacheKey, cachePath);
            nativeOnnxDeterministicSingTeacherModelKeysInProgress.erase(cacheKey);
        }
        nativeOnnxDeterministicSingTeacherModelCacheCondition.notify_all();
        return cachePath;
    } catch (...) {
        {
            std::lock_guard<std::mutex> cacheLock(nativeOnnxDeterministicSingTeacherModelCacheMutex);
            nativeOnnxDeterministicSingTeacherModelKeysInProgress.erase(cacheKey);
        }
        nativeOnnxDeterministicSingTeacherModelCacheCondition.notify_all();
        throw;
    }
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxDeterministicSingTeacherModelAssetBytes(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount) {
    float seedValue = getNativeOnnxDeterministicSingTeacherSeed();
    fs::path seededModelPath = getNativeOnnxDeterministicSingTeacherModelPath(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, seedValue);
    std::vector<NativeOnnxTraceInput> outputTensors = runNativeOnnxModelPath(nativeOnnxApi, runtimeState, seededModelPath, inputTensors, cpuThreadCount, false);
    writeNativeOnnxTensorTrace(modelAsset, inputTensors, outputTensors);
    return outputTensors;
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxSingTeacherModelAssetBytes(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    if (getNativeOnnxSingTeacherMode() == NativeOnnxSingTeacherMode::Deterministic) {
        return runNativeOnnxDeterministicSingTeacherModelAssetBytes(nativeOnnxApi, runtimeState, modelAsset, inputTensors, cpuThreadCount);
    }
    std::vector<NativeOnnxTraceInput> outputTensors;
    if (shouldUseVvBinConfig) {
        std::vector<uint8_t> modelBytes = extractNativeOnnxModelAssetBytes(modelAsset);
        outputTensors = runNativeOnnxModelBytes(
            nativeOnnxApi,
            runtimeState,
            modelBytes,
            inputTensors,
            cpuThreadCount,
            true,
            createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, true));
    } else {
        try {
            fs::path exportedModelPath = getNativeOnnxExportedModelCachePath(nativeOnnxApi, modelAsset, cpuThreadCount);
            outputTensors = runNativeOnnxModelPath(
                nativeOnnxApi,
                runtimeState,
                exportedModelPath,
                inputTensors,
                cpuThreadCount,
                false,
                createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, false));
        } catch (...) {
            if (!canFallbackToNativeOnnxExportLibrary(nativeOnnxApi)) {
                throw;
            }
            outputTensors = runNativeOnnxModelAssetViaExportLibraryVvBin(nativeOnnxApi, modelAsset, nullptr, inputTensors, cpuThreadCount);
        }
    }
    writeNativeOnnxTensorTrace(modelAsset, inputTensors, outputTensors);
    return outputTensors;
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxModelAssetBytes(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    if (shouldUseVvBinConfig) {
        std::vector<uint8_t> modelBytes = extractNativeOnnxModelAssetBytes(modelAsset);
        std::vector<NativeOnnxTraceInput> outputTensors = runNativeOnnxModelBytes(
            nativeOnnxApi,
            runtimeState,
            modelBytes,
            inputTensors,
            cpuThreadCount,
            true,
            createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, true));
        writeNativeOnnxTensorTrace(modelAsset, inputTensors, outputTensors);
        return outputTensors;
    }
    std::vector<NativeOnnxTraceInput> outputTensors;
    try {
        fs::path exportedModelPath = getNativeOnnxExportedModelCachePath(nativeOnnxApi, modelAsset, cpuThreadCount);
        outputTensors = runNativeOnnxModelPath(
            nativeOnnxApi,
            runtimeState,
            exportedModelPath,
            inputTensors,
            cpuThreadCount,
            false,
            createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, false));
    } catch (...) {
        if (!canFallbackToNativeOnnxExportLibrary(nativeOnnxApi)) {
            throw;
        }
        outputTensors = runNativeOnnxModelAssetViaExportLibraryVvBin(nativeOnnxApi, modelAsset, nullptr, inputTensors, cpuThreadCount);
    }
    writeNativeOnnxTensorTrace(modelAsset, inputTensors, outputTensors);
    return outputTensors;
}

static std::vector<NativeOnnxTraceInput> runNativeOnnxModelAssetBytes(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const ModelAssetRecord &modelAsset, const std::vector<uint8_t> &modelBytes, const std::vector<NativeOnnxTraceInput> &inputTensors, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::vector<NativeOnnxTraceInput> outputTensors;
    if (shouldUseVvBinConfig) {
        outputTensors = runNativeOnnxModelBytes(nativeOnnxApi, runtimeState, modelBytes, inputTensors, cpuThreadCount, true, createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, true));
    } else {
        try {
            fs::path exportedModelPath = getNativeOnnxExportedModelCachePath(nativeOnnxApi, modelAsset, cpuThreadCount);
            outputTensors = runNativeOnnxModelPath(nativeOnnxApi, runtimeState, exportedModelPath, inputTensors, cpuThreadCount, false, createNativeOnnxModelAssetSessionCacheKey(nativeOnnxApi, runtimeState, modelAsset, cpuThreadCount, false));
        } catch (...) {
            if (!canFallbackToNativeOnnxExportLibrary(nativeOnnxApi)) {
                throw;
            }
            outputTensors = runNativeOnnxModelAssetViaExportLibraryVvBin(nativeOnnxApi, modelAsset, &modelBytes, inputTensors, cpuThreadCount);
        }
    }
    writeNativeOnnxTensorTrace(modelAsset, inputTensors, outputTensors);
    return outputTensors;
}

static std::string formatNativeOnnxFloat(float value) {
    if (std::fabs(value - 0.0f) < 0.0000005f) {
        return "0.0";
    }
    char valueBuffer[64];
    auto conversionResult = std::to_chars(valueBuffer, valueBuffer + sizeof(valueBuffer), static_cast<double>(value));
    if (conversionResult.ec == std::errc()) {
        return std::string(valueBuffer, conversionResult.ptr);
    }
    std::ostringstream valueStream;
    valueStream << std::setprecision(17) << static_cast<double>(value);
    return valueStream.str();
}

static std::string formatNativeOnnxSettingFloat(float value) {
    if (std::fabs(value - 0.0f) < 0.0000005f) {
        return "0.0";
    }
    if (std::fabs(value - 0.1f) < 0.0000005f) {
        return "0.1";
    }
    if (std::fabs(value - 1.0f) < 0.0000005f) {
        return "1.0";
    }
    return formatNativeOnnxFloat(value);
}

static std::string createNativeOnnxMoraJson(const NativeOnnxMora &mora) {
    std::string jsonText = "{\"text\":" + quoteJsonString(mora.text) + ",";
    jsonText += "\"consonant\":";
    jsonText += mora.hasConsonant ? quoteJsonString(mora.consonant) : "null";
    jsonText += ",\"consonant_length\":";
    jsonText += mora.hasConsonant ? formatNativeOnnxFloat(mora.consonantLength) : "null";
    jsonText += ",\"vowel\":" + quoteJsonString(mora.vowel);
    jsonText += ",\"vowel_length\":" + formatNativeOnnxFloat(mora.vowelLength);
    jsonText += ",\"pitch\":" + formatNativeOnnxFloat(mora.pitch);
    jsonText += "}";
    return jsonText;
}

static std::string createNativeOnnxAccentPhrasesJson(const std::vector<NativeOnnxAccentPhrase> &accentPhrases) {
    std::string jsonText = "[";
    for (size_t phraseIndex = 0; phraseIndex < accentPhrases.size(); phraseIndex++) {
        const NativeOnnxAccentPhrase &accentPhrase = accentPhrases[phraseIndex];
        if (phraseIndex > 0) {
            jsonText += ",";
        }
        jsonText += "{\"moras\":[";
        for (size_t moraIndex = 0; moraIndex < accentPhrase.moras.size(); moraIndex++) {
            if (moraIndex > 0) {
                jsonText += ",";
            }
            jsonText += createNativeOnnxMoraJson(accentPhrase.moras[moraIndex]);
        }
        jsonText += "],\"accent\":" + std::to_string(accentPhrase.accent);
        jsonText += ",\"pause_mora\":";
        jsonText += accentPhrase.hasPauseMora ? createNativeOnnxMoraJson(accentPhrase.pauseMora) : "null";
        jsonText += ",\"is_interrogative\":";
        jsonText += accentPhrase.isInterrogative ? "true" : "false";
        jsonText += "}";
    }
    jsonText += "]";
    return jsonText;
}

static std::string createNativeOnnxAudioQueryTextFromAccentPhrasesJson(const std::string &accentPhrasesJson) {
    std::string jsonText = "{\"accent_phrases\":" + accentPhrasesJson;
    jsonText += ",\"speedScale\":1.0";
    jsonText += ",\"pitchScale\":0.0";
    jsonText += ",\"intonationScale\":1.0";
    jsonText += ",\"volumeScale\":1.0";
    jsonText += ",\"prePhonemeLength\":0.1";
    jsonText += ",\"postPhonemeLength\":0.1";
    jsonText += ",\"pauseLength\":null";
    jsonText += ",\"pauseLengthScale\":1.0";
    jsonText += ",\"outputSamplingRate\":24000";
    jsonText += ",\"outputStereo\":false";
    jsonText += ",\"kana\":\"\"";
    jsonText += "}";
    return jsonText;
}

static std::string createNativeOnnxAudioQueryTextWithAccentPhrases(const std::string &audioQueryText, const std::string &accentPhrasesJson) {
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    std::string kana = extractJsonStringField(audioQueryText, "kana");
    std::string jsonText = "{\"accent_phrases\":" + accentPhrasesJson;
    jsonText += ",\"speedScale\":" + formatNativeOnnxSettingFloat(audioQuerySettings.speedScale);
    jsonText += ",\"pitchScale\":" + formatNativeOnnxSettingFloat(audioQuerySettings.pitchScale);
    jsonText += ",\"intonationScale\":" + formatNativeOnnxSettingFloat(audioQuerySettings.intonationScale);
    jsonText += ",\"volumeScale\":" + formatNativeOnnxSettingFloat(audioQuerySettings.volumeScale);
    jsonText += ",\"prePhonemeLength\":" + formatNativeOnnxSettingFloat(audioQuerySettings.prePhonemeLength);
    jsonText += ",\"postPhonemeLength\":" + formatNativeOnnxSettingFloat(audioQuerySettings.postPhonemeLength);
    jsonText += ",\"pauseLength\":null";
    jsonText += ",\"pauseLengthScale\":1.0";
    jsonText += ",\"outputSamplingRate\":" + std::to_string(audioQuerySettings.outputSamplingRate);
    jsonText += ",\"outputStereo\":";
    jsonText += audioQuerySettings.outputStereo ? "true" : "false";
    jsonText += ",\"kana\":" + quoteJsonString(kana);
    jsonText += "}";
    return jsonText;
}

static bool hasNativeOnnxCompleteMoraData(const std::vector<NativeOnnxAccentPhrase> &accentPhrases) {
    for (const NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        for (const NativeOnnxMora &mora : accentPhrase.moras) {
            if (!mora.hasVowelLength || !mora.hasPitch || mora.vowelLength <= 0.0f) {
                return false;
            }
            if (mora.hasConsonant && (!mora.hasConsonantLength || mora.consonantLength <= 0.0f)) {
                return false;
            }
        }
        if (accentPhrase.hasPauseMora && (!accentPhrase.pauseMora.hasVowelLength || !accentPhrase.pauseMora.hasPitch || accentPhrase.pauseMora.vowelLength <= 0.0f)) {
            return false;
        }
    }
    return true;
}

static void ensureNativeOnnxMinimumPhonemeLengths(std::vector<float> &phonemeLengthValues) {
    static constexpr float minimumPhonemeLength = 0.01f;
    for (float &phonemeLength : phonemeLengthValues) {
        if (phonemeLength < minimumPhonemeLength) {
            phonemeLength = minimumPhonemeLength;
        }
    }
}

static std::vector<float> createNativeOnnxPhonemeLengthValuesFromAccentPhrases(const NativeOnnxAudioQuerySettings &audioQuerySettings, const std::vector<NativeOnnxAccentPhrase> &accentPhrases) {
    std::vector<float> phonemeLengthValues;
    phonemeLengthValues.push_back(audioQuerySettings.prePhonemeLength);
    for (const NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        for (const NativeOnnxMora &mora : accentPhrase.moras) {
            if (mora.hasConsonant) {
                phonemeLengthValues.push_back(mora.consonantLength);
            }
            phonemeLengthValues.push_back(mora.vowelLength);
        }
        if (accentPhrase.hasPauseMora) {
            if (accentPhrase.pauseMora.hasConsonant) {
                phonemeLengthValues.push_back(accentPhrase.pauseMora.consonantLength);
            }
            phonemeLengthValues.push_back(accentPhrase.pauseMora.vowelLength);
        }
    }
    phonemeLengthValues.push_back(audioQuerySettings.postPhonemeLength);
    return phonemeLengthValues;
}

static std::vector<float> createNativeOnnxF0ValuesFromAccentPhrases(const std::vector<NativeOnnxAccentPhrase> &accentPhrases) {
    std::vector<float> f0Values;
    f0Values.push_back(0.0f);
    for (const NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        for (const NativeOnnxMora &mora : accentPhrase.moras) {
            f0Values.push_back(mora.pitch);
        }
        if (accentPhrase.hasPauseMora) {
            f0Values.push_back(accentPhrase.pauseMora.pitch);
        }
    }
    f0Values.push_back(0.0f);
    return f0Values;
}

static std::vector<NativeOnnxTraceInput> createNativeOnnxDecoderInputsFromAudioQuery(const std::vector<NativeOnnxTraceInput> &frontendInputs, const std::string &audioQueryText, const NativeOnnxAudioQuerySettings &audioQuerySettings) {
    std::vector<NativeOnnxAccentPhrase> accentPhrases = parseNativeOnnxAccentPhrases(audioQueryText);
    std::vector<float> phonemeLengthValues = createNativeOnnxPhonemeLengthValuesFromAccentPhrases(audioQuerySettings, accentPhrases);
    std::vector<float> f0Values = createNativeOnnxF0ValuesFromAccentPhrases(accentPhrases);
    NativeOnnxTraceInput durationTensor;
    durationTensor.name = "phoneme_length";
    durationTensor.elementType = 1;
    durationTensor.dimensions = {static_cast<int64_t>(phonemeLengthValues.size())};
    durationTensor.bytes = createNativeOnnxTensorBytes(phonemeLengthValues);
    NativeOnnxTraceInput intonationTensor;
    intonationTensor.name = "f0_list";
    intonationTensor.elementType = 1;
    intonationTensor.dimensions = {static_cast<int64_t>(f0Values.size())};
    intonationTensor.bytes = createNativeOnnxTensorBytes(f0Values);
    return createNativeOnnxDecoderInputs(frontendInputs, {durationTensor}, {intonationTensor}, audioQuerySettings, false);
}

static std::vector<float> runNativeOnnxDurationValues(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::vector<NativeOnnxTraceInput> &frontendInputs, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    const ModelAssetRecord &durationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pd.bin");
    std::vector<NativeOnnxTraceInput> durationOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, runtimeState, durationAsset, frontendInputs, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<float> phonemeLengthValues = readNativeOnnxTensorValues<float>(requireNativeOnnxTensor(durationOutputs, "phoneme_length"), 1);
    ensureNativeOnnxMinimumPhonemeLengths(phonemeLengthValues);
    return phonemeLengthValues;
}

static std::vector<float> runNativeOnnxF0Values(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::vector<NativeOnnxTraceInput> &frontendInputs, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    const ModelAssetRecord &intonationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pi.bin");
    std::vector<NativeOnnxTraceInput> intonationOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, runtimeState, intonationAsset, frontendInputs, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<float> f0Values = readNativeOnnxTensorValues<float>(requireNativeOnnxTensor(intonationOutputs, "f0_list"), 1);
    const NativeOnnxTraceInput &vowelPhonemeListTensor = requireNativeOnnxTensor(frontendInputs, "vowel_phoneme_list");
    std::vector<int64_t> vowelValues = readNativeOnnxTensorValues<int64_t>(vowelPhonemeListTensor, 7);
    if (f0Values.size() != vowelValues.size()) {
        throw std::runtime_error("f0_list の長さが一致しません");
    }
    for (size_t vowelIndex = 0; vowelIndex < vowelValues.size(); vowelIndex++) {
        if (isNativeOnnxUnvoicedVowel(vowelValues[vowelIndex])) {
            f0Values[vowelIndex] = 0.0f;
        }
    }
    return f0Values;
}

static void applyNativeOnnxPhonemeLengthsToAccentPhrases(std::vector<NativeOnnxAccentPhrase> &accentPhrases, const std::vector<NativeOnnxTraceInput> &frontendInputs, const std::vector<float> &phonemeLengthValues) {
    const NativeOnnxTraceInput &phonemeListTensor = requireNativeOnnxTensor(frontendInputs, "phoneme_list");
    const NativeOnnxTraceInput &vowelPhonemeListTensor = requireNativeOnnxTensor(frontendInputs, "vowel_phoneme_list");
    std::vector<int64_t> phonemeValues = readNativeOnnxTensorValues<int64_t>(phonemeListTensor, 7);
    std::vector<int64_t> vowelValues = readNativeOnnxTensorValues<int64_t>(vowelPhonemeListTensor, 7);
    std::vector<int64_t> vowelIndexes = findNativeOnnxVowelIndexes(phonemeValues, vowelValues);
    if (phonemeLengthValues.size() != phonemeValues.size()) {
        throw std::runtime_error("phoneme_length の長さが一致しません");
    }
    size_t moraIndex = 0;
    auto applyLength = [&](NativeOnnxMora &mora) {
        size_t vowelIndex = static_cast<size_t>(vowelIndexes.at(moraIndex + 1));
        if (mora.hasConsonant) {
            mora.consonantLength = phonemeLengthValues.at(vowelIndex - 1);
            mora.hasConsonantLength = true;
        } else {
            mora.consonantLength = 0.0f;
            mora.hasConsonantLength = false;
        }
        mora.vowelLength = phonemeLengthValues.at(vowelIndex);
        mora.hasVowelLength = true;
        moraIndex++;
    };
    for (NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        for (NativeOnnxMora &mora : accentPhrase.moras) {
            applyLength(mora);
        }
        if (accentPhrase.hasPauseMora) {
            applyLength(accentPhrase.pauseMora);
        }
    }
}

static void applyNativeOnnxF0ToAccentPhrases(std::vector<NativeOnnxAccentPhrase> &accentPhrases, const std::vector<float> &f0Values) {
    size_t moraIndex = 0;
    auto applyPitch = [&](NativeOnnxMora &mora) {
        mora.pitch = f0Values.at(moraIndex + 1);
        mora.hasPitch = true;
        moraIndex++;
    };
    for (NativeOnnxAccentPhrase &accentPhrase : accentPhrases) {
        for (NativeOnnxMora &mora : accentPhrase.moras) {
            applyPitch(mora);
        }
        if (accentPhrase.hasPauseMora) {
            applyPitch(accentPhrase.pauseMora);
        }
    }
}

static std::string replaceNativeOnnxMoraDataWithApi(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldReplaceLength, bool shouldReplacePitch, bool shouldUseVvBinConfig) {
    std::string audioQueryText = createNativeOnnxAudioQueryTextFromAccentPhrasesJson(accentPhrasesJson);
    std::vector<NativeOnnxAccentPhrase> accentPhrases = parseNativeOnnxAccentPhrases(audioQueryText);
    const ModelAssetRecord &durationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pd.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(durationAsset, "talk", styleId);
    std::vector<NativeOnnxTraceInput> frontendInputs = createNativeOnnxFrontendInputs(audioQueryText, innerVoiceId);
    if (shouldReplaceLength) {
        applyNativeOnnxPhonemeLengthsToAccentPhrases(accentPhrases, frontendInputs, runNativeOnnxDurationValues(nativeOnnxApi, runtimeState, modelAssets, frontendInputs, cpuThreadCount, shouldUseVvBinConfig));
    }
    if (shouldReplacePitch) {
        applyNativeOnnxF0ToAccentPhrases(accentPhrases, runNativeOnnxF0Values(nativeOnnxApi, runtimeState, modelAssets, frontendInputs, cpuThreadCount, shouldUseVvBinConfig));
    }
    return createNativeOnnxAccentPhrasesJson(accentPhrases);
}

static NativeOnnxTraceInput createNativeOnnxFloatTensor(const std::string &tensorName, const std::vector<int64_t> &dimensions, const std::vector<float> &values) {
    NativeOnnxTraceInput tensor;
    tensor.name = tensorName;
    tensor.elementType = 1;
    tensor.dimensions = dimensions;
    tensor.bytes = createNativeOnnxTensorBytes(values);
    return tensor;
}

static std::vector<NativeOnnxTraceInput> createNativeOnnxModelAssetDecoderInputs(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, const NativeOnnxAudioQuerySettings &audioQuerySettings, bool shouldUseVvBinConfig) {
    const ModelAssetRecord &durationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pd.bin");
    const ModelAssetRecord &intonationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pi.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(durationAsset, "talk", styleId);
    std::vector<NativeOnnxTraceInput> frontendInputs = createNativeOnnxFrontendInputs(audioQueryText, innerVoiceId);
    if (hasNativeOnnxCompleteMoraData(parseNativeOnnxAccentPhrases(audioQueryText))) {
        return createNativeOnnxDecoderInputsFromAudioQuery(frontendInputs, audioQueryText, audioQuerySettings);
    }
    std::vector<NativeOnnxTraceInput> durationOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, runtimeState, durationAsset, frontendInputs, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<NativeOnnxTraceInput> intonationOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, runtimeState, intonationAsset, frontendInputs, cpuThreadCount, shouldUseVvBinConfig);
    return createNativeOnnxDecoderInputs(frontendInputs, durationOutputs, intonationOutputs, audioQuerySettings, true);
}

static NativeOnnxTraceInput runNativeOnnxModelAssetChainWave(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    const ModelAssetRecord &decodeAsset = requireNativeOnnxModelAsset(modelAssets, "models/d.bin");
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    std::vector<NativeOnnxTraceInput> decoderInputs = createNativeOnnxModelAssetDecoderInputs(nativeOnnxApi, runtimeState, modelAssets, audioQueryText, styleId, cpuThreadCount, audioQuerySettings, shouldUseVvBinConfig);
    std::vector<NativeOnnxTraceInput> decodeOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, runtimeState, decodeAsset, decoderInputs, cpuThreadCount, shouldUseVvBinConfig);
    return requireNativeOnnxTensor(decodeOutputs, "wave");
}

static std::vector<float> createNativeOnnxWaveValuesWithoutDecoderFramePadding(const NativeOnnxTraceInput &waveTensor, size_t frontCropFrames, size_t backCropFrames) {
    std::vector<float> waveValues = readNativeOnnxTensorValues<float>(waveTensor, 1);
    size_t frontCropSamples = frontCropFrames * nativeOnnxSamplesPerFrame;
    size_t backCropSamples = backCropFrames * nativeOnnxSamplesPerFrame;
    if (waveValues.size() < frontCropSamples + backCropSamples) {
        throw std::runtime_error("decoder wave crop が不正です");
    }
    waveValues = std::vector<float>(waveValues.begin() + static_cast<std::vector<float>::difference_type>(frontCropSamples), waveValues.end() - static_cast<std::vector<float>::difference_type>(backCropSamples));
    return waveValues;
}

static std::vector<float> createNativeOnnxWaveValuesWithoutDecoderPadding(const NativeOnnxTraceInput &waveTensor) {
    return createNativeOnnxWaveValuesWithoutDecoderFramePadding(waveTensor, nativeOnnxDecoderPaddingFrames, nativeOnnxDecoderPaddingFrames);
}

static uint32_t calculateNativeOnnxOutputRepeatCount(const NativeOnnxAudioQuerySettings &audioQuerySettings) {
    uint16_t channels = audioQuerySettings.outputStereo ? 2 : 1;
    uint32_t repeatCount = (audioQuerySettings.outputSamplingRate / nativeOnnxDefaultSamplingRate) * static_cast<uint32_t>(channels);
    if (repeatCount == 0) {
        throw std::runtime_error("native synthesis の outputSamplingRate が不正です");
    }
    return repeatCount;
}

static std::vector<uint8_t> createNativeOnnxPcmBytes(const std::vector<float> &waveValues, const NativeOnnxAudioQuerySettings &audioQuerySettings) {
    uint32_t repeatCount = calculateNativeOnnxOutputRepeatCount(audioQuerySettings);
    std::vector<uint8_t> pcmBytes;
    pcmBytes.reserve(waveValues.size() * repeatCount * sizeof(int16_t));
    for (float waveValue : waveValues) {
        float scaledValue = waveValue * audioQuerySettings.volumeScale;
        if (scaledValue > 1.0f) {
            scaledValue = 1.0f;
        } else if (scaledValue < -1.0f) {
            scaledValue = -1.0f;
        }
        int16_t sampleValue = static_cast<int16_t>(scaledValue * 32767.0f);
        for (uint32_t repeatIndex = 0; repeatIndex < repeatCount; repeatIndex++) {
            pcmBytes.push_back(static_cast<uint8_t>(sampleValue & 0xff));
            pcmBytes.push_back(static_cast<uint8_t>((static_cast<uint16_t>(sampleValue) >> 8) & 0xff));
        }
    }
    return pcmBytes;
}

static std::vector<uint8_t> createNativeOnnxWavBytes(const NativeOnnxTraceInput &waveTensor, const NativeOnnxAudioQuerySettings &audioQuerySettings) {
    std::vector<float> waveValues = createNativeOnnxWaveValuesWithoutDecoderPadding(waveTensor);
    std::vector<uint8_t> pcmBytes = createNativeOnnxPcmBytes(waveValues, audioQuerySettings);
    uint16_t channels = audioQuerySettings.outputStereo ? 2 : 1;
    std::vector<uint8_t> wavBytes = createPcmWaveHeader(audioQuerySettings.outputSamplingRate, channels, 16, pcmBytes.size());
    wavBytes.insert(wavBytes.end(), pcmBytes.begin(), pcmBytes.end());
    return wavBytes;
}

static size_t getNativeOnnxPaddedDecoderFrameCount(const NativeOnnxTraceInput &f0Tensor) {
    std::vector<float> paddedF0Values = readNativeOnnxTensorValues<float>(f0Tensor, 1);
    if (!f0Tensor.dimensions.empty() && f0Tensor.dimensions[0] >= 0 && static_cast<size_t>(f0Tensor.dimensions[0]) != paddedF0Values.size()) {
        throw std::runtime_error("decoder f0 frame 数が一致しません");
    }
    return paddedF0Values.size();
}

static std::vector<float> sliceNativeOnnxFrameValues(const std::vector<float> &frameValues, size_t frameWidth, size_t startFrame, size_t endFrame) {
    if (frameWidth == 0 || startFrame > endFrame || endFrame * frameWidth > frameValues.size()) {
        throw std::runtime_error("decoder chunk frame が不正です");
    }
    auto sliceStartIterator = frameValues.begin() + static_cast<std::vector<float>::difference_type>(startFrame * frameWidth);
    auto sliceEndIterator = frameValues.begin() + static_cast<std::vector<float>::difference_type>(endFrame * frameWidth);
    return std::vector<float>(sliceStartIterator, sliceEndIterator);
}

static NativeOnnxDecoderChunkInputSet createNativeOnnxDecoderChunkInputs(const std::vector<NativeOnnxTraceInput> &decoderInputs, size_t coreStartFrame, size_t coreEndFrame, size_t contextFrames) {
    const NativeOnnxTraceInput &f0Tensor = requireNativeOnnxTensor(decoderInputs, "f0");
    const NativeOnnxTraceInput &phonemeTensor = requireNativeOnnxTensor(decoderInputs, "phoneme");
    NativeOnnxTraceInput speakerIdTensor = requireNativeOnnxTensor(decoderInputs, "speaker_id");
    std::vector<float> paddedF0Values = readNativeOnnxTensorValues<float>(f0Tensor, 1);
    std::vector<float> paddedPhonemeValues = readNativeOnnxTensorValues<float>(phonemeTensor, 1);
    size_t paddedFrameCount = paddedF0Values.size();
    size_t paddingFrameCount = nativeOnnxDecoderPaddingFrames * 2;
    if (paddedFrameCount < paddingFrameCount || paddedPhonemeValues.size() != paddedFrameCount * static_cast<size_t>(nativeOnnxPhonemeSize)) {
        throw std::runtime_error("decoder 入力 frame 数が不正です");
    }
    size_t coreFrameCount = paddedFrameCount - paddingFrameCount;
    if (coreStartFrame > coreEndFrame || coreEndFrame > coreFrameCount) {
        throw std::runtime_error("decoder chunk 範囲が不正です");
    }
    size_t targetStartFrame = nativeOnnxDecoderPaddingFrames + coreStartFrame;
    size_t targetEndFrame = nativeOnnxDecoderPaddingFrames + coreEndFrame;
    size_t sliceStartFrame = coreStartFrame > contextFrames ? coreStartFrame - contextFrames : 0;
    size_t sliceEndFrame = std::min(paddedFrameCount, coreEndFrame + paddingFrameCount + contextFrames);
    size_t sliceFrameCount = sliceEndFrame - sliceStartFrame;
    std::vector<float> f0Values = sliceNativeOnnxFrameValues(paddedF0Values, 1, sliceStartFrame, sliceEndFrame);
    std::vector<float> phonemeValues = sliceNativeOnnxFrameValues(paddedPhonemeValues, static_cast<size_t>(nativeOnnxPhonemeSize), sliceStartFrame, sliceEndFrame);
    NativeOnnxDecoderChunkInputSet inputSet;
    inputSet.tensors = {
        createNativeOnnxFloatTensor("f0", {static_cast<int64_t>(sliceFrameCount), 1}, f0Values),
        createNativeOnnxFloatTensor("phoneme", {static_cast<int64_t>(sliceFrameCount), nativeOnnxPhonemeSize}, phonemeValues),
        speakerIdTensor,
    };
    inputSet.frontCropFrames = targetStartFrame - sliceStartFrame;
    inputSet.backCropFrames = sliceEndFrame - targetEndFrame;
    return inputSet;
}

static NativeOnnxPcmStreamInfo createNativeOnnxPcmStreamInfo(const NativeOnnxAudioQuerySettings &audioQuerySettings, size_t coreFrameCount) {
    NativeOnnxPcmStreamInfo streamInfo;
    streamInfo.sampleRate = audioQuerySettings.outputSamplingRate;
    streamInfo.channels = audioQuerySettings.outputStereo ? 2 : 1;
    streamInfo.bitsPerSample = 16;
    uint32_t repeatCount = calculateNativeOnnxOutputRepeatCount(audioQuerySettings);
    streamInfo.pcmBytes = static_cast<uintptr_t>(coreFrameCount * nativeOnnxSamplesPerFrame * repeatCount * sizeof(int16_t));
    return streamInfo;
}

std::string createNativeOnnxVvmChainText(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &inputDirectory, const fs::path &audioQueryPath, uint32_t styleId, uint16_t cpuThreadCount) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        const ModelAssetRecord &decodeAsset = requireNativeOnnxModelAsset(modelAssets, "models/d.bin");
        const ModelAssetRecord &durationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pd.bin");
        const ModelAssetRecord &intonationAsset = requireNativeOnnxModelAsset(modelAssets, "models/pi.bin");
        std::vector<NativeOnnxTraceInput> traceInputs = loadNativeOnnxTraceInputs(inputDirectory);
        std::vector<NativeOnnxTraceInput> traceOutputs = loadNativeOnnxTraceOutputs(inputDirectory);
        fs::path resolvedAudioQueryPath = resolveNativeOnnxAudioQueryPath(inputDirectory, audioQueryPath);
        std::string audioQueryText = readNativeOnnxTextFile(resolvedAudioQueryPath);
        NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
        int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(durationAsset, "talk", styleId);
        std::vector<NativeOnnxTraceInput> frontendInputs = createNativeOnnxFrontendInputs(audioQueryText, innerVoiceId);
        std::vector<uint8_t> durationBytes = extractNativeOnnxModelAssetBytes(durationAsset);
        std::vector<uint8_t> intonationBytes = extractNativeOnnxModelAssetBytes(intonationAsset);
        std::vector<uint8_t> decodeBytes = extractNativeOnnxModelAssetBytes(decodeAsset);
        std::vector<NativeOnnxTraceInput> durationOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, nullptr, durationAsset, durationBytes, frontendInputs, cpuThreadCount, true);
        const NativeOnnxTraceInput &durationTensor = requireNativeOnnxTensor(durationOutputs, "phoneme_length");
        std::vector<NativeOnnxTraceInput> intonationOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, nullptr, intonationAsset, intonationBytes, frontendInputs, cpuThreadCount, true);
        const NativeOnnxTraceInput &intonationTensor = requireNativeOnnxTensor(intonationOutputs, "f0_list");
        std::vector<NativeOnnxTraceInput> decoderInputs = createNativeOnnxDecoderInputs(frontendInputs, durationOutputs, intonationOutputs, audioQuerySettings, true);
        const NativeOnnxTraceInput &generatedF0Tensor = requireNativeOnnxTensor(decoderInputs, "f0");
        const NativeOnnxTraceInput &generatedPhonemeTensor = requireNativeOnnxTensor(decoderInputs, "phoneme");
        std::vector<NativeOnnxTraceInput> decodeOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, nullptr, decodeAsset, decodeBytes, decoderInputs, cpuThreadCount, true);
        const NativeOnnxTraceInput &waveTensor = requireNativeOnnxTensor(decodeOutputs, "wave");
        std::ostringstream chainStream;
        chainStream << "field\tvalue\n";
        chainStream << "onnxruntime\t" << onnxruntimeLibraryPath.string() << "\n";
        chainStream << "api_version\t" << ortApiVersion << "\n";
        chainStream << "ort_version\t" << getNativeOnnxVersion(nativeOnnxApi) << "\n";
        chainStream << "cpu_threads\t" << cpuThreadCount << "\n";
        chainStream << "audio_query\t" << resolvedAudioQueryPath.string() << "\n";
        chainStream << "style_id\t" << styleId << "\n";
        if (!inputDirectory.empty()) {
            chainStream << "trace_inputs\t" << inputDirectory.string() << "\n";
        }
        chainStream << "trace_input_count\t" << traceInputs.size() << "\n";
        chainStream << "trace_output_count\t" << traceOutputs.size() << "\n";
        chainStream << "speed_scale\t" << audioQuerySettings.speedScale << "\n";
        chainStream << "pre_phoneme_length\t" << audioQuerySettings.prePhonemeLength << "\n";
        chainStream << "post_phoneme_length\t" << audioQuerySettings.postPhonemeLength << "\n";
        chainStream << "duration_asset\t" << durationAsset.archivePath.filename().string() << ":" << durationAsset.entryName << "\n";
        chainStream << "intonation_asset\t" << intonationAsset.archivePath.filename().string() << ":" << intonationAsset.entryName << "\n";
        chainStream << "decode_asset\t" << decodeAsset.archivePath.filename().string() << ":" << decodeAsset.entryName << "\n";
        for (const NativeOnnxTraceInput &frontendInput : frontendInputs) {
            chainStream << "frontend_" << frontendInput.name << "\t" << compareNativeOnnxTensors(frontendInput, findNativeOnnxTraceTensor(traceInputs, frontendInput.name)) << "\n";
        }
        chainStream << "duration_run\tok\n";
        chainStream << "duration_output\t" << compareNativeOnnxTensors(durationTensor, findNativeOnnxTraceTensor(traceOutputs, "phoneme_length")) << "\n";
        chainStream << "intonation_run\tok\n";
        chainStream << "intonation_output\t" << compareNativeOnnxTensors(intonationTensor, findNativeOnnxTraceTensor(traceOutputs, "f0_list")) << "\n";
        chainStream << "decoder_generated_f0\t" << compareNativeOnnxTensors(generatedF0Tensor, findNativeOnnxTraceTensor(traceInputs, "f0")) << "\n";
        chainStream << "decoder_generated_phoneme\t" << compareNativeOnnxTensors(generatedPhonemeTensor, findNativeOnnxTraceTensor(traceInputs, "phoneme")) << "\n";
        chainStream << "decoder_frames\t" << (generatedF0Tensor.dimensions.empty() ? 0 : generatedF0Tensor.dimensions[0]) << "\n";
        chainStream << "decode_run\tok\n";
        chainStream << "decode_output\t" << compareNativeOnnxTensors(waveTensor, findNativeOnnxTraceTensor(traceOutputs, "wave")) << "\n";
        closeNativeOnnxApi(nativeOnnxApi);
        return chainStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsMoraData(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::string replacedJson = replaceNativeOnnxMoraDataWithApi(nativeOnnxApi, nullptr, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, true, true, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return replacedJson;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsPhonemeLength(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::string replacedJson = replaceNativeOnnxMoraDataWithApi(nativeOnnxApi, nullptr, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, true, false, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return replacedJson;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsMoraPitch(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::string replacedJson = replaceNativeOnnxMoraDataWithApi(nativeOnnxApi, nullptr, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, false, true, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return replacedJson;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsAudioQueryMoraData(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::string accentPhrasesJson = extractJsonArrayField(audioQueryText, "accent_phrases");
    if (accentPhrasesJson.empty()) {
        throw std::runtime_error("accent_phrases がありません");
    }
    std::string replacedAccentPhrasesJson = replaceNativeOnnxModelAssetsMoraData(onnxruntimeLibraryPath, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, shouldUseVvBinConfig);
    return createNativeOnnxAudioQueryTextWithAccentPhrases(audioQueryText, replacedAccentPhrasesJson);
}

std::vector<uint8_t> synthesizeNativeOnnxModelAssetsAudioQuery(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        NativeOnnxTraceInput waveTensor = runNativeOnnxModelAssetChainWave(nativeOnnxApi, nullptr, modelAssets, audioQueryText, styleId, cpuThreadCount, shouldUseVvBinConfig);
        std::vector<uint8_t> wavBytes = createNativeOnnxWavBytes(waveTensor, audioQuerySettings);
        closeNativeOnnxApi(nativeOnnxApi);
        return wavBytes;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

void streamNativeOnnxModelAssetsAudioQueryPcm(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, size_t chunkFrames, const std::function<void(const NativeOnnxPcmStreamInfo &)> &startStream, const std::function<void(const uint8_t *, size_t)> &writeChunk, bool shouldUseVvBinConfig) {
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        const ModelAssetRecord &decodeAsset = requireNativeOnnxModelAsset(modelAssets, "models/d.bin");
        std::vector<uint8_t> decodeBytes = extractNativeOnnxModelAssetBytes(decodeAsset);
        std::vector<NativeOnnxTraceInput> decoderInputs = createNativeOnnxModelAssetDecoderInputs(nativeOnnxApi, nullptr, modelAssets, audioQueryText, styleId, cpuThreadCount, audioQuerySettings, shouldUseVvBinConfig);
        size_t paddedFrameCount = getNativeOnnxPaddedDecoderFrameCount(requireNativeOnnxTensor(decoderInputs, "f0"));
        size_t paddingFrameCount = nativeOnnxDecoderPaddingFrames * 2;
        size_t coreFrameCount = paddedFrameCount > paddingFrameCount ? paddedFrameCount - paddingFrameCount : 0;
        NativeOnnxPcmStreamInfo streamInfo = createNativeOnnxPcmStreamInfo(audioQuerySettings, coreFrameCount);
        startStream(streamInfo);
        size_t safeChunkFrames = std::max({static_cast<size_t>(1), chunkFrames, nativeOnnxDecoderMinimumChunkFrames});
        size_t contextFrames = safeChunkFrames;
        for (size_t coreStartFrame = 0; coreStartFrame < coreFrameCount;) {
            size_t coreEndFrame = std::min(coreStartFrame + safeChunkFrames, coreFrameCount);
            if (coreEndFrame < coreFrameCount && coreFrameCount - coreEndFrame < nativeOnnxDecoderMinimumChunkFrames) {
                coreEndFrame = coreFrameCount;
            }
            NativeOnnxDecoderChunkInputSet chunkInputSet = createNativeOnnxDecoderChunkInputs(decoderInputs, coreStartFrame, coreEndFrame, contextFrames);
            std::vector<NativeOnnxTraceInput> decodeOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, nullptr, decodeAsset, decodeBytes, chunkInputSet.tensors, cpuThreadCount, shouldUseVvBinConfig);
            std::vector<float> waveValues = createNativeOnnxWaveValuesWithoutDecoderFramePadding(requireNativeOnnxTensor(decodeOutputs, "wave"), chunkInputSet.frontCropFrames, chunkInputSet.backCropFrames);
            std::vector<uint8_t> pcmBytes = createNativeOnnxPcmBytes(waveValues, audioQuerySettings);
            if (!pcmBytes.empty()) {
                writeChunk(pcmBytes.data(), pcmBytes.size());
            }
            coreStartFrame = coreEndFrame;
        }
        closeNativeOnnxApi(nativeOnnxApi);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

static std::vector<int64_t> runNativeOnnxSingConsonantLengths(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::vector<NativeOnnxScoreNote> &scoreNotes, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    const ModelAssetRecord &consonantLengthAsset = requireNativeOnnxModelAsset(modelAssets, "models/pscl.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(consonantLengthAsset, "singing_teacher", styleId);
    std::vector<int64_t> consonantValues;
    std::vector<int64_t> vowelValues;
    std::vector<int64_t> noteDurationValues;
    consonantValues.reserve(scoreNotes.size());
    vowelValues.reserve(scoreNotes.size());
    noteDurationValues.reserve(scoreNotes.size());
    for (const NativeOnnxScoreNote &scoreNote : scoreNotes) {
        if (scoreNote.frameLength > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw std::runtime_error("frame_length が大きすぎます");
        }
        noteDurationValues.push_back(static_cast<int64_t>(scoreNote.frameLength));
        if (!scoreNote.hasKey) {
            consonantValues.push_back(-1);
            vowelValues.push_back(0);
        } else {
            consonantValues.push_back(scoreNote.mora.hasConsonant ? parseNativeOnnxPhonemeCode(scoreNote.mora.consonant) : -1);
            vowelValues.push_back(parseNativeOnnxPhonemeCode(scoreNote.mora.vowel));
        }
    }
    std::vector<NativeOnnxTraceInput> consonantLengthInputs;
    consonantLengthInputs.push_back(createNativeOnnxInt64Tensor("consonants", {1, static_cast<int64_t>(consonantValues.size())}, consonantValues));
    consonantLengthInputs.push_back(createNativeOnnxInt64Tensor("vowels", {1, static_cast<int64_t>(vowelValues.size())}, vowelValues));
    consonantLengthInputs.push_back(createNativeOnnxInt64Tensor("note_durations", {1, static_cast<int64_t>(noteDurationValues.size())}, noteDurationValues));
    consonantLengthInputs.push_back(createNativeOnnxInt64Tensor("speaker_id", {1}, {innerVoiceId}));
    std::vector<NativeOnnxTraceInput> consonantLengthOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, runtimeState, consonantLengthAsset, consonantLengthInputs, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<int64_t> consonantLengths = readNativeOnnxTensorValues<int64_t>(requireNativeOnnxTensor(consonantLengthOutputs, "consonant_lengths"), 7);
    if (consonantLengths.size() != scoreNotes.size()) {
        throw std::runtime_error("consonant_lengths の長さが一致しません");
    }
    for (size_t noteIndex = 0; noteIndex < scoreNotes.size(); noteIndex++) {
        bool hasConsonant = scoreNotes[noteIndex].hasKey && scoreNotes[noteIndex].mora.hasConsonant;
        if (consonantLengths[noteIndex] == 0 && hasConsonant) {
            throw std::runtime_error("子音あり音符の consonant_length が 0 です");
        }
        if (consonantLengths[noteIndex] != 0 && !hasConsonant) {
            throw std::runtime_error("子音なし音符の consonant_length が 0 ではありません");
        }
    }
    return consonantLengths;
}

static std::vector<float> runNativeOnnxSingF0Values(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const NativeOnnxSongFrameInputs &frameInputs, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    const ModelAssetRecord &f0Asset = requireNativeOnnxModelAsset(modelAssets, "models/psf.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(f0Asset, "singing_teacher", styleId);
    std::vector<NativeOnnxTraceInput> f0Inputs;
    f0Inputs.push_back(createNativeOnnxInt64Tensor("phonemes", {1, static_cast<int64_t>(frameInputs.phonemeValues.size())}, frameInputs.phonemeValues));
    f0Inputs.push_back(createNativeOnnxInt64Tensor("notes", {1, static_cast<int64_t>(frameInputs.keyValues.size())}, frameInputs.keyValues));
    f0Inputs.push_back(createNativeOnnxInt64Tensor("speaker_id", {1}, {innerVoiceId}));
    std::vector<NativeOnnxTraceInput> f0Outputs = runNativeOnnxSingTeacherModelAssetBytes(nativeOnnxApi, runtimeState, f0Asset, f0Inputs, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<float> f0Values = readNativeOnnxTensorValues<float>(requireNativeOnnxTensor(f0Outputs, "f0s"), 1);
    if (f0Values.size() != frameInputs.phonemeValues.size()) {
        throw std::runtime_error("f0s の長さが一致しません");
    }
    for (float f0Value : f0Values) {
        if (!std::isfinite(f0Value) || f0Value < 0.0f) {
            throw std::runtime_error("f0s に不正な値があります");
        }
    }
    return f0Values;
}

static std::vector<float> runNativeOnnxSingVolumeValues(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const NativeOnnxSongFrameInputs &frameInputs, const std::vector<float> &f0Values, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    if (f0Values.size() != frameInputs.phonemeValues.size()) {
        throw std::runtime_error("f0 の長さが frame 数と一致しません");
    }
    const ModelAssetRecord &volumeAsset = requireNativeOnnxModelAsset(modelAssets, "models/psv.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(volumeAsset, "singing_teacher", styleId);
    std::vector<NativeOnnxTraceInput> volumeInputs;
    volumeInputs.push_back(createNativeOnnxInt64Tensor("phonemes", {1, static_cast<int64_t>(frameInputs.phonemeValues.size())}, frameInputs.phonemeValues));
    volumeInputs.push_back(createNativeOnnxInt64Tensor("notes", {1, static_cast<int64_t>(frameInputs.keyValues.size())}, frameInputs.keyValues));
    volumeInputs.push_back(createNativeOnnxFloatTensor("frame_f0s", {1, static_cast<int64_t>(f0Values.size())}, f0Values));
    volumeInputs.push_back(createNativeOnnxInt64Tensor("speaker_id", {1}, {innerVoiceId}));
    std::vector<NativeOnnxTraceInput> volumeOutputs = runNativeOnnxSingTeacherModelAssetBytes(nativeOnnxApi, runtimeState, volumeAsset, volumeInputs, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<float> volumeValues = readNativeOnnxTensorValues<float>(requireNativeOnnxTensor(volumeOutputs, "volumes"), 1);
    if (volumeValues.size() != frameInputs.phonemeValues.size()) {
        throw std::runtime_error("volumes の長さが一致しません");
    }
    for (float volumeValue : volumeValues) {
        if (!std::isfinite(volumeValue)) {
            throw std::runtime_error("volumes に不正な値があります");
        }
    }
    return volumeValues;
}

static NativeOnnxFrameAudioQuery createNativeOnnxSingFrameAudioQueryWithApi(NativeOnnxApi &nativeOnnxApi, const NativeOnnxRuntimeState *runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::vector<NativeOnnxScoreNote> scoreNotes = parseNativeOnnxScore(scoreText);
    std::vector<int64_t> consonantLengths = runNativeOnnxSingConsonantLengths(nativeOnnxApi, runtimeState, modelAssets, scoreNotes, styleId, cpuThreadCount, shouldUseVvBinConfig);
    std::vector<NativeOnnxSongPhonemeFeature> phonemeFeatures = createNativeOnnxSongPhonemeFeatures(scoreNotes);
    std::vector<uint64_t> phonemeLengths = createNativeOnnxSongPhonemeLengths(scoreNotes, consonantLengths);
    if (phonemeFeatures.size() != phonemeLengths.size()) {
        throw std::runtime_error("歌唱 phoneme_length の長さが一致しません");
    }
    NativeOnnxFrameAudioQuery frameAudioQuery;
    for (size_t phonemeIndex = 0; phonemeIndex < phonemeFeatures.size(); phonemeIndex++) {
        NativeOnnxFramePhoneme framePhoneme;
        framePhoneme.phoneme = phonemeFeatures[phonemeIndex].phoneme;
        framePhoneme.frameLength = phonemeLengths[phonemeIndex];
        framePhoneme.noteId = phonemeFeatures[phonemeIndex].noteId;
        framePhoneme.hasNoteId = phonemeFeatures[phonemeIndex].hasNoteId;
        frameAudioQuery.phonemes.push_back(std::move(framePhoneme));
    }
    NativeOnnxSongFrameInputs frameInputs = createNativeOnnxSongFrameInputs(phonemeFeatures, frameAudioQuery.phonemes);
    frameAudioQuery.f0Values = runNativeOnnxSingF0Values(nativeOnnxApi, runtimeState, modelAssets, frameInputs, styleId, cpuThreadCount, shouldUseVvBinConfig);
    frameAudioQuery.volumeValues = runNativeOnnxSingVolumeValues(nativeOnnxApi, runtimeState, modelAssets, frameInputs, frameAudioQuery.f0Values, styleId, cpuThreadCount, shouldUseVvBinConfig);
    frameAudioQuery.volumeScale = 1.0f;
    frameAudioQuery.outputSamplingRate = nativeOnnxDefaultSamplingRate;
    frameAudioQuery.outputStereo = false;
    validateNativeOnnxParsedFrameAudioQuery(frameAudioQuery);
    return frameAudioQuery;
}

static NativeOnnxSongFrameInputs createNativeOnnxSongFrameInputsFromScoreAndQuery(const std::string &scoreText, const NativeOnnxFrameAudioQuery &frameAudioQuery) {
    std::vector<NativeOnnxScoreNote> scoreNotes = parseNativeOnnxScore(scoreText);
    std::vector<NativeOnnxSongPhonemeFeature> phonemeFeatures = createNativeOnnxSongPhonemeFeatures(scoreNotes);
    return createNativeOnnxSongFrameInputs(phonemeFeatures, frameAudioQuery.phonemes);
}

void validateNativeOnnxFrameAudioQuery(const std::string &frameAudioQueryText) {
    validateNativeOnnxParsedFrameAudioQuery(parseNativeOnnxFrameAudioQuery(frameAudioQueryText));
}

std::string createNativeOnnxModelAssetsSingFrameAudioQuery(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        NativeOnnxFrameAudioQuery frameAudioQuery = createNativeOnnxSingFrameAudioQueryWithApi(nativeOnnxApi, nullptr, modelAssets, scoreText, styleId, cpuThreadCount, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return createNativeOnnxFrameAudioQueryJson(frameAudioQuery);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxModelAssetsSingFrameF0(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxFrameAudioQuery frameAudioQuery = parseNativeOnnxFrameAudioQuery(frameAudioQueryText);
    NativeOnnxSongFrameInputs frameInputs = createNativeOnnxSongFrameInputsFromScoreAndQuery(scoreText, frameAudioQuery);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<float> f0Values = runNativeOnnxSingF0Values(nativeOnnxApi, nullptr, modelAssets, frameInputs, styleId, cpuThreadCount, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return createNativeOnnxFloatArrayJson(f0Values);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxModelAssetsSingFrameVolume(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxFrameAudioQuery frameAudioQuery = parseNativeOnnxFrameAudioQuery(frameAudioQueryText);
    NativeOnnxSongFrameInputs frameInputs = createNativeOnnxSongFrameInputsFromScoreAndQuery(scoreText, frameAudioQuery);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<float> volumeValues = runNativeOnnxSingVolumeValues(nativeOnnxApi, nullptr, modelAssets, frameInputs, frameAudioQuery.f0Values, styleId, cpuThreadCount, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return createNativeOnnxFloatArrayJson(volumeValues);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::vector<uint8_t> synthesizeNativeOnnxModelAssetsFrameAudioQuery(const fs::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxFrameAudioQuery frameAudioQuery = parseNativeOnnxFrameAudioQuery(frameAudioQueryText);
    validateNativeOnnxParsedFrameAudioQuery(frameAudioQuery);
    const ModelAssetRecord &decodeAsset = requireNativeOnnxModelAsset(modelAssets, "models/sd.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(decodeAsset, "frame_decode", styleId);
    std::vector<int64_t> framePhonemeValues;
    framePhonemeValues.reserve(frameAudioQuery.f0Values.size());
    for (const NativeOnnxFramePhoneme &framePhoneme : frameAudioQuery.phonemes) {
        int64_t phonemeCode = parseNativeOnnxPhonemeCode(framePhoneme.phoneme);
        for (uint64_t frameIndex = 0; frameIndex < framePhoneme.frameLength; frameIndex++) {
            framePhonemeValues.push_back(phonemeCode);
        }
    }
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<NativeOnnxTraceInput> decodeInputs;
        decodeInputs.push_back(createNativeOnnxInt64Tensor("frame_phonemes", {1, static_cast<int64_t>(framePhonemeValues.size())}, framePhonemeValues));
        decodeInputs.push_back(createNativeOnnxFloatTensor("frame_f0s", {1, static_cast<int64_t>(frameAudioQuery.f0Values.size())}, frameAudioQuery.f0Values));
        decodeInputs.push_back(createNativeOnnxFloatTensor("frame_volumes", {1, static_cast<int64_t>(frameAudioQuery.volumeValues.size())}, frameAudioQuery.volumeValues));
        decodeInputs.push_back(createNativeOnnxInt64Tensor("speaker_id", {1}, {innerVoiceId}));
        std::vector<NativeOnnxTraceInput> decodeOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, nullptr, decodeAsset, decodeInputs, cpuThreadCount, shouldUseVvBinConfig);
        std::vector<float> waveValues = readNativeOnnxTensorValues<float>(requireNativeOnnxTensor(decodeOutputs, "wav"), 1);
        NativeOnnxAudioQuerySettings audioQuerySettings = createNativeOnnxSettingsFromFrameAudioQuery(frameAudioQuery);
        std::vector<uint8_t> pcmBytes = createNativeOnnxPcmBytes(waveValues, audioQuerySettings);
        uint16_t channels = frameAudioQuery.outputStereo ? 2 : 1;
        std::vector<uint8_t> wavBytes = createPcmWaveHeader(frameAudioQuery.outputSamplingRate, channels, 16, pcmBytes.size());
        wavBytes.insert(wavBytes.end(), pcmBytes.begin(), pcmBytes.end());
        closeNativeOnnxApi(nativeOnnxApi);
        return wavBytes;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsMoraData(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        std::string replacedJson = replaceNativeOnnxMoraDataWithApi(nativeOnnxApi, &runtimeState, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, true, true, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return replacedJson;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsPhonemeLength(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        std::string replacedJson = replaceNativeOnnxMoraDataWithApi(nativeOnnxApi, &runtimeState, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, true, false, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return replacedJson;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsMoraPitch(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        std::string replacedJson = replaceNativeOnnxMoraDataWithApi(nativeOnnxApi, &runtimeState, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, false, true, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return replacedJson;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string replaceNativeOnnxModelAssetsAudioQueryMoraData(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    std::string accentPhrasesJson = extractJsonArrayField(audioQueryText, "accent_phrases");
    if (accentPhrasesJson.empty()) {
        throw std::runtime_error("accent_phrases がありません");
    }
    std::string replacedAccentPhrasesJson = replaceNativeOnnxModelAssetsMoraData(runtimeState, modelAssets, accentPhrasesJson, styleId, cpuThreadCount, shouldUseVvBinConfig);
    return createNativeOnnxAudioQueryTextWithAccentPhrases(audioQueryText, replacedAccentPhrasesJson);
}

std::vector<uint8_t> synthesizeNativeOnnxModelAssetsAudioQuery(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        NativeOnnxTraceInput waveTensor = runNativeOnnxModelAssetChainWave(nativeOnnxApi, &runtimeState, modelAssets, audioQueryText, styleId, cpuThreadCount, shouldUseVvBinConfig);
        std::vector<uint8_t> wavBytes = createNativeOnnxWavBytes(waveTensor, audioQuerySettings);
        closeNativeOnnxApi(nativeOnnxApi);
        return wavBytes;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

void streamNativeOnnxModelAssetsAudioQueryPcm(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, size_t chunkFrames, const std::function<void(const NativeOnnxPcmStreamInfo &)> &startStream, const std::function<void(const uint8_t *, size_t)> &writeChunk, bool shouldUseVvBinConfig) {
    NativeOnnxAudioQuerySettings audioQuerySettings = parseNativeOnnxAudioQuerySettings(audioQueryText);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        const ModelAssetRecord &decodeAsset = requireNativeOnnxModelAsset(modelAssets, "models/d.bin");
        std::vector<uint8_t> decodeBytes;
        if (shouldUseVvBinConfig) {
            decodeBytes = extractNativeOnnxModelAssetBytes(decodeAsset);
        }
        std::vector<NativeOnnxTraceInput> decoderInputs = createNativeOnnxModelAssetDecoderInputs(nativeOnnxApi, &runtimeState, modelAssets, audioQueryText, styleId, cpuThreadCount, audioQuerySettings, shouldUseVvBinConfig);
        size_t paddedFrameCount = getNativeOnnxPaddedDecoderFrameCount(requireNativeOnnxTensor(decoderInputs, "f0"));
        size_t paddingFrameCount = nativeOnnxDecoderPaddingFrames * 2;
        size_t coreFrameCount = paddedFrameCount > paddingFrameCount ? paddedFrameCount - paddingFrameCount : 0;
        NativeOnnxPcmStreamInfo streamInfo = createNativeOnnxPcmStreamInfo(audioQuerySettings, coreFrameCount);
        startStream(streamInfo);
        size_t safeChunkFrames = std::max({static_cast<size_t>(1), chunkFrames, nativeOnnxDecoderMinimumChunkFrames});
        size_t contextFrames = safeChunkFrames;
        for (size_t coreStartFrame = 0; coreStartFrame < coreFrameCount;) {
            size_t coreEndFrame = std::min(coreStartFrame + safeChunkFrames, coreFrameCount);
            if (coreEndFrame < coreFrameCount && coreFrameCount - coreEndFrame < nativeOnnxDecoderMinimumChunkFrames) {
                coreEndFrame = coreFrameCount;
            }
            NativeOnnxDecoderChunkInputSet chunkInputSet = createNativeOnnxDecoderChunkInputs(decoderInputs, coreStartFrame, coreEndFrame, contextFrames);
            std::vector<NativeOnnxTraceInput> decodeOutputs = shouldUseVvBinConfig
                ? runNativeOnnxModelAssetBytes(nativeOnnxApi, &runtimeState, decodeAsset, decodeBytes, chunkInputSet.tensors, cpuThreadCount, true)
                : runNativeOnnxModelAssetBytes(nativeOnnxApi, &runtimeState, decodeAsset, chunkInputSet.tensors, cpuThreadCount, false);
            std::vector<float> waveValues = createNativeOnnxWaveValuesWithoutDecoderFramePadding(requireNativeOnnxTensor(decodeOutputs, "wave"), chunkInputSet.frontCropFrames, chunkInputSet.backCropFrames);
            std::vector<uint8_t> pcmBytes = createNativeOnnxPcmBytes(waveValues, audioQuerySettings);
            if (!pcmBytes.empty()) {
                writeChunk(pcmBytes.data(), pcmBytes.size());
            }
            coreStartFrame = coreEndFrame;
        }
        closeNativeOnnxApi(nativeOnnxApi);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxModelAssetsSingFrameAudioQuery(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        NativeOnnxFrameAudioQuery frameAudioQuery = createNativeOnnxSingFrameAudioQueryWithApi(nativeOnnxApi, &runtimeState, modelAssets, scoreText, styleId, cpuThreadCount, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return createNativeOnnxFrameAudioQueryJson(frameAudioQuery);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxModelAssetsSingFrameF0(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxFrameAudioQuery frameAudioQuery = parseNativeOnnxFrameAudioQuery(frameAudioQueryText);
    NativeOnnxSongFrameInputs frameInputs = createNativeOnnxSongFrameInputsFromScoreAndQuery(scoreText, frameAudioQuery);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        std::vector<float> f0Values = runNativeOnnxSingF0Values(nativeOnnxApi, &runtimeState, modelAssets, frameInputs, styleId, cpuThreadCount, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return createNativeOnnxFloatArrayJson(f0Values);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::string createNativeOnnxModelAssetsSingFrameVolume(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxFrameAudioQuery frameAudioQuery = parseNativeOnnxFrameAudioQuery(frameAudioQueryText);
    NativeOnnxSongFrameInputs frameInputs = createNativeOnnxSongFrameInputsFromScoreAndQuery(scoreText, frameAudioQuery);
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        std::vector<float> volumeValues = runNativeOnnxSingVolumeValues(nativeOnnxApi, &runtimeState, modelAssets, frameInputs, frameAudioQuery.f0Values, styleId, cpuThreadCount, shouldUseVvBinConfig);
        closeNativeOnnxApi(nativeOnnxApi);
        return createNativeOnnxFloatArrayJson(volumeValues);
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::vector<uint8_t> synthesizeNativeOnnxModelAssetsFrameAudioQuery(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig) {
    NativeOnnxFrameAudioQuery frameAudioQuery = parseNativeOnnxFrameAudioQuery(frameAudioQueryText);
    validateNativeOnnxParsedFrameAudioQuery(frameAudioQuery);
    const ModelAssetRecord &decodeAsset = requireNativeOnnxModelAsset(modelAssets, "models/sd.bin");
    int64_t innerVoiceId = resolveNativeOnnxInnerVoiceId(decodeAsset, "frame_decode", styleId);
    std::vector<int64_t> framePhonemeValues;
    framePhonemeValues.reserve(frameAudioQuery.f0Values.size());
    for (const NativeOnnxFramePhoneme &framePhoneme : frameAudioQuery.phonemes) {
        int64_t phonemeCode = parseNativeOnnxPhonemeCode(framePhoneme.phoneme);
        for (uint64_t frameIndex = 0; frameIndex < framePhoneme.frameLength; frameIndex++) {
            framePhonemeValues.push_back(phonemeCode);
        }
    }
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(runtimeState.libraryPath);
    try {
        std::vector<NativeOnnxTraceInput> decodeInputs;
        decodeInputs.push_back(createNativeOnnxInt64Tensor("frame_phonemes", {1, static_cast<int64_t>(framePhonemeValues.size())}, framePhonemeValues));
        decodeInputs.push_back(createNativeOnnxFloatTensor("frame_f0s", {1, static_cast<int64_t>(frameAudioQuery.f0Values.size())}, frameAudioQuery.f0Values));
        decodeInputs.push_back(createNativeOnnxFloatTensor("frame_volumes", {1, static_cast<int64_t>(frameAudioQuery.volumeValues.size())}, frameAudioQuery.volumeValues));
        decodeInputs.push_back(createNativeOnnxInt64Tensor("speaker_id", {1}, {innerVoiceId}));
        std::vector<NativeOnnxTraceInput> decodeOutputs = runNativeOnnxModelAssetBytes(nativeOnnxApi, &runtimeState, decodeAsset, decodeInputs, cpuThreadCount, shouldUseVvBinConfig);
        std::vector<float> waveValues = readNativeOnnxTensorValues<float>(requireNativeOnnxTensor(decodeOutputs, "wav"), 1);
        NativeOnnxAudioQuerySettings audioQuerySettings = createNativeOnnxSettingsFromFrameAudioQuery(frameAudioQuery);
        std::vector<uint8_t> pcmBytes = createNativeOnnxPcmBytes(waveValues, audioQuerySettings);
        uint16_t channels = frameAudioQuery.outputStereo ? 2 : 1;
        std::vector<uint8_t> wavBytes = createPcmWaveHeader(frameAudioQuery.outputSamplingRate, channels, 16, pcmBytes.size());
        wavBytes.insert(wavBytes.end(), pcmBytes.begin(), pcmBytes.end());
        closeNativeOnnxApi(nativeOnnxApi);
        return wavBytes;
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

std::vector<uint8_t> synthesizeNativeOnnxVvmAudioQuery(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &audioQueryPath, uint32_t styleId, uint16_t cpuThreadCount) {
    ensurePathExists(audioQueryPath, "audio query");
    std::string audioQueryText = readNativeOnnxTextFile(audioQueryPath);
    return synthesizeNativeOnnxModelAssetsAudioQuery(onnxruntimeLibraryPath, collectModelAssets(archiveSummaries), audioQueryText, styleId, cpuThreadCount);
}
