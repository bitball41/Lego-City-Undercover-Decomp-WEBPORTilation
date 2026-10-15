#include "tt_read.h"

#include "lz2k.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <zlib.h>

namespace {

uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t Be32p(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8)
           | static_cast<uint32_t>(p[3]);
}

float HalfToFloat(uint16_t h) {
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t out = 0;
    if (exp == 0) {
        if (mant == 0) {
            out = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400) == 0) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3ff;
            out = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        out = sign | 0x7f800000u | (mant << 13);
    } else {
        out = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float value = 0.f;
    std::memcpy(&value, &out, 4);
    return value;
}

struct Cursor {
    const uint8_t* b = nullptr;
    int n = 0;
    int p = 0;
    bool be = false;
    bool ok = true;

    bool Need(int k) {
        if (!ok || k < 0 || p < 0 || p > n || k > n - p) {
            ok = false;
            return false;
        }
        return true;
    }

    uint32_t U32() {
        if (!Need(4)) {
            return 0;
        }
        uint32_t v = be ? Be32p(b + p) : Le32(b + p);
        p += 4;
        return v;
    }

    uint64_t U64() {
        uint32_t hi = U32();
        uint32_t lo = U32();
        if (!be) {
            return (static_cast<uint64_t>(lo) << 32) | hi;
        }
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }

    uint16_t U16() {
        if (!Need(2)) {
            return 0;
        }
        uint16_t v = be ? static_cast<uint16_t>((b[p] << 8) | b[p + 1]) : static_cast<uint16_t>(b[p] | (b[p + 1] << 8));
        p += 2;
        return v;
    }

    int16_t I16() {
        return static_cast<int16_t>(U16());
    }

    int32_t I32() {
        return static_cast<int32_t>(U32());
    }

    float F32() {
        uint32_t u = U32();
        float v = 0.f;
        std::memcpy(&v, &u, 4);
        return v;
    }

    float Half() {
        return HalfToFloat(U16());
    }

    void Skip(int k) {
        if (!Need(k)) {
            return;
        }
        p += k;
    }

    uint8_t Peek() {
        if (!ok || p < 0 || p >= n) {
            ok = false;
            return 0;
        }
        return b[p];
    }

    uint32_t PeekU32() {
        if (!ok || p < 0 || p + 4 > n) {
            ok = false;
            return 0;
        }
        return be ? Be32p(b + p) : Le32(b + p);
    }

    std::string Str(int at) {
        if (at < 0 || at >= n) {
            ok = false;
            return {};
        }
        std::string out;
        for (int i = at; i < n && static_cast<int>(out.size()) < 512; ++i) {
            if (b[i] == 0) {
                return out;
            }
            out.push_back(static_cast<char>(b[i]));
        }
        ok = false;
        return {};
    }
};

uint32_t HashTt(std::string name) {
    if (!name.empty() && (name[0] == '\\' || name[0] == '/')) {
        name.erase(name.begin());
    }
    uint32_t crc = 0x811c9dc5u;
    for (unsigned char c : name) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<unsigned char>(c - 'a' + 'A');
        }
        if (c == '/') {
            c = '\\';
        }
        crc ^= c;
        crc *= 0x199933u;
    }
    return crc;
}

bool RangeOk(uint64_t offset, uint32_t bytes, int size) {
    if (bytes > 64u * 1024u * 1024u) {
        return false;
    }
    return offset <= static_cast<uint64_t>(size) && bytes <= static_cast<uint64_t>(size) - offset;
}

