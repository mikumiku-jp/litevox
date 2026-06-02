#include "runtime.hpp"

#include "runtime_internal.hpp"
#include "native_audio_query.hpp"
#include "native_audio_query_validation.hpp"
#include "native_text_query.hpp"
#include "native_user_dict.hpp"
#include "json_utility.hpp"
#include "preset_store.hpp"
#include "setting_store.hpp"
#include "utility.hpp"
#include "vvm_archive.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;

static std::string createVoiceModelLibraryName(const VoiceModelRecord &modelRecord) {
    std::vector<std::string> speakerNames;
    for (const std::string &speakerObject : splitJsonObjects(modelRecord.metasJson)) {
        std::string speakerName = extractJsonStringField(speakerObject, "name");
        if (!speakerName.empty() && std::find(speakerNames.begin(), speakerNames.end(), speakerName) == speakerNames.end()) {
            speakerNames.push_back(speakerName);
        }
    }
    if (speakerNames.empty()) {
        return modelRecord.modelPath.filename().string();
    }
    if (speakerNames.size() == 1) {
        return speakerNames.front();
    }
    return speakerNames.front() + " ほか " + std::to_string(speakerNames.size() - 1) + "名";
}

static std::string createVoiceModelLibraryVersion(const VoiceModelRecord &modelRecord) {
    for (const std::string &speakerObject : splitJsonObjects(modelRecord.metasJson)) {
        std::string versionText = extractJsonStringField(speakerObject, "version");
        if (!versionText.empty()) {
            return versionText;
        }
    }
    return "";
}

static std::string createVoiceModelLibrarySpeakersJson(const VoiceModelRecord &modelRecord, bool supportsMorphing) {
    std::vector<std::string> speakerObjects = splitJsonObjects(createSpeakersJson(modelRecord.metasJson, supportsMorphing));
    std::ostringstream librarySpeakersStream;
    librarySpeakersStream << "[";
    for (size_t speakerIndex = 0; speakerIndex < speakerObjects.size(); speakerIndex++) {
        if (speakerIndex > 0) {
            librarySpeakersStream << ",";
        }
        std::string speakerUuid = extractJsonStringField(speakerObjects[speakerIndex], "speaker_uuid");
        librarySpeakersStream << "{\"speaker\":" << speakerObjects[speakerIndex] << ",";
        librarySpeakersStream << "\"speaker_info\":" << createSpeakerInfoJson(modelRecord.metasJson, speakerUuid) << "}";
    }
    librarySpeakersStream << "]";
    return librarySpeakersStream.str();
}

std::string createInstalledLibrariesJson(RuntimeState &runtimeState) {
    bool supportsMorphing = getCoreBackendCapabilities(runtimeState.coreBackend).supportsMorphing;
    std::ostringstream librariesStream;
    librariesStream << "{";
    for (size_t modelIndex = 0; modelIndex < runtimeState.voiceModels.size(); modelIndex++) {
        const VoiceModelRecord &modelRecord = runtimeState.voiceModels[modelIndex];
        std::string libraryUuid = getVoiceModelLibraryUuid(modelRecord);
        if (modelIndex > 0) {
            librariesStream << ",";
        }
        librariesStream << quoteJsonString(libraryUuid) << ":{";
        librariesStream << "\"name\":" << quoteJsonString(createVoiceModelLibraryName(modelRecord)) << ",";
        librariesStream << "\"uuid\":" << quoteJsonString(libraryUuid) << ",";
        librariesStream << "\"version\":" << quoteJsonString(createVoiceModelLibraryVersion(modelRecord)) << ",";
        librariesStream << "\"download_url\":\"\",";
        librariesStream << "\"bytes\":" << countModelAssetBytes(modelRecord.modelAssets) << ",";
        librariesStream << "\"speakers\":" << createVoiceModelLibrarySpeakersJson(modelRecord, supportsMorphing) << ",";
        librariesStream << "\"uninstallable\":" << (modelRecord.isInstalledLibrary ? "true" : "false") << "}";
    }
    librariesStream << "}";
    return librariesStream.str();
}

std::string createDownloadableLibrariesJson(RuntimeState &runtimeState) {
    return readOptionalJsonArrayFile(runtimeState.engineManifestAssetDirectory / "downloadable_libraries.json");
}

