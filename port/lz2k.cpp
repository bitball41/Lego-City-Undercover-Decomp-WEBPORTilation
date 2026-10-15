#include "lz2k.h"

#include <cstring>

namespace {

constexpr int kChunk = 0x2000;

struct Lz2k {
    const uint8_t* src = nullptr;
    int srcLen = 0;
    uint8_t chunk[kChunk]{};
    uint8_t smallBytes[20]{};
    uint8_t largeBytes[510]{};
    int16_t smallWords[256]{};
    int16_t largeWords[4096]{};
    int16_t parallel0[1024]{};
    int16_t parallel1[1024]{};
    uint32_t bitStream = 0;
    uint8_t previousBitAlign = 0;
    uint8_t lastByte = 0;
    uint32_t srcOffset = 0;
    int literalsToCopy = 0;
    int16_t chunksWithSetup = 0;
    uint32_t readOffset = 0;
    bool ok = true;

    void LoadBits(int bits) {
        if (bits < 0 || bits > 32) {
            ok = false;
            return;
        }
        bitStream <<= bits;
        if (bits > previousBitAlign) {
            do {
                bits -= previousBitAlign;
                bitStream |= static_cast<uint32_t>(lastByte) << bits;
                lastByte = srcOffset >= static_cast<uint32_t>(srcLen) ? 0 : src[srcOffset++];
                previousBitAlign = 8;
            } while (bits > previousBitAlign);
        }
        previousBitAlign = static_cast<uint8_t>(previousBitAlign - bits);
        bitStream |= static_cast<uint32_t>(lastByte) >> previousBitAlign;
    }

    bool ClearBytes(uint8_t* buf, int cap, int index, int length) {
        if (length < 0 || index < 0 || index > cap || length > cap - index) {
            ok = false;
            return false;
        }
        if (length > 0) {
            std::memset(buf + index, 0, static_cast<size_t>(length));
        }
        return true;
    }

    void FillWords(int bytesLength, uint8_t* bytesBuffer, int bytesCap, int pivot, int16_t* words, int wordsCap) {
        if (bytesLength < 0 || bytesLength > bytesCap || pivot < 1 || pivot > 15) {
            ok = false;
            return;
        }
        uint16_t counts[17]{};
        uint16_t dest[18]{};
        for (int i = 0; i < bytesLength; ++i) {
            if (bytesBuffer[i] > 16) {
                ok = false;
                return;
            }
            counts[bytesBuffer[i]]++;
        }
        int shift = 14;
        int ind = 1;
        while (ind <= 16) {
            uint16_t low = counts[ind];
            uint16_t high = counts[ind + 1];
            low = static_cast<uint16_t>(low << (shift + 1));
            high = static_cast<uint16_t>(high << shift);
            low = static_cast<uint16_t>(low + dest[ind]);
            ind += 4;
            high = static_cast<uint16_t>(high + low);
            dest[ind - 3] = low;
            dest[ind - 2] = high;
            low = static_cast<uint16_t>(counts[ind - 2] << (shift - 1));
            low = static_cast<uint16_t>(low + high);
            high = static_cast<uint16_t>(counts[ind - 1] << (shift - 2));
            high = static_cast<uint16_t>(high + low);
            dest[ind - 1] = low;
            dest[ind] = high;
            shift -= 4;
        }
        if (dest[17] != 0) {
            ok = false;
            return;
        }
        shift = pivot - 1;
        int tmp = 16 - pivot;
        int tmpCopy = tmp;
        for (int i = 1; i <= pivot; ++i) {
            dest[i] = static_cast<uint16_t>(dest[i] >> tmp);
            counts[i] = static_cast<uint16_t>(1u << shift);
            --shift;
        }
        --tmp;
        for (int i = pivot + 1; i <= 16; ++i) {
            counts[i] = static_cast<uint16_t>(1u << tmp);
            --tmp;
        }
        uint16_t comp1 = static_cast<uint16_t>(dest[pivot + 1] >> (16 - pivot));
        if (comp1 != 0) {
            uint16_t comp2 = static_cast<uint16_t>(1u << pivot);
            if (comp1 != comp2) {
                for (int i = comp1; i < comp2 && i < wordsCap; ++i) {
                    words[i] = 0;
                }
            }
        }
        if (bytesLength <= 0) {
            return;
        }
        shift = 15 - pivot;
        uint16_t mask = static_cast<uint16_t>(1u << shift);
        int node = bytesLength;
        for (int i = 0; i < bytesLength; ++i) {
            uint8_t codeLen = bytesBuffer[i];
            if (codeLen == 0) {
                continue;
            }
            uint16_t destValue = dest[codeLen];
            uint16_t srcValue = counts[codeLen];
            srcValue = static_cast<uint16_t>(srcValue + destValue);
            if (codeLen > pivot) {
                int16_t* table = words;
                int offset = destValue >> tmpCopy;
                int left = codeLen - pivot;
                while (left != 0) {
                    if (offset < 0 || offset >= wordsCap) {
                        ok = false;
                        return;
                    }
                    if (table[offset] == 0) {
                        if (node < 0 || node >= 1024) {
                            ok = false;
                            return;
                        }
                        parallel0[node] = 0;
                        parallel1[node] = 0;
                        table[offset] = static_cast<int16_t>(node);
                        ++node;
                    }
                    offset = table[offset];
                    table = (destValue & mask) == 0 ? parallel0 : parallel1;
                    destValue = static_cast<uint16_t>(destValue + destValue);
                    --left;
                }
                if (offset < 0 || offset >= wordsCap) {
                    ok = false;
                    return;
                }
                table[offset] = static_cast<int16_t>(i);
            } else if (destValue < srcValue) {
                for (int j = destValue; j < srcValue; ++j) {
                    if (j < 0 || j >= wordsCap) {
                        ok = false;
                        return;
                    }
                    words[j] = static_cast<int16_t>(i);
                }
            }
            dest[codeLen] = srcValue;
        }
    }

