#include "utils/misc.hpp"
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace misc {

std::string sec2Time(int64_t t) {
    if (t < 3600) {
        return fmt::format("{:%M:%S}", std::chrono::seconds(t));
    }
    return fmt::format("{:%H:%M:%S}", std::chrono::seconds(t));
}

std::string randHex(const int len) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << dis(gen);
    }
    return ss.str();
}

std::string hexEncode(const unsigned char* data, size_t len) {
    std::stringstream ss;
    for (size_t i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i];
    }
    return ss.str();
}

}  // namespace misc