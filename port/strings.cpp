#include "script/common/SCmdParams.h"
#include "support/CommonString.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <unistd.h>

int NuStrCmp(const char* a, const char* b) {
    if (a == b) {
        return 0;
    }
    if (a == nullptr) {
        return -1;
    }
    if (b == nullptr) {
        return 1;
    }
    return std::strcmp(a, b);
}

int NuStrICmp(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return NuStrCmp(a, b);
    }
    while (*a && *b) {
        unsigned char ca = static_cast<unsigned char>(std::toupper(*a));
        unsigned char cb = static_cast<unsigned char>(std::toupper(*b));
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a == *b) {
        return 0;
    }
    return *a ? 1 : -1;
}

char* NuStrNCpy(char* dest, const char* src, long count) {
    if (dest == nullptr || count <= 0) {
        return dest;
    }
    long i = 0;
    if (src != nullptr) {
        for (; i < count - 1 && src[i] != '\0'; ++i) {
            dest[i] = src[i];
        }
    }
    dest[i] = '\0';
    return dest;
}

char NuToUpper(unsigned char c) {
    return static_cast<char>(std::toupper(c));
}

char NuToLower(unsigned char c) {
    return static_cast<char>(std::tolower(c));
}

uint32_t NuStrHashUpperCaseFNV1(const char* str, unsigned int seed) {
    uint32_t hash = seed;
    if (str == nullptr) {
        return hash;
    }
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != 0; ++p) {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = static_cast<unsigned char>(c - 'a' + 'A');
        }
        hash *= 16777619u;
        hash ^= c;
    }
    return hash;
}

int NuFileExistQuiet(const char* path) {
    if (path == nullptr) {
        return 0;
    }
    return access(path, F_OK) == 0 ? 1 : 0;
}

void NuFileSetCurrentDirectory(const char* path) {
    if (path != nullptr) {
        chdir(path);
    }
}

int ReadNextNumber(const char*& str, int max) {
    if (str == nullptr) {
        return 0;
    }
    while (*str == ' ' || *str == ',' || *str == '\t') {
        ++str;
    }
    int sign = 1;
    if (*str == '-') {
        sign = -1;
        ++str;
    }
    int value = 0;
    while (*str >= '0' && *str <= '9') {
        value = value * 10 + (*str - '0');
        ++str;
    }
    value *= sign;
    if (value > max) {
        value = max;
    }
    if (value < 0) {
        value = 0;
    }
    return value;
}

bool CommonString::m_CaseSensitiveCompare = false;

namespace {
NuConstStringManager g_strings;
}

NuConstStringManager* NuCore::GetConstStringManager() {
    return &g_strings;
}

void NuConstStringManager::Free(const char*) {}