void AssignNames(std::vector<TtFile>& files, const std::vector<std::string>& produced, const std::vector<uint32_t>& crcs,
                 bool haveCrc) {
    std::vector<char> used(files.size(), 0);
    for (size_t i = 0; i < produced.size() && i < files.size(); ++i) {
        int idx = -1;
        if (haveCrc) {
            uint32_t hash = HashTt(produced[i]);
            for (size_t j = 0; j < files.size(); ++j) {
                if (!used[j] && crcs[j] == hash) {
                    idx = static_cast<int>(j);
                    break;
                }
            }
        }
        if (idx < 0) {
            for (size_t j = 0; j < files.size(); ++j) {
                if (!used[j]) {
                    idx = static_cast<int>(j);
                    break;
                }
            }
        }
        if (idx < 0) {
            break;
        }
        used[static_cast<size_t>(idx)] = 1;
        files[static_cast<size_t>(idx)].name = produced[i];
    }
}

bool OpenPak(const uint8_t* data, int size, TtArchive& out) {
    if (size < 24 || Le32(data) != 0x1234567Au) {
        return false;
    }
    uint32_t files = Le32(data + 4);
    if (files == 0 || files > 400000u) {
        return false;
    }
    uint64_t table = 24ull + static_cast<uint64_t>(files) * 28ull;
    if (table > static_cast<uint64_t>(size)) {
        return false;
    }
    out.kind = "pak";
    out.files.reserve(files);
    for (uint32_t i = 0; i < files; ++i) {
        const uint8_t* e = data + 24 + i * 28;
        uint32_t nameOff = Le32(e);
        uint32_t offset = Le32(e + 4);
        uint32_t fsize = Le32(e + 8);
        TtFile file;
        file.offset = offset;
        file.size = fsize;
        file.zsize = fsize;
        if (nameOff < static_cast<uint32_t>(size)) {
            Cursor reader{data, size, 0, false, true};
            file.name = reader.Str(static_cast<int>(nameOff));
        }
        if (file.name.empty()) {
            file.name = "file";
        }
        if (RangeOk(file.offset, file.size, size)) {
            out.files.push_back(std::move(file));
        }
    }
    return !out.files.empty();
}

bool OpenOldDat(const uint8_t* data, int size, const uint8_t* info, int infoSize, TtArchive& out) {
    if (infoSize < 12) {
        return false;
    }
    int32_t type = static_cast<int32_t>(Le32(info));
    uint32_t files = Le32(info + 4);
    if (files == 0 || files > 400000u || type > 16 || type < -32) {
        return false;
    }
    if (8 + static_cast<uint64_t>(files) * 16ull + 4ull > static_cast<uint64_t>(infoSize)) {
        return false;
    }
    Cursor dir{info, infoSize, 0, false, true};
    int nameInfo = 8 + static_cast<int>(files) * 16;
    dir.p = nameInfo;
    uint32_t nameCount = dir.U32();
    int nameCursor = dir.p;
    int field = type <= -5 ? 12 : 8;
    if (nameCount > 1000000u) {
        return false;
    }
    int stringsAt = nameCursor + static_cast<int>(nameCount) * field;
    if (stringsAt < 0 || stringsAt + 4 > infoSize) {
        return false;
    }
    uint32_t crcRel = Le32(info + stringsAt);
    int stringBase = stringsAt + 4;
    int crcAt = stringBase + static_cast<int>(crcRel);
    std::vector<std::string> stored;
    std::vector<std::string> produced;
    produced.reserve(files);
    std::string fullPath;
    dir.ok = true;
    for (uint32_t i = 0; i < files; ++i) {
        int16_t next = 1;
        std::string name;
        int guard = 0;
        do {
            if (++guard > 100000 || nameCursor + field > infoSize) {
                return false;
            }
            dir.p = nameCursor;
            next = dir.I16();
            int16_t prev = dir.I16();
            int32_t off = dir.I32();
            if (field == 12) {
                dir.U32();
            }
            nameCursor = dir.p;
            if (!dir.ok) {
                return false;
            }
            if (off >= 0) {
                name = dir.Str(stringBase + off);
                if (!dir.ok) {
                    return false;
                }
            } else {
                name.clear();
            }
            if (!name.empty() && static_cast<unsigned char>(name[0]) >= 0xf0) {
                name.clear();
            }
            if (prev != 0) {
                if (prev > 0 && prev < static_cast<int>(stored.size())) {
                    fullPath = stored[static_cast<size_t>(prev)];
                } else {
                    fullPath.clear();
                }
            }
            stored.push_back(fullPath);
            if (next > 0 && !name.empty()) {
                if (!fullPath.empty() && fullPath.back() != '\\' && fullPath.back() != '/') {
                    fullPath.push_back('\\');
                }
                fullPath += name;
                fullPath.push_back('\\');
            }
        } while (next > 0);
        produced.push_back(fullPath + name);
    }
    bool haveCrc = crcAt >= 0 && crcAt + static_cast<int>(files) * 4 < infoSize;
    std::vector<uint32_t> crcs(files, 0);
    if (haveCrc) {
        for (uint32_t i = 0; i < files; ++i) {
            crcs[i] = Le32(info + crcAt + static_cast<int>(i) * 4);
        }
    }
    out.kind = "dat";
    out.files.resize(files);
    for (uint32_t i = 0; i < files; ++i) {
        const uint8_t* rec = info + 8 + i * 16;
        uint32_t offset = Le32(rec);
        uint32_t zsize = Le32(rec + 4);
        uint32_t fsize = Le32(rec + 8);
        uint8_t offset2 = rec[15];
        if (type != -1) {
            offset = (offset << 8) | offset2;
        } else {
            offset += offset2;
        }
        out.files[i].offset = offset;
        out.files[i].zsize = zsize;
        out.files[i].size = fsize;
    }
    AssignNames(out.files, produced, crcs, haveCrc);
    out.files.erase(std::remove_if(out.files.begin(), out.files.end(),
                                   [&](const TtFile& file) {
                                       uint32_t bytes = file.zsize != 0 ? file.zsize : file.size;
                                       return !RangeOk(file.offset, bytes, size);
                                   }),
                    out.files.end());
    return !out.files.empty();
}

