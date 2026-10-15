#include "draw.h"
#include "host_state.h"
#include "numath/matrix.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

int g_width = 0;
int g_height = 0;
std::vector<uint8_t> g_color;
std::vector<float> g_depth;
NUMTX g_viewProj;
bool g_camera = false;

struct ClipV {
    float x, y, z, w;
};

void Xform(const NUMTX& m, float x, float y, float z, ClipV& out) {
    out.x = x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0];
    out.y = x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1];
    out.z = x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2];
    out.w = x * m.m[0][3] + y * m.m[1][3] + z * m.m[2][3] + m.m[3][3];
}

ClipV Lerp(const ClipV& a, const ClipV& b, float t) {
    ClipV o;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = a.z + (b.z - a.z) * t;
    o.w = a.w + (b.w - a.w) * t;
    return o;
}

void ClipNear(ClipV in[3], ClipV* out, int& count) {
    const float kNear = 0.05f;
    count = 0;
    for (int i = 0; i < 3; ++i) {
        ClipV cur = in[i];
        ClipV prev = in[(i + 2) % 3];
        bool curIn = cur.w >= kNear;
        bool prevIn = prev.w >= kNear;
        if (curIn != prevIn) {
            float t = (kNear - prev.w) / (cur.w - prev.w);
            out[count++] = Lerp(prev, cur, t);
        }
        if (curIn) {
            out[count++] = cur;
        }
    }
}

void PutPixel(int x, int y, float depth, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= g_width || y >= g_height) {
        return;
    }
    int index = y * g_width + x;
    if (depth >= g_depth[static_cast<size_t>(index)]) {
        return;
    }
    g_depth[static_cast<size_t>(index)] = depth;
    uint8_t* px = &g_color[static_cast<size_t>(index) * 4];
    px[0] = static_cast<uint8_t>(std::max(0, std::min(255, r)));
    px[1] = static_cast<uint8_t>(std::max(0, std::min(255, g)));
    px[2] = static_cast<uint8_t>(std::max(0, std::min(255, b)));
    px[3] = 255;
}

void Raster(const ClipV& a, const ClipV& b, const ClipV& c, int r, int g, int bcol) {
    if (a.w < 0.05f || b.w < 0.05f || c.w < 0.05f) {
        return;
    }
    auto project = [&](const ClipV& v, float& sx, float& sy, float& sz) {
        float inv = 1.f / v.w;
        float ndcX = v.x * inv;
        float ndcY = v.y * inv;
        sz = v.z * inv;
        sx = (ndcX * 0.5f + 0.5f) * static_cast<float>(g_width);
        sy = (0.5f - ndcY * 0.5f) * static_cast<float>(g_height);
    };
    float ax, ay, az, bx, by, bz, cx, cy, cz;
    project(a, ax, ay, az);
    project(b, bx, by, bz);
    project(c, cx, cy, cz);
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (area > -0.5f && area < 0.5f) {
        return;
    }
    float invArea = 1.f / area;
    int minX = static_cast<int>(std::floor(std::min(ax, std::min(bx, cx))));
    int maxX = static_cast<int>(std::ceil(std::max(ax, std::max(bx, cx))));
    int minY = static_cast<int>(std::floor(std::min(ay, std::min(by, cy))));
    int maxY = static_cast<int>(std::ceil(std::max(ay, std::max(by, cy))));
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX >= g_width) maxX = g_width - 1;
    if (maxY >= g_height) maxY = g_height - 1;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            float px = static_cast<float>(x) + 0.5f;
            float py = static_cast<float>(y) + 0.5f;
            float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) * invArea;
            float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) * invArea;
            float w2 = 1.f - w0 - w1;
            if (w0 >= 0.f && w1 >= 0.f && w2 >= 0.f) {
                float depth = w0 * az + w1 * bz + w2 * cz;
                PutPixel(x, y, depth, r, g, bcol);
            }
        }
    }
}

void DrawClipped(ClipV tri[3], int r, int g, int b) {
    ClipV poly[8];
    int count = 0;
    ClipNear(tri, poly, count);
    if (count < 3) {
        return;
    }
    for (int i = 1; i + 1 < count; ++i) {
        Raster(poly[0], poly[i], poly[i + 1], r, g, b);
    }
}

