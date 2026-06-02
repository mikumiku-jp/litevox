#pragma once

#include <filesystem>
#include <string>

void *openDynamicLibrary(const std::filesystem::path &libraryPath);
void *loadDynamicLibrarySymbol(void *libraryHandle, const char *symbolName);
void closeDynamicLibrary(void *libraryHandle);
std::string getDynamicLibraryErrorText();
