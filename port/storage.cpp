#include "host_state.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, js_save, (const char* hex), {
    try {
        localStorage.setItem("lcu-web-save", UTF8ToString(hex));
    } catch (err) {
    }
});
EM_JS(void, js_load, (char* dest, int maxChars), {
    let value = "";
    try {
        value = localStorage.getItem("lcu-web-save") || "";
    } catch (err) {
        value = "";
    }
    stringToUTF8(value, dest, maxChars);
});
#endif

namespace {

bool g_memory = false;
std::vector<unsigned char> g_blob;
const char* kPath = "lcu_web_save.bin";

void WriteHex(const unsigned char* data, int size, std::string& out) {
    static const char* kDigits = "0123456789abcdef";
    out.resize(static_cast<size_t>(size) * 2);
    for (int i = 0; i < size; ++i) {
        out[static_cast<size_t>(i) * 2] = kDigits[data[i] >> 4];
        out[static_cast<size_t>(i) * 2 + 1] = kDigits[data[i] & 0xf];
    }
}

int FromHex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

void HostStorageUseMemory(bool enabled) {
    g_memory = enabled;
    if (enabled) {
        g_blob.clear();
    }
}

void HostStorageWrite(const void* data, int size) {
    if (data == nullptr || size <= 0) {
        return;
    }
    const auto* bytes = static_cast<const unsigned char*>(data);
    g_blob.assign(bytes, bytes + size);
    if (g_memory) {
        return;
    }
#ifdef __EMSCRIPTEN__
    std::string hex;
    WriteHex(bytes, size, hex);
    js_save(hex.c_str());
#else
    std::ofstream out(kPath, std::ios::binary);
    if (out) {
        out.write(reinterpret_cast<const char*>(bytes), size);
    }
#endif
}

int HostStorageRead(void* data, int maxSize) {
    if (data == nullptr || maxSize <= 0) {
        return 0;
    }
    if (!g_blob.empty()) {
        int n = static_cast<int>(g_blob.size());
        if (n > maxSize) {
            n = maxSize;
        }
        std::memcpy(data, g_blob.data(), static_cast<size_t>(n));
        return n;
    }
    if (g_memory) {
        return 0;
    }
#ifdef __EMSCRIPTEN__
    char hex[512];
    hex[0] = '\0';
    js_load(hex, static_cast<int>(sizeof(hex)));
    int nibble = 0;
    int count = 0;
    auto* out = static_cast<unsigned char*>(data);
    int value = 0;
    for (int i = 0; hex[i] != '\0' && count < maxSize; ++i) {
        int digit = FromHex(hex[i]);
        if (digit < 0) {
            break;
        }
        value = (value << 4) | digit;
        ++nibble;
        if (nibble == 2) {
            out[count++] = static_cast<unsigned char>(value);
            nibble = 0;
            value = 0;
        }
    }
    return count;
#else
    std::ifstream in(kPath, std::ios::binary);
    if (!in) {
        return 0;
    }
    in.read(static_cast<char*>(data), maxSize);
    return static_cast<int>(in.gcount());
#endif
}
