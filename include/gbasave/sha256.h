#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gbasave
{
    std::string sha256Hex(const std::vector<std::uint8_t> &data);
}