bool OpenCc4(const uint8_t* data, int size, const uint8_t* info, int infoSize, TtArchive& out) {
    Cursor dir{info, infoSize, 0, true, true};
    dir.U32();
    if (!dir.Need(8) || std::memcmp(info + dir.p, ".CC40TAD", 8) != 0) {
        return false;
    }
    dir.p += 8;
    int32_t type = dir.I32();
    uint32_t ver = dir.U32();
    uint32_t files = dir.U32();
    uint32_t names = dir.U32();
    uint32_t namesSize = dir.U32();
    if (!dir.ok || files == 0 || files > 400000u || names > 1000000u) {
        return false;
    }
    int namesOff = dir.p;
    dir.Skip(static_cast<int>(namesSize));
    dir.U32();
    std::vector<std::string> folders(names);
    std::vector<std::string> produced;
    uint32_t myId = 0;
    for (uint32_t i = 0; i < names && dir.ok; ++i) {
        uint32_t nameOff = dir.U32();
        uint16_t folderId = dir.U16();
        if (ver >= 2) {
            dir.U16();
        }
        dir.I16();
        uint16_t fileId = dir.U16();
        if (!dir.ok) {
            return false;
        }
        if (nameOff == 0xffffffffu) {
            continue;
        }
        std::string name = dir.Str(namesOff + static_cast<int>(nameOff));
        if (!dir.ok) {
            return false;
        }
        if (i + 1 == names) {
            fileId = static_cast<uint16_t>(myId);
        }
        std::string parent = folderId < folders.size() ? folders[folderId] : std::string();
        if (!parent.empty() && parent.back() != '\\') {
            parent.push_back('\\');
        }
        name = parent + name;
        if (fileId != 0) {
            produced.push_back(name);
            ++myId;
        } else if (i < folders.size()) {
            folders[i] = name;
        }
    }
    int32_t type2 = dir.I32();
    uint32_t files2 = dir.U32();
    if (!dir.ok || files2 == 0 || files2 > 400000u) {
        return false;
    }
    out.kind = "cc4";
    out.files.resize(files2);
    for (uint32_t i = 0; i < files2; ++i) {
        uint64_t offset = type2 <= -11 ? dir.U64() : dir.U32();
        uint32_t zsize = dir.U32();
        uint32_t fsize = dir.U32();
        if (type2 <= -10) {
            fsize &= 0x7fffffffu;
        } else {
            dir.Skip(1);
            dir.Skip(2);
            uint8_t offset2 = 0;
            if (dir.Need(1)) {
                offset2 = info[dir.p];
                dir.p += 1;
            }
            offset = (offset << 8) | offset2;
        }
        if (!dir.ok) {
            return false;
        }
        out.files[i].offset = offset;
        out.files[i].zsize = zsize;
        out.files[i].size = fsize;
    }
    bool haveCrc = false;
    std::vector<uint32_t> crcs(files2, 0);
    if (dir.p >= 0 && dir.p + static_cast<int>(files2) * 4 < infoSize) {
        haveCrc = true;
        for (uint32_t i = 0; i < files2; ++i) {
            crcs[i] = dir.U32();
        }
    }
    if (produced.size() == files2 && !haveCrc) {
        for (uint32_t i = 0; i < files2; ++i) {
            out.files[i].name = produced[i];
        }
    } else {
        AssignNames(out.files, produced, crcs, haveCrc);
    }
    (void)type;
    (void)data;
    out.files.erase(std::remove_if(out.files.begin(), out.files.end(),
                                   [&](const TtFile& file) {
                                       uint32_t bytes = file.zsize != 0 ? file.zsize : file.size;
                                       return !RangeOk(file.offset, bytes, size);
                                   }),
                    out.files.end());
    return !out.files.empty();
}

