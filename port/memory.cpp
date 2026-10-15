#include "kestrel/NuMemory.h"
#include "kestrel/NuMemoryPool.h"

#include <cstdint>
#include <cstdlib>

namespace {

NuMemory g_memory;
NuMemoryManager g_threadMem;
NuMemoryPool g_scriptPool;

size_t AlignUp(size_t value, size_t alignment) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

}  // namespace

NuMemoryPool* ScriptMemory::m_Pool = &g_scriptPool;

NuMemory* NuMemoryGet() {
    return &g_memory;
}

NuMemoryManager* NuMemory::GetThreadMem() {
    return &g_threadMem;
}

void* NuMemoryManager::_BlockAlloc(size_t size, size_t alignment, unsigned int, const char*, unsigned short) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }
    size_t bytes = size == 0 ? alignment : AlignUp(size, alignment);
    void* raw = nullptr;
    if (posix_memalign(&raw, alignment, bytes) != 0) {
        return nullptr;
    }
    return raw;
}

void NuMemoryManager::BlockFree(void* ptr, unsigned int) {
    std::free(ptr);
}

void* NuMemoryPool::_PoolBlockAlloc(size_t size, const char*) {
    if (size == 0) {
        size = 1;
    }
    return std::malloc(size);
}

void NuMemoryPool::PoolBlockFree(void* ptr, size_t) {
    std::free(ptr);
}