const char* Glyph(char ch) {
    switch (ch) {
    case 'A': return ".###.#...##...#######...##...##...#";
    case 'B': return "####.#...##...#####.#...##...#####.";
    case 'C': return ".###.#...##....#....#....#...#.###.";
    case 'D': return "####.#...##...##...##...##...#####.";
    case 'E': return "######....#....####.#....#....#####";
    case 'F': return "######....#....####.#....#....#....";
    case 'G': return ".###.#...##....#.####...##...#.###.";
    case 'H': return "#...##...##...#######...##...##...#";
    case 'I': return "###....#....#....#....#....#..###..";
    case 'J': return "..###...#....#....#.#..#.#..#..##..";
    case 'K': return "#...##..#.#.#..##...#.#..#..#.#...#";
    case 'L': return "#....#....#....#....#....#....#####";
    case 'M': return "#...###.###.#.##...##...##...##...#";
    case 'N': return "#...###..##.#.##..###...##...##...#";
    case 'O': return ".###.#...##...##...##...##...#.###.";
    case 'P': return "####.#...##...#####.#....#....#....";
    case 'R': return "####.#...##...#####.#.#..#..#.#...#";
    case 'S': return ".#####....#.....###.....#....#####.";
    case 'T': return "#####..#....#....#....#....#....#..";
    case 'U': return "#...##...##...##...##...##...#.###.";
    case 'V': return "#...##...##...##...##...#.#.#...#..";
    case 'W': return "#...##...##...##.#.##.#.###.###...#";
    case 'X': return "#...##...#.#.#...#...#.#.#...##...#";
    case 'Y': return "#...##...#.#.#...#....#....#....#..";
    case 'Z': return "#####...#...#...#...#....#....#####";
    case '0': return ".###.#..###.#.###..##...##...#.###.";
    case '1': return ".#...##.....#....#....#....#...###.";
    case '2': return ".###.#...#....#...#...#...#...#####";
    case '3': return ".###.#...#....#..##.....##...#.###.";
    case '4': return "...#...##.#..#.#####...#....#....#.";
    case '5': return "######....####.....#....##...#.###.";
    case '6': return ".###.#....#....####.#...##...#.###.";
    case '7': return "#####....#...#...#...#...#....#....";
    case '8': return ".###.#...##...#.###.#...##...#.###.";
    case '9': return ".###.#...##...#.####....#....#.###.";
    case '/': return "....#...#...#...#...#....#.........";
    case '-': return "...............#####...............";
    case '.': return "...........................#....#..";
    case ':': return ".......#....#.........#....#.......";
    case ' ': return "...................................";
    default: return nullptr;
    }
}

void DrawGlyph(int x, int y, char ch, int r, int g, int b) {
    const char* rows = Glyph(ch);
    if (rows == nullptr) {
        for (int py = 0; py < 8; ++py) {
            PutPixel(x + 1, y + py, -1.f, r, g, b);
        }
        return;
    }
    size_t glyphLen = std::strlen(rows);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            int index = row * 5 + col;
            if (static_cast<size_t>(index) < glyphLen && rows[index] == '#') {
                int px = x + col * 2;
                int py = y + row * 2;
                PutPixel(px, py, -1.f, r, g, b);
                PutPixel(px + 1, py, -1.f, r, g, b);
                PutPixel(px, py + 1, -1.f, r, g, b);
                PutPixel(px + 1, py + 1, -1.f, r, g, b);
            }
        }
    }
}

}  // namespace

void DrawBegin(int width, int height) {
    g_width = width;
    g_height = height;
    g_color.assign(static_cast<size_t>(width * height * 4), 0);
    g_depth.assign(static_cast<size_t>(width * height), 1.0e9f);
    g_camera = false;
    for (int y = 0; y < height; ++y) {
        float t = static_cast<float>(y) / static_cast<float>(height);
        int r = static_cast<int>(90 + t * 40);
        int g = static_cast<int>(170 - t * 40);
        int b = static_cast<int>(230 - t * 20);
        for (int x = 0; x < width; ++x) {
            uint8_t* px = &g_color[static_cast<size_t>(y * width + x) * 4];
            px[0] = static_cast<uint8_t>(r);
            px[1] = static_cast<uint8_t>(g);
            px[2] = static_cast<uint8_t>(b);
            px[3] = 255;
        }
    }
}

void DrawSetCamera(const NUVEC& eye, const NUVEC& target, float fovY) {
    NUMTX view;
    NUMTX proj;
    NUVEC up{0.f, 1.f, 0.f};
    NuMtxLookAtD3D(&view, &eye, &target, &up);
    float aspect = g_height == 0 ? 1.f : static_cast<float>(g_width) / static_cast<float>(g_height);
    NuMtxSetPerspectiveD3D(&proj, fovY, aspect, 0.1f, 250.f);
    NuMtxMul(&g_viewProj, &view, &proj);
    g_camera = true;
}