bool InflateRaw(const uint8_t* src, int srcLen, std::vector<uint8_t>& dst) {
    if (src == nullptr || srcLen <= 0) {
        return false;
    }
    z_stream strm{};
    if (inflateInit2(&strm, -15) != Z_OK) {
        return false;
    }
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src));
    strm.avail_in = static_cast<uInt>(srcLen);
    uint8_t buf[16384];
    bool good = false;
    while (true) {
        strm.next_out = buf;
        strm.avail_out = sizeof(buf);
        int ret = inflate(&strm, Z_NO_FLUSH);
        size_t have = sizeof(buf) - strm.avail_out;
        if (dst.size() + have > 48u * 1024u * 1024u) {
            inflateEnd(&strm);
            return false;
        }
        dst.insert(dst.end(), buf, buf + have);
        if (have > 0) {
            good = true;
        }
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret != Z_OK) {
            inflateEnd(&strm);
            return false;
        }
    }
    inflateEnd(&strm);
    return good;
}

bool UnpackDflt(const uint8_t* src, int srcLen, std::vector<uint8_t>& dst) {
    int pos = 0;
    while (pos + 12 <= srcLen) {
        if (std::memcmp(src + pos, "DFLT", 4) != 0) {
            return false;
        }
        uint32_t zsize = Le32(src + pos + 4);
        uint32_t size = Le32(src + pos + 8);
        pos += 12;
        if (zsize > static_cast<uint32_t>(srcLen - pos)) {
            return false;
        }
        if (zsize == size) {
            dst.insert(dst.end(), src + pos, src + pos + zsize);
        } else if (!InflateRaw(src + pos, static_cast<int>(zsize), dst)) {
            return false;
        }
        pos += static_cast<int>(zsize);
        if (dst.size() > 48u * 1024u * 1024u) {
            return false;
        }
    }
    return pos == srcLen && !dst.empty();
}

struct Vtx {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    bool has = false;
};

struct VList {
    std::vector<Vtx> verts;
};

struct IList {
    std::vector<uint16_t> idx;
};

struct MeshReader {
    Cursor c;
    int nextRef = 5;
    std::vector<VList> vlists = std::vector<VList>(8);
    std::vector<IList> ilists = std::vector<IList>(8);
    HostMesh* mesh = nullptr;

