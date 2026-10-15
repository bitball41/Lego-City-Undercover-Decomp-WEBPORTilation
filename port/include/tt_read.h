#pragma once

#include "assets.h"

#include <cstdint>
#include <string>
#include <vector>

struct TtFile {
    std::string name;
    uint64_t offset = 0;
    uint32_t size = 0;
    uint32_t zsize = 0;
};

struct TtArchive {
    std::string kind;
    std::vector<TtFile> files;
};

bool TtOpenArchive(const uint8_t* data, int size, TtArchive& out);
bool TtExtract(const uint8_t* data, int size, const TtFile& file, std::vector<uint8_t>& out);
bool TtReadNu20(const uint8_t* data, int size, HostMesh& mesh, char* note, int noteLen);
const char* TtExtension(const std::string& name);
