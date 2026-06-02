#include "dynamic_library.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

void *openDynamicLibrary(const std::filesystem::path &libraryPath) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(LoadLibraryW(libraryPath.wstring().c_str()));
#else
    return dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void *loadDynamicLibrarySymbol(void *libraryHandle, const char *symbolName) {
#if defined(_WIN32)
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(libraryHandle), symbolName));
#else
    return dlsym(libraryHandle, symbolName);
#endif
}

void closeDynamicLibrary(void *libraryHandle) {
    if (!libraryHandle) {
        return;
    }
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(libraryHandle));
#else
    dlclose(libraryHandle);
#endif
}

std::string getDynamicLibraryErrorText() {
#if defined(_WIN32)
    DWORD errorCode = GetLastError();
    if (errorCode == 0) {
        return "unknown";
    }
    LPWSTR wideMessage = nullptr;
    DWORD messageLength = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&wideMessage),
        0,
        nullptr);
    std::string errorText = "error code " + std::to_string(errorCode);
    if (messageLength > 0 && wideMessage != nullptr) {
        int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideMessage, static_cast<int>(messageLength), nullptr, 0, nullptr, nullptr);
        if (utf8Length > 0) {
            std::string utf8Text(static_cast<size_t>(utf8Length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wideMessage, static_cast<int>(messageLength), utf8Text.data(), utf8Length, nullptr, nullptr);
            while (!utf8Text.empty() && (utf8Text.back() == '\n' || utf8Text.back() == '\r' || utf8Text.back() == ' ')) {
                utf8Text.pop_back();
            }
            if (!utf8Text.empty()) {
                errorText = utf8Text;
            }
        }
        LocalFree(wideMessage);
    }
    return errorText;
#else
    const char *errorText = dlerror();
    return errorText ? errorText : "unknown";
#endif
}
