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

// Normalize a Jellyfin codec string to a lowercase file extension.
// "subrip" is mapped to "srt"; an empty codec defaults to "srt".
std::string codec2Ext(const std::string& codec);

// Build the URL of a Jellyfin subtitle stream. Falls back to the standard
// subtitle endpoint (/Videos/{itemId}/{sourceId}/Subtitles/{index}/0/Stream.{ext})
// when DeliveryUrl is empty. Returns an empty string when the stream cannot be
// addressed (embedded stream without codec and without a delivery URL).
std::string buildSubtitleUrl(const std::string& serverUrl, const std::string& itemId,
    const std::string& sourceId, long index, const std::string& codec, bool isExternal,
    const std::string& deliveryUrl);

bool sendIPC(const std::string& sock, const std::string& payload);

void initCrashDump();

}  // namespace misc

namespace base64 {

std::string encode(const std::string& input);

}