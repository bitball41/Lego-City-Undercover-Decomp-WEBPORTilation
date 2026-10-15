#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HostMesh {
    std::string name;
    std::vector<float> xyz;
    std::vector<unsigned> indices;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};

int AssetsMountDirectory(const char* path);
int AssetsSubmit(const char* name, const uint8_t* data, int size);
void AssetsClear();
void AssetsPump();
int AssetsPending();
int AssetsLoaded();
const HostMesh* AssetsAt(int index);
