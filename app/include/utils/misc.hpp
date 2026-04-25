#pragma once

#include <string>
#include <cstdint>
#include <vector>

#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include("experimental/filesystem")
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#elif !defined(USE_LIBROMFS)
#error "Failed to include <filesystem> header!"
#endif

namespace misc {

std::string sec2Time(int64_t t);

std::string formatSize(uint64_t s);

std::string formatTime(const std::string& str);

std::string randHex(const int len);

std::string hexEncode(const unsigned char* data, size_t len);

void split(const std::string& data, std::vector<std::string>& result, char seq);

bool sendIPC(const std::string& sock, const std::string& payload);

void initCrashDump();

}  // namespace misc

namespace base64 {

std::string encode(const std::string& input);

}