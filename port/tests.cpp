#include "city_game.h"

#include "assets.h"
#include "draw.h"
#include "host_pad.h"
#include "host_state.h"
#include "lz2k.h"
#include "tt_read.h"
#include "kestrel/NuMemory.h"
#include "legoapi/optionssave.h"
#include "legoapi/players.h"
#include "numath/matrix.h"
#include "numath/scalar.h"
#include "numath/vector.h"
#include "script/common/ScriptContext.h"
#include "script/commands/SCmdAdd.h"
#include "script/commands/SCmdAnd.h"
#include "script/commands/SCmdGreaterThan.h"
#include "support/CommonString.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

int ReadNextNumber(const char*& str, int max);
uint32_t NuStrHashUpperCaseFNV1(const char* str, unsigned int seed);
int TestLocale();

namespace {

int g_fails = 0;

void Expect(bool ok, const char* message) {
    if (!ok) {
        std::printf("FAIL %s\n", message);
        ++g_fails;
    }
}

struct OptionsBlock {
    Options options;
    OPTIONSSAVE save;
};

}  // namespace

int GameRunTests() {
    HostStorageUseMemory(true);
    HostAudioSilence();

    Expect(NuFloatToHalf(1.f) == 0x3c00, "half float 1");
    Expect(std::fabs(NuSinOld(0)) < 0.001f, "sin 0");
    Expect(std::fabs(NuSinOld(16384) - 1.f) < 0.02f, "sin quarter turn");
    Expect(NuStrCmp("a", "b") < 0, "strcmp");
    Expect(NuStrICmp("Street", "street") == 0, "stricmp");
    uint32_t driver = NuStrHashUpperCaseFNV1("Driver", 0x811c9dc5);
    uint32_t any = NuStrHashUpperCaseFNV1("Any", 0x811c9dc5);
    Expect(driver != any && driver != 0, "fnv");

    const char* tint = "12, 34, 200";
    Expect(ReadNextNumber(tint, 255) == 12, "read number");
    Expect(ReadNextNumber(tint, 255) == 34, "read number 2");

    void* aligned = NuMemoryGet()->GetThreadMem()->_BlockAlloc(24, 16, 0, "test", 0);
    Expect(aligned != nullptr && (reinterpret_cast<uintptr_t>(aligned) % 16) == 0, "aligned alloc");
    NuMemoryGet()->GetThreadMem()->BlockFree(aligned, 0);
    NuMemoryGet()->GetThreadMem()->BlockFree(nullptr, 0);

    NUMTX view;
    NUVEC eye{0.f, 2.f, 0.f};
    NUVEC at{0.f, 2.f, 5.f};
    NUVEC up{0.f, 1.f, 0.f};
    NuMtxLookAtD3D(&view, &eye, &at, &up);
    NUVEC point{0.f, 2.f, 5.f};
    NUVEC cam;
    NuVecMtxTransform(&cam, &point, &view);
    Expect(std::fabs(cam.x) < 0.02f && std::fabs(cam.y) < 0.02f && std::fabs(cam.z - 5.f) < 0.05f, "look at");
    NUMTX proj;
    NUMTX vp;
    NuMtxSetPerspectiveD3D(&proj, 1.f, 1.f, 0.1f, 100.f);
    NuMtxMul(&vp, &view, &proj);
    float clipX = point.x * vp.m[0][0] + point.y * vp.m[1][0] + point.z * vp.m[2][0] + vp.m[3][0];
    float clipY = point.x * vp.m[0][1] + point.y * vp.m[1][1] + point.z * vp.m[2][1] + vp.m[3][1];
    float clipW = point.x * vp.m[0][3] + point.y * vp.m[1][3] + point.z * vp.m[2][3] + vp.m[3][3];
    Expect(std::fabs(clipW) > 0.1f && std::fabs(clipX / clipW) < 0.02f && std::fabs(clipY / clipW) < 0.02f,
           "project center");

    {
        ScriptContext ctx{};
        SCmdAdd add(nullptr, 1);
        ctx.SetNumberArg(0, 2.f);
        ctx.SetNumberArg(1, 3.5f);
        add.Run(ctx, 0);
        Expect(std::fabs(ctx.ReturnFloat(0) - 5.5f) < 0.001f, "script add");
        SCmdGreaterThan greater(nullptr, 1);
        ctx.SetNumberArg(0, 4.f);
        ctx.SetNumberArg(1, 1.f);
        greater.Exec(ctx);
        Expect(ctx.ReturnBool(0), "script greater");
        SCmdAnd both(nullptr, 2);
        ctx.SetBoolArg(0, true);
        ctx.SetBoolArg(1, false);
        both.Exec(ctx);
        Expect(!ctx.ReturnBool(0), "script and");
    }

    OptionsBlock block;
    block.options.SetBrightness(150);
    Expect(block.options.GetBrightness() == 100, "brightness clamp");
    block.options.SetBrightness(-4);
    Expect(block.options.GetBrightness() == 0, "brightness floor");
    Expect(HostVideoState().brightness == 0, "video brightness");

    HostAudioSet(1.f, 1.f, 1.f, true);
    HostAudioPlay(440.f, 0.1f, false);
    float peak = 0.f;
    for (int i = 0; i < 80; ++i) {
        peak = std::max(peak, std::fabs(HostAudioRender(1.f / 200.f)));
    }
    Expect(peak > 0.05f, "audio voice");
    HostAudioSilence();
    HostAudioSet(0.f, 0.f, 0.f, false);
    HostAudioPlay(440.f, 0.1f, false);
    Expect(HostAudioRender(0.01f) == 0.f, "audio muted");

    const char* obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    Expect(AssetsSubmit("tri.obj", reinterpret_cast<const uint8_t*>(obj), static_cast<int>(std::strlen(obj))) == 1,
           "obj mesh");
    auto be32 = [](std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v >> 24));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    };
    auto be16 = [](std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v));
    };
    auto bef = [&](std::vector<uint8_t>& out, float f) {
        uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        be32(out, u);
    };
    auto le32 = [](std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
        out.push_back(static_cast<uint8_t>(v >> 16));
        out.push_back(static_cast<uint8_t>(v >> 24));
    };
    auto le16 = [](std::vector<uint8_t>& out, uint16_t v) {
        out.push_back(static_cast<uint8_t>(v));
        out.push_back(static_cast<uint8_t>(v >> 8));
    };
    std::vector<uint8_t> nu20;
    nu20.push_back('0');
    nu20.push_back('2');
    nu20.push_back('U');
    nu20.push_back('N');
    be32(nu20, 0x0a);
    nu20.push_back('H');
    nu20.push_back('S');
    nu20.push_back('E');
    nu20.push_back('M');
    be32(nu20, 4);
    be32(nu20, 1);
    be32(nu20, 1);
    nu20.insert(nu20.end(), 8, 0);
    be32(nu20, 3);
    nu20.push_back(3);
    nu20.insert(nu20.end(), 31, 0);
    bef(nu20, 0.f);
    bef(nu20, 0.f);
    bef(nu20, 0.f);
    bef(nu20, 1.f);
    bef(nu20, 0.f);
    bef(nu20, 0.f);
    bef(nu20, 0.f);
    bef(nu20, 1.f);
    bef(nu20, 0.f);
    nu20.insert(nu20.end(), 4, 0);
    nu20.insert(nu20.end(), 4, 0);
    nu20.insert(nu20.end(), 8, 0);
    be32(nu20, 3);
    nu20.insert(nu20.end(), 4, 0);
    be16(nu20, 4);
    be16(nu20, 5);
    be16(nu20, 6);
    be32(nu20, 0);
    be32(nu20, 3);
    be32(nu20, 0);
    be16(nu20, 0);
    be32(nu20, 3);
    nu20.insert(nu20.end(), 4, 0);
    be32(nu20, 0);
    nu20.insert(nu20.end(), 4, 0);
    be32(nu20, 0);
    nu20.insert(nu20.end(), 4, 0);
    Expect(AssetsSubmit("crate.ghg", nu20.data(), static_cast<int>(nu20.size())) == 1, "nu20 mesh");
    Expect(AssetsLoaded() == 2, "asset count");
    const HostMesh* loaded = AssetsAt(1);
    Expect(loaded != nullptr && loaded->indices.size() == 3, "nu20 triangle");

    std::vector<uint8_t> info;
    le32(info, static_cast<uint32_t>(-1));
    le32(info, 1);
    le32(info, 56);
    le32(info, static_cast<uint32_t>(nu20.size()));
    le32(info, static_cast<uint32_t>(nu20.size()));
    info.insert(info.end(), 4, 0);
    le32(info, 1);
    le16(info, 0);
    le16(info, 0);
    le32(info, 0);
    le32(info, 8);
    const char* inner = "tri.ghg";
    info.insert(info.end(), inner, inner + 8);
    Expect(info.size() == 48, "dat directory size");
    std::vector<uint8_t> dat;
    le32(dat, 8);
    le32(dat, static_cast<uint32_t>(info.size()));
    dat.insert(dat.end(), info.begin(), info.end());
    dat.insert(dat.end(), nu20.begin(), nu20.end());
    {
        std::ofstream sample("/tmp/lcu-sample.dat", std::ios::binary);
        sample.write(reinterpret_cast<const char*>(dat.data()), static_cast<std::streamsize>(dat.size()));
    }
    Expect(AssetsSubmit("GAME.DAT", dat.data(), static_cast<int>(dat.size())) == 1, "dat extract");
    Expect(AssetsLoaded() == 3, "dat mesh landed");

    std::vector<uint8_t> compressed(nu20.size() + 64);
    z_stream strm{};
    deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    strm.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(nu20.data()));
    strm.avail_in = static_cast<uInt>(nu20.size());
    strm.next_out = compressed.data();
    strm.avail_out = static_cast<uInt>(compressed.size());
    int zret = deflate(&strm, Z_FINISH);
    int zlen = static_cast<int>(compressed.size() - strm.avail_out);
    deflateEnd(&strm);
    Expect(zret == Z_STREAM_END && zlen > 0, "deflate nu20");
    std::vector<uint8_t> dflt;
    dflt.insert(dflt.end(), {'D', 'F', 'L', 'T'});
    le32(dflt, static_cast<uint32_t>(zlen));
    le32(dflt, static_cast<uint32_t>(nu20.size()));
    dflt.insert(dflt.end(), compressed.begin(), compressed.begin() + zlen);
    TtFile wrapped;
    wrapped.offset = 0;
    wrapped.size = static_cast<uint32_t>(nu20.size());
    wrapped.zsize = static_cast<uint32_t>(dflt.size());
    std::vector<uint8_t> inflated;
    Expect(TtExtract(dflt.data(), static_cast<int>(dflt.size()), wrapped, inflated), "dflt extract");
    Expect(inflated.size() == nu20.size() && std::memcmp(inflated.data(), nu20.data(), nu20.size()) == 0, "dflt bytes");

    std::vector<uint8_t> stored;
    stored.insert(stored.end(), {'L', 'Z', '2', 'K'});
    le32(stored, 4);
    le32(stored, 4);
    stored.insert(stored.end(), {'H', 'I', 'Y', 'A'});
    std::vector<uint8_t> plain;
    Expect(Lz2kDecompress(stored.data(), static_cast<int>(stored.size()), plain), "lz2k stored");
    Expect(plain.size() == 4 && std::memcmp(plain.data(), "HIYA", 4) == 0, "lz2k bytes");
    std::string dir = "/tmp/lcu-assets";
    std::string cmd = "mkdir -p " + dir;
    Expect(std::system(cmd.c_str()) == 0, "asset dir");
    {
        std::ofstream extra(dir + "/extra.obj");
        extra << obj;
    }
    Expect(AssetsMountDirectory(dir.c_str()) == 1, "mount streams index");
    Expect(AssetsPending() == 1, "queued file");
    AssetsPump();
    Expect(AssetsPending() == 0 && AssetsLoaded() == 4, "pumped one file");
    AssetsClear();

    NuVideoSetBrightness(50, 50);
    DrawBegin(160, 90);
    DrawSetCamera(eye, at, 1.f);
    DrawBox(NUVEC{-1.f, 1.f, 4.f}, NUVEC{1.f, 3.f, 6.f}, 220, 20, 20);
    DrawFinish();
    const unsigned char* pixels = DrawPixels();
    int cx = DrawWidth() / 2;
    int cy = DrawHeight() / 2;
    const unsigned char* center = pixels + (cy * DrawWidth() + cx) * 4;
    Expect(center[0] > center[2] + 30 && center[0] > 60, "red wall in front of camera");

    Expect(TestLocale() == 0, "locale name");
    Expect(GetPlayer1() == nullptr && GetPlayer2() == nullptr, "players empty");

    GameBoot();
    GameDebugSetPlayer(0.f, 0.f, 0.f);
    GameDebugSetYaw(0.f);
    HostSetPad(0, 0, 0.f, 1.f, 0.f, 0.f);
    float z0 = GamePlayerZ();
    for (int i = 0; i < 20; ++i) {
        GameFrame(1.f / 60.f);
    }
    Expect(GamePlayerZ() > z0 + 0.4f, "walk forward");

    GameDebugSetPlayer(0.f, 0.f, -4.f);
    HostSetPad(0, HOST_BTN_A, 0.f, 0.f, 0.f, 0.f);
    GameFrame(1.f / 60.f);
    for (int i = 0; i < 8; ++i) {
        GameFrame(1.f / 60.f);
    }
    Expect(GamePlayerY() > 0.35f, "jump");

    HostSetPad(0, 0, 0.f, 0.f, 0.f, 0.f);
    GameFrame(1.f / 60.f);
    Expect(std::strstr(GameStatus(), "DROP GAME DUMP") != nullptr, "status asks for dump");

    const unsigned char* frame = DrawPixels();
    bool varied = false;
    if (frame != nullptr) {
        unsigned char first = frame[0];
        for (int i = 0; i < DrawWidth() * DrawHeight(); i += 40) {
            if (frame[i * 4] != first) {
                varied = true;
                break;
            }
        }
    }
    Expect(varied, "frame has a scene");

    if (g_fails == 0) {
        std::printf("ok\n");
    } else {
        std::printf("%d failed\n", g_fails);
    }
    return g_fails == 0 ? 0 : 1;
}