void DrawTriangle(const NUVEC& a, const NUVEC& b, const NUVEC& c, int r, int g, int bcol) {
    if (!g_camera) {
        return;
    }
    ClipV tri[3];
    Xform(g_viewProj, a.x, a.y, a.z, tri[0]);
    Xform(g_viewProj, b.x, b.y, b.z, tri[1]);
    Xform(g_viewProj, c.x, c.y, c.z, tri[2]);
    float nx = (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y);
    float ny = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
    float nz = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    float mag = std::sqrt(nx * nx + ny * ny + nz * nz);
    float shade = 0.72f;
    if (mag > 1.0e-4f) {
        shade = 0.68f + 0.32f * std::fabs(ny / mag);
    }
    DrawClipped(tri, static_cast<int>(r * shade), static_cast<int>(g * shade), static_cast<int>(bcol * shade));
}

void DrawBox(const NUVEC& mn, const NUVEC& mx, int r, int g, int b) {
    NUVEC p[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {3, 2, 6, 7}, {0, 3, 7, 4}, {1, 5, 6, 2}};
    for (const auto& face : faces) {
        DrawTriangle(p[face[0]], p[face[1]], p[face[2]], r, g, b);
        DrawTriangle(p[face[0]], p[face[2]], p[face[3]], r, g, b);
    }
}

void DrawMesh(const float* xyz, int vertexCount, const unsigned* indices, int indexCount, const NUVEC& pos, float yaw,
              float scale, int r, int g, int b) {
    if (xyz == nullptr || indices == nullptr || indexCount < 3) {
        return;
    }
    float cs = std::cos(yaw);
    float sn = std::sin(yaw);
    auto world = [&](unsigned index, NUVEC& out) {
        if (index >= static_cast<unsigned>(vertexCount)) {
            out = pos;
            return;
        }
        float x = xyz[index * 3] * scale;
        float y = xyz[index * 3 + 1] * scale;
        float z = xyz[index * 3 + 2] * scale;
        out.x = x * cs + z * sn + pos.x;
        out.y = y + pos.y;
        out.z = -x * sn + z * cs + pos.z;
    };
    for (int i = 0; i + 2 < indexCount; i += 3) {
        NUVEC va, vb, vc;
        world(indices[i], va);
        world(indices[i + 1], vb);
        world(indices[i + 2], vc);
        DrawTriangle(va, vb, vc, r, g, b);
    }
}

void DrawText(int x, int y, const char* text, int r, int g, int b) {
    if (text == nullptr) {
        return;
    }
    int cursor = x;
    for (const char* p = text; *p != '\0'; ++p) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        DrawGlyph(cursor, y, ch, r, g, b);
        cursor += 12;
    }
}

void DrawRect(int x, int y, int w, int h, int r, int g, int b, int a) {
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            if (xx < 0 || yy < 0 || xx >= g_width || yy >= g_height) {
                continue;
            }
            uint8_t* px = &g_color[static_cast<size_t>(yy * g_width + xx) * 4];
            if (a >= 255) {
                px[0] = static_cast<uint8_t>(r);
                px[1] = static_cast<uint8_t>(g);
                px[2] = static_cast<uint8_t>(b);
            } else {
                float t = static_cast<float>(a) / 255.f;
                px[0] = static_cast<uint8_t>(px[0] * (1.f - t) + r * t);
                px[1] = static_cast<uint8_t>(px[1] * (1.f - t) + g * t);
                px[2] = static_cast<uint8_t>(px[2] * (1.f - t) + b * t);
            }
            px[3] = 255;
        }
    }
}

void DrawFinish() {
    float bright = 0.25f + (static_cast<float>(HostVideoState().brightness) / 100.f) * 1.5f;
    if (bright < 0.05f) {
        bright = 0.05f;
    }
    for (size_t i = 0; i < g_color.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            int value = static_cast<int>(g_color[i + static_cast<size_t>(c)] * bright);
            if (value > 255) {
                value = 255;
            }
            g_color[i + static_cast<size_t>(c)] = static_cast<uint8_t>(value);
        }
    }
}

const unsigned char* DrawPixels() {
    return g_color.empty() ? nullptr : g_color.data();
}
int DrawWidth() {
    return g_width;
}
int DrawHeight() {
    return g_height;
}