    void FillSmall(int length, int bits, int specialInd) {
        if (length > 20 || bits <= 0 || bits > 31) {
            ok = false;
            return;
        }
        int count = static_cast<int>(bitStream >> (32 - bits));
        LoadBits(bits);
        if (!ok) {
            return;
        }
        if (count != 0) {
            int filled = 0;
            while (filled < count) {
                uint32_t symbol = static_cast<uint8_t>(bitStream >> 29);
                int take = 3;
                if (symbol == 7) {
                    uint32_t mask = 0x10000000u;
                    if ((bitStream & mask) == 0) {
                        take = 4;
                    } else {
                        int extra = 0;
                        while ((bitStream & mask) != 0) {
                            mask >>= 1;
                            ++extra;
                            if (extra > 24) {
                                ok = false;
                                return;
                            }
                        }
                        take = extra + 4;
                        symbol += static_cast<uint32_t>(extra);
                    }
                }
                LoadBits(take);
                if (filled >= 20) {
                    ok = false;
                    return;
                }
                smallBytes[filled] = static_cast<uint8_t>(symbol);
                ++filled;
                if (filled == specialInd) {
                    int special = static_cast<int>(bitStream >> 30);
                    LoadBits(2);
                    if (special >= 1) {
                        if (!ClearBytes(smallBytes, 20, filled, special)) {
                            return;
                        }
                        filled += special;
                    }
                }
            }
            if (filled < length) {
                if (!ClearBytes(smallBytes, 20, filled, length - filled)) {
                    return;
                }
            }
            FillWords(length, smallBytes, 20, 8, smallWords, 256);
            return;
        }
        count = static_cast<int>(bitStream >> (32 - bits));
        LoadBits(bits);
        ClearBytes(smallBytes, 20, 0, length);
        for (int i = 0; i < 256; ++i) {
            smallWords[i] = static_cast<int16_t>(count);
        }
    }

