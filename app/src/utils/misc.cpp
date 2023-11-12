#include "utils/misc.hpp"
#include <fmt/format.h>
#include <fmt/chrono.h>

namespace misc {

std::string sec2Time(int64_t t) {
    if (t < 3600) {
        return fmt::format("{:%M:%S}", std::chrono::seconds(t));
    }
    return fmt::format("{:%H:%M:%S}", std::chrono::seconds(t));
}

std::string md5hash(const std::string& data) {
    return "";
}

}