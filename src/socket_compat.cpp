#include "socket_compat.hpp"

#include <mutex>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstring>
#endif

void initializeSocketRuntime() {
#if defined(_WIN32)
    static std::once_flag socketInitFlag;
    std::call_once(socketInitFlag, []() {
        WSADATA socketData{};
        if (WSAStartup(MAKEWORD(2, 2), &socketData) != 0) {
            throw std::runtime_error("WinSock を初期化できません");
        }
    });
#endif
}

void closeSocket(LitevoxSocket socketHandle) {
    if (!isValidSocket(socketHandle)) {
        return;
    }
#if defined(_WIN32)
    closesocket(socketHandle);
#else
    close(socketHandle);
#endif
}

bool isValidSocket(LitevoxSocket socketHandle) {
#if defined(_WIN32)
    return socketHandle != INVALID_SOCKET;
#else
    return socketHandle >= 0;
#endif
}

std::string getSocketErrorText(int errorCode) {
#if defined(_WIN32)
    DWORD resolvedErrorCode = errorCode == 0 ? static_cast<DWORD>(WSAGetLastError()) : static_cast<DWORD>(errorCode);
    if (resolvedErrorCode == 0) {
        return "unknown";
    }
    LPWSTR wideMessage = nullptr;
    DWORD messageLength = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        resolvedErrorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&wideMessage),
        0,
        nullptr);
    std::string errorText = "error code " + std::to_string(resolvedErrorCode);
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
    int resolvedErrorCode = errorCode == 0 ? errno : errorCode;
    const char *errorText = std::strerror(resolvedErrorCode);
    return errorText ? errorText : "unknown";
#endif
}

std::string getAddrInfoErrorText(int errorCode) {
#if defined(_WIN32)
    const char *errorText = gai_strerrorA(errorCode);
#else
    const char *errorText = gai_strerror(errorCode);
#endif
    return errorText ? errorText : std::to_string(errorCode);
}
