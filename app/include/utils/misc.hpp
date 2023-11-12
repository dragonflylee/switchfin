#pragma once

#include <string>
#include <cstdint>

namespace misc {

std::string sec2Time(int64_t t);

std::string md5hash(const std::string& data);

}