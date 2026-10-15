#pragma once

#include <cstdint>
#include <vector>

// TT "LZ2K" chunk wrapper. Payload bytes are the whole member, headers included.
bool Lz2kDecompress(const uint8_t* src, int srcLen, std::vector<uint8_t>& dst);
