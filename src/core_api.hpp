#pragma once

#include "voicevox_types.hpp"

#include <filesystem>
#include <string>

CoreApi loadCoreApi(const std::filesystem::path &coreLibraryPath);
void closeCoreApi(CoreApi &coreApi);
std::string describeCoreError(const CoreApi &coreApi, VoicevoxResultCode callStatus);
void ensureCoreCall(const CoreApi &coreApi, VoicevoxResultCode callStatus, const std::string &actionName);
std::string takeJsonPointer(CoreApi &coreApi, char *jsonPointer);