    void FillLarge() {
        int count = static_cast<int>(bitStream >> 23);
        LoadBits(9);
        if (!ok) {
            return;
        }
        if (count == 0) {
            int symbol = static_cast<int>(bitStream >> 23);
            LoadBits(9);
            ClearBytes(largeBytes, 510, 0, 510);
            for (int i = 0; i < 4096; ++i) {
                largeWords[i] = static_cast<int16_t>(symbol);
            }
            return;
        }
        if (count < 0) {
            ClearBytes(largeBytes, 510, 0, 510);
            FillWords(510, largeBytes, 510, 12, largeWords, 4096);
            return;
        }
        int filled = 0;
        while (filled < count) {
            int lookup = static_cast<int>(bitStream >> 24);
            if (lookup < 0 || lookup >= 256) {
                ok = false;
                return;
            }
            int symbol = smallWords[lookup];
            if (symbol >= 19) {
                uint32_t mask = 0x800000u;
                do {
                    if (symbol < 0 || symbol >= 1024) {
                        ok = false;
                        return;
                    }
                    symbol = (bitStream & mask) == 0 ? parallel0[symbol] : parallel1[symbol];
                    mask >>= 1;
                } while (symbol >= 19);
            }
            if (symbol < 0 || symbol >= 20) {
                ok = false;
                return;
            }
            LoadBits(smallBytes[symbol]);
            if (symbol > 2) {
                symbol -= 2;
                if (filled >= 510) {
                    ok = false;
                    return;
                }
                largeBytes[filled++] = static_cast<uint8_t>(symbol);
            } else {
                int run = 1;
                if (symbol == 0) {
                    run = 1;
                } else if (symbol == 1) {
                    int extra = static_cast<int>(bitStream >> 28);
                    LoadBits(4);
                    run = extra + 3;
                } else {
                    int extra = static_cast<int>(bitStream >> 23);
                    LoadBits(9);
                    run = extra + 20;
                }
                if (run > 0) {
                    if (!ClearBytes(largeBytes, 510, filled, run)) {
                        return;
                    }
                    filled += run;
                }
            }
        }
        if (filled < 510) {
            ClearBytes(largeBytes, 510, filled, 510 - filled);
        }
        FillWords(510, largeBytes, 510, 12, largeWords, 4096);
    }

    int16_t DecodeSymbol() {
        if (chunksWithSetup == 0) {
            chunksWithSetup = static_cast<int16_t>(bitStream >> 16);
            LoadBits(0x10);
            FillSmall(19, 5, 3);
            FillLarge();
            FillSmall(14, 4, -1);
            if (!ok) {
                return 0;
            }
        }
        --chunksWithSetup;
        int lookup = static_cast<int>(bitStream >> 20);
        if (lookup < 0 || lookup >= 4096) {
            ok = false;
            return 0;
        }
        int symbol = largeWords[lookup];
        if (symbol >= 510) {
            uint32_t mask = 0x80000u;
            while (symbol >= 510) {
                if (symbol < 0 || symbol >= 1024) {
                    ok = false;
                    return 0;
                }
                symbol = (bitStream & mask) == 0 ? parallel0[symbol] : parallel1[symbol];
                mask >>= 1;
            }
        }
        if (symbol < 0 || symbol >= 510) {
            ok = false;
            return 0;
        }
        LoadBits(largeBytes[symbol]);
        return static_cast<int16_t>(symbol);
    }

    int DecodeDistance() {
        int lookup = static_cast<int>(bitStream >> 24);
        if (lookup < 0 || lookup >= 256) {
            ok = false;
            return 0;
        }
        int symbol = smallWords[lookup];
        if (symbol >= 14) {
            uint32_t mask = 0x800000u;
            while (symbol >= 14) {
                if (symbol < 0 || symbol >= 1024) {
                    ok = false;
                    return 0;
                }
                symbol = (bitStream & mask) == 0 ? parallel0[symbol] : parallel1[symbol];
                mask >>= 1;
            }
        }
        if (symbol < 0 || symbol >= 20) {
            ok = false;
            return 0;
        }
        LoadBits(smallBytes[symbol]);
        if (symbol == 0) {
            return 0;
        }
        if (symbol == 1) {
            return 2;
        }
        --symbol;
        uint32_t extra = bitStream >> (32 - symbol);
        LoadBits(symbol);
        return static_cast<int>(extra + (1u << symbol));
    }

