#include "assets.h"

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

bool NameOk(const std::string& name) {
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = name.substr(dot);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext == ".obj" || ext == ".ghg" || ext == ".gsc";
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

uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}

bool TagEq(const uint8_t* p, char a, char b, char c, char d) {
    return p[0] == static_cast<uint8_t>(a) && p[1] == static_cast<uint8_t>(b) && p[2] == static_cast<uint8_t>(c)
           && p[3] == static_cast<uint8_t>(d);
}

bool ParseMeshPayload(const uint8_t* data, int length, HostMesh& mesh) {
    if (length < 12) {
        return false;
    }
    uint32_t version = ReadU32(data);
    uint32_t verts = ReadU32(data + 4);
    uint32_t indices = ReadU32(data + 8);
    if (version != 1 || verts == 0 || verts > 200000 || indices < 3 || indices > 600000) {
        return false;
    }
    size_t need = 12u + static_cast<size_t>(verts) * 12u + static_cast<size_t>(indices) * 4u;
    if (need > static_cast<size_t>(length)) {
        return false;
    }
    const uint8_t* vptr = data + 12;
    mesh.xyz.resize(static_cast<size_t>(verts) * 3);
    for (uint32_t i = 0; i < verts * 3; ++i) {
        float value = 0.f;
        std::memcpy(&value, vptr + i * 4, 4);
        mesh.xyz[i] = value;
    }
    const uint8_t* iptr = vptr + verts * 12;
    mesh.indices.resize(indices);
    for (uint32_t i = 0; i < indices; ++i) {
        mesh.indices[i] = ReadU32(iptr + i * 4);
        if (mesh.indices[i] >= verts) {
            return false;
        }
    }
    return true;
}

bool ParseGhg(const uint8_t* data, int length, HostMesh& mesh) {
    for (int i = 0; i + 16 < length; ++i) {
        bool meshTag = TagEq(data + i, 'H', 'S', 'E', 'M') || TagEq(data + i, 'M', 'E', 'S', 'H');
        if (!meshTag) {
            continue;
        }
        if (ParseMeshPayload(data + i + 8, length - (i + 8), mesh)) {
            return true;
        }
    }
    return false;
}

bool Ingest(const std::string& name, const uint8_t* data, int size) {
    if (data == nullptr || size <= 0 || size > 48 * 1024 * 1024) {
        return false;
    }
    HostMesh mesh;
    mesh.name = name;
    bool ok = false;
    std::string lower = name;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".obj") {
        ok = ParseObj(reinterpret_cast<const char*>(data), size, mesh);
    } else {
        ok = ParseGhg(data, size, mesh);
    }
    if (!ok) {
        return false;
    }
    Bounds(mesh);
    g_meshes.push_back(std::move(mesh));
    return true;
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
