#pragma once

#include <array>
#include <cstdint>
#include <vector>

using Byte = std::uint8_t;
using Frequency = std::uint64_t;
using FrequencyTable = std::array<Frequency, 256>;

struct Codeword {
    std::vector<bool> bits;
};

using CodeTable = std::array<Codeword, 256>;