    bool DecodeChunk(int chunkSize, std::vector<uint8_t>& dst) {
        int dstOffset = 0;
        auto emit = [&](uint8_t value) {
            if (dstOffset >= chunkSize || dst.size() >= 48u * 1024u * 1024u) {
                ok = false;
                return false;
            }
            chunk[dstOffset++] = value;
            return true;
        };
        --literalsToCopy;
        if (literalsToCopy >= 0) {
            while (literalsToCopy >= 0) {
                if (!emit(chunk[readOffset++ & 0x1fff])) {
                    return false;
                }
                if (dstOffset == chunkSize) {
                    dst.insert(dst.end(), chunk, chunk + chunkSize);
                    return true;
                }
                --literalsToCopy;
            }
        }
        while (dstOffset < chunkSize) {
            int16_t symbol = DecodeSymbol();
            if (!ok) {
                return false;
            }
            if (symbol <= 255) {
                if (!emit(static_cast<uint8_t>(symbol))) {
                    return false;
                }
                if (dstOffset == chunkSize) {
                    dst.insert(dst.end(), chunk, chunk + chunkSize);
                    return true;
                }
            } else {
                int distance = DecodeDistance();
                if (!ok) {
                    return false;
                }
                readOffset = static_cast<uint32_t>(dstOffset - distance - 1) & 0x1fffu;
                literalsToCopy = symbol - 254;
                while (literalsToCopy >= 0) {
                    if (!emit(chunk[readOffset++ & 0x1fff])) {
                        return false;
                    }
                    if (dstOffset == chunkSize) {
                        dst.insert(dst.end(), chunk, chunk + chunkSize);
                        return true;
                    }
                    --literalsToCopy;
                }
            }
        }
        if (dstOffset != chunkSize) {
            ok = false;
            return false;
        }
        dst.insert(dst.end(), chunk, chunk + chunkSize);
        return true;
    }

    bool Run(const uint8_t* compressed, int compressedLen, int outLen, std::vector<uint8_t>& dst) {
        src = compressed;
        srcLen = compressedLen;
        if (outLen <= 0) {
            return true;
        }
        if (outLen > 48 * 1024 * 1024) {
            return false;
        }
        LoadBits(0x20);
        int left = outLen;
        while (left != 0 && ok) {
            int chunkSize = left < kChunk ? left : kChunk;
            if (!DecodeChunk(chunkSize, dst)) {
                return false;
            }
            left -= chunkSize;
        }
        return ok && static_cast<int>(dst.size()) >= outLen;
    }
};

uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool Lz2kDecompress(const uint8_t* src, int srcLen, std::vector<uint8_t>& dst) {
    dst.clear();
    if (src == nullptr || srcLen < 12) {
        return false;
    }
    int pos = 0;
    while (pos + 12 <= srcLen) {
        if (std::memcmp(src + pos, "LZ2K", 4) != 0) {
            return false;
        }
        uint32_t size = U32(src + pos + 4);
        uint32_t zsize = U32(src + pos + 8);
        pos += 12;
        if (zsize > static_cast<uint32_t>(srcLen - pos) || size > 48u * 1024u * 1024u) {
            return false;
        }
        if (zsize == size) {
            dst.insert(dst.end(), src + pos, src + pos + size);
        } else {
            Lz2k decoder;
            if (!decoder.Run(src + pos, static_cast<int>(zsize), static_cast<int>(size), dst)) {
                return false;
            }
        }
        pos += static_cast<int>(zsize);
    }
    return pos == srcLen && !dst.empty();
}
