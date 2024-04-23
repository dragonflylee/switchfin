#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace misc {

std::string sec2Time(int64_t t);

std::string randHex(const int len);

std::string hexEncode(const unsigned char* data, size_t len);

std::vector<std::string> split(const std::string& data, char seq);

bool sendIPC(const std::string& sock, const std::string& payload);

void initCrashDump();

}