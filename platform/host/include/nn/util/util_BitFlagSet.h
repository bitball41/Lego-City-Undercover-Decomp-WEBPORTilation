#pragma once

#include <nn/types.h>
#include <type_traits>

namespace nn::util {

template <int N, typename Tag>
struct BitFlagSet {
    typedef typename std::conditional<N <= 32, uint32_t, uint64_t>::type StorageT;
    static const int StorageBitCount = 8 * sizeof(StorageT);
    static const int StorageCount = (N + StorageBitCount - 1) / StorageBitCount;
    StorageT _storage[StorageCount]{};

    bool Test(int index) const {
        if (index < 0 || index >= N) {
            return false;
        }
        return (_storage[index / StorageBitCount] & (StorageT(1) << (index % StorageBitCount))) != 0;
    }

    BitFlagSet& Set(int index, bool on) {
        if (index < 0 || index >= N) {
            return *this;
        }
        StorageT bit = StorageT(1) << (index % StorageBitCount);
        if (on) {
            _storage[index / StorageBitCount] |= bit;
        } else {
            _storage[index / StorageBitCount] &= ~bit;
        }
        return *this;
    }

    bool IsAnyOn() const {
        for (int i = 0; i < StorageCount; ++i) {
            if (_storage[i] != 0) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace nn::util
