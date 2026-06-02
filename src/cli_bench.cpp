#include "cli_bench.hpp"

#include "json_utility.hpp"
#include "socket_compat.hpp"
#include "utility.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
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

std::vector<std::unique_ptr<RuntimeHolder>> createRuntimeHolders(const CliOptions &cliOptions, std::map<uint32_t, size_t> *sharedLoadedStyleCounts, std::mutex *sharedLoadedStylesMutex, std::map<uint32_t, uint64_t> *sharedStyleUnloadGenerations, std::mutex *sharedStyleUnloadMutex, std::mutex *sharedUserDictMutex, std::mutex *sharedPresetMutex, std::mutex *sharedSettingMutex, std::mutex *sharedLibraryMutex) {
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

int runBenchCommand(const CliOptions &cliOptions) {
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

int runSongBenchCommand(const CliOptions &cliOptions) {
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

int runHttpSongBenchCommand(const CliOptions &cliOptions) {
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

int runHttpBenchCommand(const CliOptions &cliOptions) {
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

int runApiSessionCommand(const CliOptions &cliOptions) {
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