    VList* ListV(int id) {
        if (id < 0 || id >= static_cast<int>(vlists.size()) || vlists[static_cast<size_t>(id)].verts.empty()) {
            return nullptr;
        }
        return &vlists[static_cast<size_t>(id)];
    }

    IList* ListI(int id) {
        if (id < 0 || id >= static_cast<int>(ilists.size()) || ilists[static_cast<size_t>(id)].idx.empty()) {
            return nullptr;
        }
        return &ilists[static_cast<size_t>(id)];
    }

    void Grow(int id) {
        if (id + 1 > static_cast<int>(vlists.size())) {
            if (id > 100000) {
                c.ok = false;
                return;
            }
            vlists.resize(static_cast<size_t>(id) + 1);
            ilists.resize(static_cast<size_t>(id) + 1);
        }
    }

    bool ReadPosition(int type, float& x, float& y, float& z) {
        if (type == 2) {
            x = c.F32();
            y = c.F32();
            z = 0.f;
        } else if (type == 3) {
            x = c.F32();
            y = c.F32();
            z = c.F32();
        } else if (type == 4) {
            x = c.F32();
            y = c.F32();
            z = c.F32();
            c.Half();
            c.Skip(2);
        } else if (type == 5) {
            x = c.Half();
            y = c.Half();
            z = 0.f;
        } else if (type == 6) {
            x = c.Half();
            y = c.Half();
            z = c.Half();
            c.Half();
        } else if (type == 7 || type == 8 || type == 9) {
            c.Skip(4);
            return false;
        } else {
            c.ok = false;
            return false;
        }
        return c.ok;
    }

    void SkipValue(int type) {
        float x, y, z;
        ReadPosition(type, x, y, z);
    }

    VList ReadVertices(int count) {
        VList list;
        if (count <= 0 || count > 200000) {
            c.ok = false;
            return list;
        }
        int decl[16];
        int declN = 0;
        auto take = [&](bool live) {
            int type = 0;
            if (live && c.ok && c.p >= 0 && c.p < c.n) {
                type = c.b[c.p];
            }
            if (live && type != 0 && declN < 16) {
                decl[declN++] = type;
            }
            c.Skip(2);
        };
        take(true);
        take(true);
        take(true);
        take(true);
        take(true);
        take(true);
        c.Skip(2);
        take(true);
        c.Skip(2);
        take(true);
        take(true);
        take(true);
        take(true);
        c.Skip(6);
        list.verts.resize(static_cast<size_t>(count));
        for (int v = 0; v < count && c.ok; ++v) {
            bool got = false;
            float x = 0, y = 0, z = 0;
            for (int d = 0; d < declN && c.ok; ++d) {
                float px, py, pz;
                bool pos = ReadPosition(decl[d], px, py, pz);
                if (!got && pos) {
                    x = px;
                    y = py;
                    z = pz;
                    got = true;
                }
            }
            list.verts[static_cast<size_t>(v)] = Vtx{x, y, z, got};
        }
        return list;
    }

    int VertexRef() {
        if (c.Peek() == 192) {
            c.Skip(2);
            int id = c.I16();
            c.Skip(8);
            return id;
        }
        c.Skip(8);
        int count = static_cast<int>(c.U32());
        VList list = ReadVertices(count);
        c.Skip(4);
        int id = nextRef++;
        Grow(id);
        if (c.ok) {
            vlists[static_cast<size_t>(id)] = std::move(list);
        }
        return id;
    }

