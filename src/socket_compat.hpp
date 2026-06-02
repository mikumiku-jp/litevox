#pragma once

#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using LitevoxSocket = SOCKET;
#else
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using LitevoxSocket = int;
#endif

void initializeSocketRuntime();
void closeSocket(LitevoxSocket socketHandle);
bool isValidSocket(LitevoxSocket socketHandle);
std::string getSocketErrorText(int errorCode = 0);
std::string getAddrInfoErrorText(int errorCode);
