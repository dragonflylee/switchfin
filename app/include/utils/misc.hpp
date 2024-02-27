#pragma once

#include <string>
#include <cstdint>

namespace misc {

std::string sec2Time(int64_t t);

std::string randHex(const int len);

std::string hexEncode(const unsigned char* data, size_t len);

void initCrashDump();

}