    int IndexRef() {
        if (c.Peek() == 192) {
            c.Skip(2);
            int id = c.I16();
            c.Skip(4);
            return id;
        }
        c.Skip(8);
        int count = static_cast<int>(c.U32());
        c.Skip(4);
        if (count < 0 || count > 600000) {
            c.ok = false;
            return -1;
        }
        IList list;
        list.idx.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            list.idx[static_cast<size_t>(i)] = c.U16();
        }
        int id = nextRef++;
        Grow(id);
        if (c.ok) {
            ilists[static_cast<size_t>(id)] = std::move(list);
        }
        return id;
    }

    void SkipRelative4() {
        c.Skip(4);
        int groups = 1;
        int guard = 0;
        while (c.ok && c.PeekU32() != 0) {
            c.Skip(8);
            ++groups;
            if (++guard > 10000) {
                c.ok = false;
                return;
            }
        }
        c.Skip(4);
        for (int i = 0; i < groups && c.ok; ++i) {
            int count = static_cast<int>(c.U32());
            if (count < 0 || count > 200000) {
                c.ok = false;
                return;
            }
            if (count != 0) {
                c.Skip(count * 12);
            }
            c.Skip(6);
            int tuples = static_cast<int>(c.U32());
            c.Skip(4);
            if (tuples < 0 || tuples > 200000) {
                c.ok = false;
                return;
            }
            c.Skip(tuples * 4);
        }
    }

    void Emit(const std::vector<int>& refs, int iref, int offI, int numI, int offV, int numV) {
        if (!c.ok || mesh == nullptr || refs.empty() || numV <= 0 || numI < 3) {
            return;
        }
        VList* verts = nullptr;
        for (int id : refs) {
            VList* candidate = ListV(id);
            if (candidate != nullptr && !candidate->verts.empty() && candidate->verts[0].has) {
                verts = candidate;
                break;
            }
        }
        IList* indices = ListI(iref);
        if (verts == nullptr || indices == nullptr) {
            return;
        }
        if (offV < 0 || numV < 0 || offV > static_cast<int>(verts->verts.size())
            || numV > static_cast<int>(verts->verts.size()) - offV) {
            return;
        }
        if (offI < 0 || offI > static_cast<int>(indices->idx.size())
            || numI > static_cast<int>(indices->idx.size()) - offI) {
            return;
        }
        if (mesh->xyz.size() / 3 + static_cast<size_t>(numV) > 400000u) {
            return;
        }
        unsigned base = static_cast<unsigned>(mesh->xyz.size() / 3);
        for (int v = 0; v < numV; ++v) {
            const Vtx& vert = verts->verts[static_cast<size_t>(offV + v)];
            mesh->xyz.push_back(vert.x);
            mesh->xyz.push_back(vert.y);
            mesh->xyz.push_back(vert.z);
        }
        for (int i = 0; i + 2 < numI; i += 3) {
            unsigned local[3];
            bool good = true;
            for (int k = 0; k < 3; ++k) {
                unsigned raw = indices->idx[static_cast<size_t>(offI + i + k)];
                int obj = static_cast<int>(raw) - numV;
                int index = -1;
                if (obj >= 1 && obj <= numV) {
                    index = obj - 1;
                } else if (raw < static_cast<unsigned>(numV)) {
                    index = static_cast<int>(raw);
                }
                if (index < 0) {
                    good = false;
                    break;
                }
                local[k] = base + static_cast<unsigned>(index);
            }
            if (good) {
                mesh->indices.push_back(local[0]);
                mesh->indices.push_back(local[1]);
                mesh->indices.push_back(local[2]);
            }
        }
    }

    void PartV4() {
        int lists = static_cast<int>(c.U32());
        if (lists < 1 || lists > 8) {
            c.ok = false;
            return;
        }
        std::vector<int> refs;
        for (int i = 0; i < lists; ++i) {
            refs.push_back(VertexRef());
        }
        c.Skip(4);
        int iref = IndexRef();
        int offI = static_cast<int>(c.U32());
        int numI = static_cast<int>(c.U32());
        int offV = static_cast<int>(c.U32());
        if (c.U16() != 0) {
            c.ok = false;
            return;
        }
        int numV = static_cast<int>(c.U32());
        c.Skip(4);
        int blob = static_cast<int>(c.U32());
        c.Skip(4);
        if (blob > 0) {
            if (blob > 1 << 20) {
                c.ok = false;
                return;
            }
            c.Skip(blob);
        }
        int relative = static_cast<int>(c.U32());
        c.Skip(4);
        if (relative != 0) {
            SkipRelative4();
        }
        Emit(refs, iref, offI, numI, offV, numV);
    }

    void PartAf(bool c8) {
        if (c8) {
            c.U32();
            c.Skip(4);
            c.U32();
        } else {
            c.Skip(4);
        }
        int lists = static_cast<int>(c.U32());
        if (lists < 1 || lists > 8) {
            c.ok = false;
            return;
        }
        std::vector<int> refs;
        for (int i = 0; i < lists; ++i) {
            refs.push_back(VertexRef());
        }
        c.Skip(4);
        int iref = IndexRef();
        int offI = static_cast<int>(c.U32());
        int numI = static_cast<int>(c.U32());
        int offV = static_cast<int>(c.U32());
        if (c.U16() != 0) {
            c.ok = false;
            return;
        }
        int numV = static_cast<int>(c.U32());
        c.Skip(4);
        c.Skip(4);
        int blob = static_cast<int>(c.U32());
        c.Skip(4);
        if (blob > 0) {
            if (blob > 1 << 20) {
                c.ok = false;
                return;
            }
            c.Skip(blob);
        }
        int relative = static_cast<int>(c.U32());
        c.Skip(4);
        if (relative != 0) {
            c.Skip(4);
            int guard = 0;
            while (c.ok && c.PeekU32() != 0) {
                c.Skip(8);
                if (++guard > 10000) {
                    c.ok = false;
                    return;
                }
            }
            c.Skip(8);
        } else {
            c.Skip(4);
        }
        c.Skip(36);
        Emit(refs, iref, offI, numI, offV, numV);
    }
};

