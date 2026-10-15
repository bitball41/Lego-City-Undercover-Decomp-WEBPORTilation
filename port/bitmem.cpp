#include "legoapi/BitArray.h"

#include <cstdlib>

namespace {
NuMemoryManager g_bitMem;
}

NuMemoryManager* NuMemoryManager::GetInstance() {
    return &g_bitMem;
}

void* NuMemoryManager::Alloc(int size) {
    if (size <= 0) {
        return nullptr;
    }
    return std::malloc(static_cast<size_t>(size));
}
