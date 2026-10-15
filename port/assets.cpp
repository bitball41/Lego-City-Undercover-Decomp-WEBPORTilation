#include "assets.h"

#include "tt_read.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<HostMesh> g_meshes;
std::vector<std::string> g_pending;
char g_note[64] = {};

void SetNote(const char* text) {
    if (text == nullptr) {
        g_note[0] = 0;
        return;
    }
    std::snprintf(g_note, sizeof(g_note), "%s", text);
}

bool NameOk(const std::string& name) {
    const char* ext = TtExtension(name);
    return std::strcmp(ext, ".obj") == 0 || std::strcmp(ext, ".ghg") == 0 || std::strcmp(ext, ".gsc") == 0
           || std::strcmp(ext, ".dat") == 0 || std::strcmp(ext, ".pak") == 0;
}

void Bounds(HostMesh& mesh) {
    mesh.minX = mesh.minY = mesh.minZ = 0.f;
    mesh.maxX = mesh.maxY = mesh.maxZ = 0.f;
    if (mesh.xyz.size() < 3) {
        return;
    }
    mesh.minX = mesh.maxX = mesh.xyz[0];
    mesh.minY = mesh.maxY = mesh.xyz[1];
    mesh.minZ = mesh.maxZ = mesh.xyz[2];
    for (size_t i = 0; i + 2 < mesh.xyz.size(); i += 3) {
        float x = mesh.xyz[i];
        float y = mesh.xyz[i + 1];
        float z = mesh.xyz[i + 2];
        if (x < mesh.minX) mesh.minX = x;
        if (y < mesh.minY) mesh.minY = y;
        if (z < mesh.minZ) mesh.minZ = z;
        if (x > mesh.maxX) mesh.maxX = x;
        if (y > mesh.maxY) mesh.maxY = y;
        if (z > mesh.maxZ) mesh.maxZ = z;
    }
}

bool ParseObj(const char* text, int length, HostMesh& mesh) {
    std::vector<float> verts;
    const char* p = text;
    const char* end = text + length;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
            ++p;
        }
        if (p >= end) {
            break;
        }
        const char* line = p;
        while (p < end && *p != '\n') {
            ++p;
        }
        if (line + 2 <= p && line[0] == 'v' && line[1] == ' ') {
            float x = 0, y = 0, z = 0;
            std::sscanf(line + 1, "%f %f %f", &x, &y, &z);
            verts.push_back(x);
            verts.push_back(y);
            verts.push_back(z);
        } else if (line + 2 <= p && line[0] == 'f' && line[1] == ' ') {
            int ids[8];
            int count = 0;
            const char* q = line + 2;
            const char* stop = p;
            while (q < stop && count < 8) {
                while (q < stop && (*q == ' ' || *q == '\t')) {
                    ++q;
                }
                if (q >= stop || *q == '\n' || *q == '\r') {
                    break;
                }
                int id = 0;
                bool any = false;
                while (q < stop && *q >= '0' && *q <= '9') {
                    id = id * 10 + (*q - '0');
                    ++q;
                    any = true;
                }
                if (!any) {
                    break;
                }
                ids[count++] = id;
                while (q < stop && *q != ' ' && *q != '\t' && *q != '\n') {
                    ++q;
                }
            }
            for (int i = 1; i + 1 < count; ++i) {
                int tri[3] = {ids[0], ids[i], ids[i + 1]};
                for (int corner : tri) {
                    int index = corner - 1;
                    if (index < 0 || index * 3 + 2 >= static_cast<int>(verts.size())) {
                        continue;
                    }
                    mesh.indices.push_back(static_cast<unsigned>(mesh.xyz.size() / 3));
                    mesh.xyz.push_back(verts[static_cast<size_t>(index) * 3]);
                    mesh.xyz.push_back(verts[static_cast<size_t>(index) * 3 + 1]);
                    mesh.xyz.push_back(verts[static_cast<size_t>(index) * 3 + 2]);
                }
            }
        }
        if (p < end && *p == '\n') {
            ++p;
        }
    }
    return mesh.indices.size() >= 3;
}