int FindRev(const uint8_t* data, int size, int start, char a, char b, char c, char d) {
    for (int i = start; i + 8 < size; ++i) {
        if (data[i] == static_cast<uint8_t>(a) && data[i + 1] == static_cast<uint8_t>(b)
            && data[i + 2] == static_cast<uint8_t>(c) && data[i + 3] == static_cast<uint8_t>(d)) {
            return i;
        }
    }
    return -1;
}

}  // namespace

const char* TtExtension(const std::string& name) {
    static std::string ext;
    ext.clear();
    auto slash = name.find_last_of("/\\");
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return "";
    }
    ext = name.substr(dot);
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return ext.c_str();
}

bool TtOpenArchive(const uint8_t* data, int size, TtArchive& out) {
    out = {};
    if (data == nullptr || size < 16) {
        return false;
    }
    if (Le32(data) == 0x1234567Au) {
        return OpenPak(data, size, out);
    }
    if (std::memcmp(data, "CMP2CMP2CMP2CMP2", 16) == 0) {
        out.kind = "cmp2";
        return false;
    }
    uint32_t infoOff = Le32(data);
    if ((infoOff & 0x80000000u) != 0) {
        infoOff ^= 0xffffffffu;
        infoOff <<= 8;
        infoOff += 0x100u;
    }
    uint32_t infoSize = Le32(data + 4);
    if (infoOff < 8 || infoSize < 16 || infoOff > static_cast<uint32_t>(size)
        || infoSize > static_cast<uint32_t>(size) - infoOff) {
        return false;
    }
    const uint8_t* info = data + infoOff;
    uint32_t type = Le32(info);
    uint32_t files = Le32(info + 4);
    if (type == 0x3443432eu || type == 0x2e434334u || files == 0x3443432eu || files == 0x2e434334u) {
        return OpenCc4(data, size, info, static_cast<int>(infoSize), out);
    }
    return OpenOldDat(data, size, info, static_cast<int>(infoSize), out);
}

