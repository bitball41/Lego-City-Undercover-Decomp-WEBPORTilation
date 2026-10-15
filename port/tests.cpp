#include "city_game.h"

#include "assets.h"
#include "draw.h"
#include "host_pad.h"
#include "host_state.h"
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
    unsigned char ghg[8 + 12 + 36 + 12];
    std::memset(ghg, 0, sizeof(ghg));
    ghg[0] = 'H';
    ghg[1] = 'S';
    ghg[2] = 'E';
    ghg[3] = 'M';
    uint32_t version = 1, verts = 3, indices = 3;
    std::memcpy(ghg + 8, &version, 4);
    std::memcpy(ghg + 12, &verts, 4);
    std::memcpy(ghg + 16, &indices, 4);
    float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(ghg + 20, positions, sizeof(positions));
    uint32_t indexList[3] = {0, 1, 2};
    std::memcpy(ghg + 56, indexList, sizeof(indexList));
    Expect(AssetsSubmit("crate.ghg", ghg, static_cast<int>(sizeof(ghg))) == 1, "ghg mesh");
    Expect(AssetsLoaded() == 2, "asset count");
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
    Expect(AssetsPending() == 0 && AssetsLoaded() == 3, "pumped one file");
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

    GameDebugSetPlayer(2.f, 0.f, 2.f);
    HostSetPad(0, 0, 0.f, 0.f, 0.f, 0.f);
    int before = GameStuds();
    GameFrame(1.f / 60.f);
    Expect(GameStuds() == before + 1, "collect stud");
    Expect(std::strstr(GameStatus(), "STUDS") != nullptr, "status");

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
        std::printf("ok %d checks\n", 22);
    } else {
        std::printf("%d failed\n", g_fails);
    }
    return g_fails == 0 ? 0 : 1;
}
