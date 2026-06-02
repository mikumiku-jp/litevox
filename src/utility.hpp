#pragma once

#include <filesystem>
#include <string>
#include <vector>

std::string readTextFile(const std::filesystem::path &filePath);
std::vector<uint8_t> readBinaryFile(const std::filesystem::path &filePath);
void writeTextFile(const std::filesystem::path &filePath, const std::string &bodyText);
void writeBinaryFile(const std::filesystem::path &filePath, const std::vector<uint8_t> &fileBytes);
void writeBinaryStdout(const std::vector<uint8_t> &fileBytes);
std::string trimAscii(const std::string &text);
std::string lowercaseAscii(std::string text);
std::vector<uint8_t> makeBodyBytes(const std::string &bodyText);
std::string createSha256Hex(const uint8_t *bytes, size_t byteCount);
std::filesystem::path getExecutableDirectory();
void ensurePathExists(const std::filesystem::path &pathToCheck, const std::string &label);
std::filesystem::path findFirstExistingPath(const std::filesystem::path &directoryPath, const std::vector<std::string> &fileNames);
std::filesystem::path getFirstNamedPath(const std::filesystem::path &directoryPath, const std::vector<std::string> &fileNames);
std::string getPlatformExecutableFilename(const std::string &baseName);
std::vector<std::string> getVoicevoxCoreLibraryFileNames();
std::vector<std::string> getVoicevoxOnnxruntimeLibraryFileNames();
void setEnvironmentVariable(const std::string &name, const std::string &value);
void clearEnvironmentVariable(const std::string &name);
uint64_t getPeakResidentBytes();
std::filesystem::path createTemporaryFilePath(const std::string &baseName, const std::string &extensionText);