bool TtExtract(const uint8_t* data, int size, const TtFile& file, std::vector<uint8_t>& out) {
    out.clear();
    if (data == nullptr || file.offset > static_cast<uint64_t>(size)) {
        return false;
    }
    int avail = static_cast<int>(static_cast<uint64_t>(size) - file.offset);
    const uint8_t* src = data + file.offset;
    if (avail >= 4 && std::memcmp(src, "LZ2K", 4) == 0) {
        int n = file.zsize != 0 ? static_cast<int>(file.zsize) : avail;
        if (n > avail) {
            n = avail;
        }
        return Lz2kDecompress(src, n, out);
    }
    if (avail >= 4 && std::memcmp(src, "DFLT", 4) == 0) {
        int n = file.zsize != 0 ? static_cast<int>(file.zsize) : avail;
        if (n > avail) {
            n = avail;
        }
        return UnpackDflt(src, n, out);
    }
    if (avail >= 0x28 && std::memcmp(src, "Deflate_v1.0", 13) == 0) {
        int n = avail - 0x24;
        if (file.size > 0x24 && static_cast<int>(file.size) - 0x24 < n) {
            n = static_cast<int>(file.size) - 0x24;
        }
        return InflateRaw(src + 0x24, n, out);
    }
    uint32_t n = file.size != 0 ? file.size : file.zsize;
    if (!RangeOk(file.offset, n, size) || n == 0) {
        return false;
    }
    out.assign(src, src + n);
    return true;
}

bool TtReadNu20(const uint8_t* data, int size, HostMesh& mesh, char* note, int noteLen) {
    if (note != nullptr && noteLen > 0) {
        note[0] = 0;
    }
    if (data == nullptr || size < 16) {
        return false;
    }
    int nu = FindRev(data, size, 0, '0', '2', 'U', 'N');
    if (nu < 0 || nu + 8 > size) {
        return false;
    }
    uint32_t nuVer = Be32p(data + nu + 4);
    if (nuVer == 0 || nuVer > 0x400u) {
        return false;
    }
    int meshAt = FindRev(data, size, nu + 8, 'H', 'S', 'E', 'M');
    if (meshAt < 0) {
        if (note != nullptr && noteLen > 0) {
            std::snprintf(note, static_cast<size_t>(noteLen), "NU20 v%X NO MESH", nuVer);
        }
        return false;
    }
    uint32_t meshVer = Be32p(data + meshAt + 4);
    MeshReader reader;
    reader.c = Cursor{data, size, meshAt + 8, true, true};
    reader.mesh = &mesh;
    auto parts = [&](bool c8, bool af) {
        int count = static_cast<int>(reader.c.U32());
        if (count <= 0 || count > 10000) {
            reader.c.ok = false;
            return;
        }
        for (int i = 0; i < count && reader.c.ok; ++i) {
            if (c8 || af) {
                reader.PartAf(c8);
            } else {
                reader.PartV4();
            }
        }
    };
    if (meshVer == 4 || meshVer == 5 || meshVer == 0x2e || meshVer == 0x2f || meshVer == 0x30 || meshVer == 0xa9
        || meshVer == 0xaa) {
        parts(false, false);
    } else if (meshVer == 0xaf) {
        parts(false, true);
    } else if (meshVer == 0xc8) {
        parts(true, false);
    } else if (note != nullptr && noteLen > 0) {
        std::snprintf(note, static_cast<size_t>(noteLen), "NU20 v%X MESH v%X", nuVer, meshVer);
        return false;
    } else {
        return false;
    }
    if (mesh.indices.size() >= 3 && mesh.xyz.size() >= 9) {
        if (note != nullptr && noteLen > 0) {
            std::snprintf(note, static_cast<size_t>(noteLen), "NU20 v%X MESH v%X", nuVer, meshVer);
        }
        return true;
    }
    if (note != nullptr && noteLen > 0) {
        std::snprintf(note, static_cast<size_t>(noteLen), "MESH v%X NO VERTS", meshVer);
    }
    return false;
}