bool IngestMesh(const std::string& name, const uint8_t* data, int size) {
    if (data == nullptr || size <= 0 || size > 48 * 1024 * 1024) {
        return false;
    }
    HostMesh mesh;
    mesh.name = name;
    char note[64];
    note[0] = 0;
    bool ok = false;
    const char* ext = TtExtension(name);
    if (std::strcmp(ext, ".obj") == 0) {
        ok = ParseObj(reinterpret_cast<const char*>(data), size, mesh);
    } else {
        ok = TtReadNu20(data, size, mesh, note, static_cast<int>(sizeof(note)));
        if (!ok && note[0] != 0) {
            SetNote(note);
        }
    }
    if (!ok || mesh.indices.size() < 3) {
        return false;
    }
    Bounds(mesh);
    g_meshes.push_back(std::move(mesh));
    return true;
}

bool IngestArchive(const uint8_t* data, int size) {
    if (data == nullptr || size <= 0 || size > 256 * 1024 * 1024) {
        return false;
    }
    TtArchive archive;
    if (!TtOpenArchive(data, size, archive)) {
        if (!archive.kind.empty()) {
            SetNote(archive.kind.c_str());
        }
        return false;
    }
    int got = 0;
    int seen = 0;
    for (const TtFile& file : archive.files) {
        const char* ext = TtExtension(file.name);
        if (std::strcmp(ext, ".obj") != 0 && std::strcmp(ext, ".ghg") != 0 && std::strcmp(ext, ".gsc") != 0) {
            continue;
        }
        if (++seen > 80) {
            break;
        }
        std::vector<uint8_t> bytes;
        if (!TtExtract(data, size, file, bytes)) {
            continue;
        }
        if (IngestMesh(file.name, bytes.data(), static_cast<int>(bytes.size()))) {
            ++got;
        }
    }
    if (got == 0) {
        char note[64];
        std::snprintf(note, sizeof(note), "%s %d FILES", archive.kind.c_str(), static_cast<int>(archive.files.size()));
        SetNote(note);
        return false;
    }
    return true;
}

bool Ingest(const std::string& name, const uint8_t* data, int size) {
    const char* ext = TtExtension(name);
    if (std::strcmp(ext, ".obj") == 0) {
        return IngestMesh(name, data, size);
    }
    if (std::strcmp(ext, ".dat") == 0 || std::strcmp(ext, ".pak") == 0) {
        return IngestArchive(data, size);
    }
    if (std::strcmp(ext, ".ghg") == 0 || std::strcmp(ext, ".gsc") == 0) {
        if (IngestMesh(name, data, size)) {
            return true;
        }
        return IngestArchive(data, size);
    }
    if (IngestMesh(name, data, size)) {
        return true;
    }
    return IngestArchive(data, size);
}

}  // namespace

int AssetsMountDirectory(const char* path) {
    if (path == nullptr) {
        return 0;
    }
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        std::string full = entry.path().string();
        if (NameOk(full)) {
            g_pending.push_back(full);
        }
    }
    return static_cast<int>(g_pending.size());
}

int AssetsSubmit(const char* name, const uint8_t* data, int size) {
    std::string label = name != nullptr ? name : "asset";
    return Ingest(label, data, size) ? 1 : 0;
}

void AssetsClear() {
    g_meshes.clear();
    g_pending.clear();
    g_note[0] = 0;
}

void AssetsPump() {
    if (g_pending.empty()) {
        return;
    }
    std::string path = g_pending.back();
    g_pending.pop_back();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return;
    }
    in.seekg(0, std::ios::end);
    std::streamoff length = in.tellg();
    if (length <= 0 || length > 48 * 1024 * 1024) {
        return;
    }
    in.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    in.read(reinterpret_cast<char*>(bytes.data()), length);
    Ingest(path, bytes.data(), static_cast<int>(bytes.size()));
}

int AssetsPending() {
    return static_cast<int>(g_pending.size());
}

int AssetsLoaded() {
    return static_cast<int>(g_meshes.size());
}

const HostMesh* AssetsAt(int index) {
    if (index < 0 || index >= static_cast<int>(g_meshes.size())) {
        return nullptr;
    }
    return &g_meshes[static_cast<size_t>(index)];
}

const char* AssetsStatus() {
    return g_note;
}
