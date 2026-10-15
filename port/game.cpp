#include "city_game.h"

#include "assets.h"
#include "draw.h"
#include "host_pad.h"
#include "host_state.h"
#include "legoapi/optionssave.h"
#include "nucore/NuInputDevice.h"
#include "nucore/NuInputDevicePS.h"
#include "numath/vector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kWidth = 480;
constexpr int kFbH = 270;
constexpr int kStudCount = 20;
constexpr float kRadius = 0.36f;
constexpr float kBody = 1.55f;

struct Solid {
    float x0, y0, z0, x1, y1, z1;
    int r, g, b;
};

struct Stud {
    float x, y, z;
    bool got;
};

struct SaveBlob {
    char magic[4];
    uint32_t version;
    OPTIONSSAVE options;
    uint32_t studs;
    float x, y, z, yaw, pitch;
};

struct OptionsBlock {
    Options options;
    OPTIONSSAVE save;
};

struct GameState {
    NuInputDevice pad;
    OptionsBlock options;
    std::vector<Solid> solids;
    Stud studs[kStudCount];
    float x, y, z;
    float vy;
    float yaw;
    float pitch;
    bool grounded;
    bool paused;
    bool booted;
    int menu;
    unsigned prevButtons;
    float time;
    int collected;
    bool wonTune;
    char status[192];

    GameState() : pad(0) {}
};

GameState& State() {
    static GameState state;
    return state;
}

void ApplyOptions(GameState& game) {
    float master = static_cast<float>(game.options.options.GetAudio_volume()) / 10.f;
    float music = static_cast<float>(game.options.options.GetMusic_volume()) / 10.f;
    float sfx = static_cast<float>(game.options.options.GetSFX_volume()) / 10.f;
    if (music > 1.f) {
        music = 1.f;
    }
    if (sfx > 1.f) {
        sfx = 1.f;
    }
    HostAudioSet(master, music, sfx, game.options.options.GetMusic_on() != 0);
}

void DefaultOptions(GameState& game) {
    std::memset(&game.options.save, 0, sizeof(game.options.save));
    game.options.options.SetBrightness(50);
    game.options.options.SetMusic_on(1);
    game.options.options.SetMusic_volume(7);
    game.options.options.SetSFX_volume(8);
    game.options.options.SetAudio_volume(10);
    game.options.options.SetSubtitles_on(1);
    game.options.options.SetSafeZone(1.f);
    game.options.options.SetVSync(1);
    ApplyOptions(game);
}

void BuildCity(GameState& game) {
    game.solids.clear();
    auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1, int r, int g, int b) {
        if (x0 > x1) std::swap(x0, x1);
        if (y0 > y1) std::swap(y0, y1);
        if (z0 > z1) std::swap(z0, z1);
        game.solids.push_back({x0, y0, z0, x1, y1, z1, r, g, b});
    };
    add(-18, 0, -18, -8, 7, -8, 40, 90, 170);
    add(8, 0, -18, 18, 9, -6, 180, 50, 40);
    add(-18, 0, 10, -7, 5, 18, 50, 140, 70);
    add(9, 0, 8, 18, 6, 18, 90, 90, 110);
    add(-4, 0, -6, 4, 1.15f, -2, 200, 170, 40);
    add(-2, 0, 14, 2, 0.35f, 18, 160, 160, 170);
    add(2, 0, 14, 6, 0.7f, 18, 150, 150, 165);
    add(6, 0, 14, 10, 1.05f, 18, 140, 140, 160);
    for (int i = 0; i < kStudCount; ++i) {
        float angle = static_cast<float>(i) * 0.55f;
        game.studs[i].x = std::sin(angle) * (3.f + i * 0.35f);
        game.studs[i].z = 4.f + i * 0.8f;
        game.studs[i].y = 0.35f;
        game.studs[i].got = false;
    }
    game.studs[0].x = 2.f;
    game.studs[0].z = 2.f;
    game.studs[0].y = 0.4f;
}

void Resolve(GameState& game, const Solid& box) {
    float px0 = game.x - kRadius;
    float px1 = game.x + kRadius;
    float py0 = game.y;
    float py1 = game.y + kBody;
    float pz0 = game.z - kRadius;
    float pz1 = game.z + kRadius;
    if (px1 <= box.x0 || px0 >= box.x1 || py1 <= box.y0 || py0 >= box.y1 || pz1 <= box.z0 || pz0 >= box.z1) {
        return;
    }
    float penX = std::min(px1 - box.x0, box.x1 - px0);
    float penY = std::min(py1 - box.y0, box.y1 - py0);
    float penZ = std::min(pz1 - box.z0, box.z1 - pz0);
    float midX = (box.x0 + box.x1) * 0.5f;
    float midY = (box.y0 + box.y1) * 0.5f;
    float midZ = (box.z0 + box.z1) * 0.5f;
    if (penY <= penX && penY <= penZ) {
        if (game.y + kBody * 0.5f < midY) {
            game.y -= penY;
            if (game.vy > 0.f) {
                game.vy = 0.f;
            }
        } else {
            game.y += penY;
            if (game.vy < 0.f) {
                game.vy = 0.f;
                game.grounded = true;
            }
        }
    } else if (penX < penZ) {
        game.x += game.x < midX ? -penX : penX;
    } else {
        game.z += game.z < midZ ? -penZ : penZ;
    }
}

void SaveGame(GameState& game) {
    SaveBlob blob{};
    blob.magic[0] = 'L';
    blob.magic[1] = 'C';
    blob.magic[2] = 'U';
    blob.magic[3] = 'W';
    blob.version = 1;
    std::memcpy(&blob.options, &game.options.save, sizeof(blob.options));
    uint32_t mask = 0;
    for (int i = 0; i < kStudCount; ++i) {
        if (game.studs[i].got) {
            mask |= 1u << i;
        }
    }
    blob.studs = mask;
    blob.x = game.x;
    blob.y = game.y;
    blob.z = game.z;
    blob.yaw = game.yaw;
    blob.pitch = game.pitch;
    HostStorageWrite(&blob, static_cast<int>(sizeof(blob)));
}

void LoadGame(GameState& game) {
    SaveBlob blob{};
    int n = HostStorageRead(&blob, static_cast<int>(sizeof(blob)));
    if (n < static_cast<int>(sizeof(blob)) || std::memcmp(blob.magic, "LCUW", 4) != 0 || blob.version != 1) {
        return;
    }
    std::memcpy(&game.options.save, &blob.options, sizeof(game.options.save));
    game.options.options.SetBrightness(game.options.save.m_brightness);
    game.options.options.SetSafeZone(game.options.save.m_safeZone);
    ApplyOptions(game);
    game.x = blob.x;
    game.y = blob.y;
    game.z = blob.z;
    game.yaw = blob.yaw;
    game.pitch = blob.pitch;
    game.collected = 0;
    for (int i = 0; i < kStudCount; ++i) {
        game.studs[i].got = (blob.studs & (1u << i)) != 0;
        if (game.studs[i].got) {
            ++game.collected;
        }
    }
}

void Simulate(GameState& game, float dt) {
    unsigned buttons = game.pad.GetButtons();
    float lx = game.pad.GetAnalogValue(NU_ANALOG_LEFT_STICK_X);
    float ly = game.pad.GetAnalogValue(NU_ANALOG_LEFT_STICK_Y);
    float rx = game.pad.GetAnalogValue(NU_ANALOG_RIGHT_STICK_X);
    float ry = game.pad.GetAnalogValue(NU_ANALOG_RIGHT_STICK_Y);
    bool invert = game.options.options.GetInvertYAxis() != 0;
    game.yaw -= rx * 2.4f * dt;
    game.pitch += ry * (invert ? -1.3f : 1.3f) * dt;
    if (game.pitch < -0.45f) game.pitch = -0.45f;
    if (game.pitch > 0.7f) game.pitch = 0.7f;

    float sy = std::sin(game.yaw);
    float cy = std::cos(game.yaw);
    NUVEC forward{sy, 0.f, cy};
    NUVEC right{cy, 0.f, -sy};
    float speed = (buttons & HOST_BTN_ZL) ? 7.4f : 4.1f;
    game.x += (right.x * lx + forward.x * ly) * speed * dt;
    game.z += (right.z * lx + forward.z * ly) * speed * dt;

    bool jump = (buttons & HOST_BTN_A) && !(game.prevButtons & HOST_BTN_A);
    if (jump && game.grounded) {
        game.vy = 8.4f;
        game.grounded = false;
        HostAudioPlay(520.f, 0.08f, false);
    }
    game.vy += -26.f * dt;
    game.y += game.vy * dt;
    game.grounded = false;
    if (game.y < 0.f) {
        game.y = 0.f;
        if (game.vy < 0.f) {
            game.vy = 0.f;
        }
        game.grounded = true;
    }
    for (const Solid& solid : game.solids) {
        Resolve(game, solid);
    }

    for (int i = 0; i < kStudCount; ++i) {
        Stud& stud = game.studs[i];
        if (stud.got) {
            continue;
        }
        float dx = game.x - stud.x;
        float dz = game.z - stud.z;
        if (dx * dx + dz * dz < 0.7f * 0.7f && std::fabs(game.y - stud.y) < 1.4f) {
            stud.got = true;
            ++game.collected;
            HostAudioPlay(880.f, 0.09f, false);
            game.pad.SetMotors(0.4f, 0.2f);
            SaveGame(game);
        }
    }
    if (game.collected == kStudCount && !game.wonTune) {
        game.wonTune = true;
        HostAudioPlay(523.f, 0.2f, true);
        HostAudioPlay(659.f, 0.25f, true);
        HostAudioPlay(784.f, 0.4f, true);
    }
}

void Menu(GameState& game, unsigned buttons) {
    bool up = (buttons & HOST_BTN_UP) && !(game.prevButtons & HOST_BTN_UP);
    bool down = (buttons & HOST_BTN_DOWN) && !(game.prevButtons & HOST_BTN_DOWN);
    bool left = (buttons & HOST_BTN_LEFT) && !(game.prevButtons & HOST_BTN_LEFT);
    bool right = (buttons & HOST_BTN_RIGHT) && !(game.prevButtons & HOST_BTN_RIGHT);
    if (up) game.menu = (game.menu + 5) % 6;
    if (down) game.menu = (game.menu + 1) % 6;
    int dir = (right ? 1 : 0) - (left ? 1 : 0);
    if (dir == 0) {
        return;
    }
    switch (game.menu) {
    case 0:
        game.options.options.AdjustBrightness(static_cast<char>(dir * 5));
        break;
    case 1:
        game.options.options.AdjustMusic_volume(static_cast<char>(dir));
        break;
    case 2:
        game.options.options.AdjustSFX_volume(static_cast<char>(dir));
        break;
    case 3:
        game.options.options.ToggleMusic_on();
        break;
    case 4:
        game.options.options.ToggleInvertYAxis();
        break;
    default:
        game.options.options.AdjustSafeZone(dir * 0.02f);
        break;
    }
    ApplyOptions(game);
    SaveGame(game);
}

void Render(GameState& game) {
    DrawBegin(kWidth, kFbH);
    float sy = std::sin(game.yaw);
    float cy = std::cos(game.yaw);
    float sp = std::sin(game.pitch);
    float cp = std::cos(game.pitch);
    NUVEC look{sy * cp, sp, cy * cp};
    NUVEC eye{game.x - look.x * 5.6f, game.y + 1.5f - look.y * 5.6f, game.z - look.z * 5.6f};
    if (eye.y < 0.4f) {
        eye.y = 0.4f;
    }
    NUVEC target{game.x, game.y + 1.25f, game.z};
    DrawSetCamera(eye, target, 1.05f);
    DrawTriangle(NUVEC{-200.f, -0.04f, -200.f}, NUVEC{200.f, -0.04f, -200.f}, NUVEC{200.f, -0.04f, 200.f}, 58, 64, 60);
    DrawTriangle(NUVEC{-200.f, -0.04f, -200.f}, NUVEC{200.f, -0.04f, 200.f}, NUVEC{-200.f, -0.04f, 200.f}, 58, 64, 60);

    int gx0 = static_cast<int>(std::floor(game.x / 4.f)) - 6;
    int gz0 = static_cast<int>(std::floor(game.z / 4.f)) - 6;
    for (int gz = gz0; gz < gz0 + 12; ++gz) {
        for (int gx = gx0; gx < gx0 + 12; ++gx) {
            float x0 = static_cast<float>(gx) * 4.f;
            float z0 = static_cast<float>(gz) * 4.f;
            bool alt = ((gx + gz) & 1) != 0;
            int shade = alt ? 70 : 88;
            NUVEC a{x0, 0.f, z0};
            NUVEC b{x0 + 4.f, 0.f, z0};
            NUVEC c{x0 + 4.f, 0.f, z0 + 4.f};
            NUVEC d{x0, 0.f, z0 + 4.f};
            DrawTriangle(a, b, c, shade, shade + 8, shade + 4);
            DrawTriangle(a, c, d, shade, shade + 8, shade + 4);
        }
    }
    for (const Solid& solid : game.solids) {
        NUVEC mn{solid.x0, solid.y0, solid.z0};
        NUVEC mx{solid.x1, solid.y1, solid.z1};
        DrawBox(mn, mx, solid.r, solid.g, solid.b);
    }
    for (int i = 0; i < AssetsLoaded(); ++i) {
        const HostMesh* mesh = AssetsAt(i);
        if (mesh == nullptr || mesh->xyz.empty()) {
            continue;
        }
        float extent = std::max(mesh->maxX - mesh->minX, std::max(mesh->maxY - mesh->minY, mesh->maxZ - mesh->minZ));
        float scale = extent > 0.001f ? 2.2f / extent : 1.f;
        NUVEC pos{-14.f + static_cast<float>(i % 5) * 3.2f, -mesh->minY * scale, -16.f};
        DrawMesh(mesh->xyz.data(), static_cast<int>(mesh->xyz.size() / 3), mesh->indices.data(),
                 static_cast<int>(mesh->indices.size()), pos, 0.4f * i, scale, 220, 200, 80);
    }
    for (int i = 0; i < kStudCount; ++i) {
        const Stud& stud = game.studs[i];
        if (stud.got) {
            continue;
        }
        float bob = std::sin(game.time * 4.f + i) * 0.08f;
        NUVEC mn{stud.x - 0.18f, stud.y + bob, stud.z - 0.18f};
        NUVEC mx{stud.x + 0.18f, stud.y + bob + 0.28f, stud.z + 0.18f};
        DrawBox(mn, mx, 240, 210, 20);
    }
    NUVEC feet{game.x, game.y, game.z};
    DrawBox({feet.x - 0.22f, feet.y, feet.z - 0.16f}, {feet.x + 0.22f, feet.y + 0.45f, feet.z + 0.16f}, 30, 70, 170);
    DrawBox({feet.x - 0.28f, feet.y + 0.45f, feet.z - 0.18f}, {feet.x + 0.28f, feet.y + 1.05f, feet.z + 0.18f}, 200, 40,
            40);
    DrawBox({feet.x - 0.16f, feet.y + 1.05f, feet.z - 0.16f}, {feet.x + 0.16f, feet.y + 1.42f, feet.z + 0.16f}, 240, 200,
            40);

    float zone = game.options.options.GetSafeZone();
    int insetX = static_cast<int>((1.f - zone) * kWidth * 0.5f);
    int insetY = static_cast<int>((1.f - zone) * kFbH * 0.5f);
    std::snprintf(game.status, sizeof(game.status), "STUDS %02d/%02d", game.collected, kStudCount);
    DrawText(16 + insetX, 16 + insetY, game.status, 255, 230, 40);
    if (game.collected == kStudCount) {
        DrawText(150, 120, "CITY CLEAR", 255, 255, 255);
    }
    if (AssetsLoaded() > 0 || AssetsPending() > 0) {
        char assets[64];
        std::snprintf(assets, sizeof(assets), "ASSETS %d  QUEUED %d", AssetsLoaded(), AssetsPending());
        DrawText(16 + insetX, 40 + insetY, assets, 220, 240, 255);
    }
    if (game.paused) {
        DrawRect(90, 40, 300, 180, 10, 16, 28, 210);
        DrawText(120, 52, "PAUSE", 255, 220, 40);
        const char* labels[6] = {"BRIGHTNESS", "MUSIC VOL", "SFX VOL", "MUSIC", "INVERT Y", "SAFE ZONE"};
        char values[6][16];
        std::snprintf(values[0], 16, "%d", game.options.options.GetBrightness());
        std::snprintf(values[1], 16, "%d", game.options.options.GetMusic_volume());
        std::snprintf(values[2], 16, "%d", game.options.options.GetSFX_volume());
        std::snprintf(values[3], 16, "%s", game.options.options.GetMusic_on() ? "ON" : "OFF");
        std::snprintf(values[4], 16, "%s", game.options.options.GetInvertYAxis() ? "ON" : "OFF");
        std::snprintf(values[5], 16, "%.2f", game.options.options.GetSafeZone());
        for (int i = 0; i < 6; ++i) {
            int r = i == game.menu ? 255 : 180;
            int g = i == game.menu ? 220 : 180;
            int b = i == game.menu ? 40 : 180;
            DrawText(110, 84 + i * 20, labels[i], r, g, b);
            DrawText(280, 84 + i * 20, values[i], r, g, b);
        }
    }
    DrawFinish();
}

}  // namespace

void GameBoot() {
    GameState& game = State();
    if (game.booted) {
        return;
    }
    static_assert(offsetof(OptionsBlock, save) == sizeof(Options), "Options save must follow the object");
    NuInputDevicePS::ClassInitPS();
    HostSetPad(0, 0, 0.f, 0.f, 0.f, 0.f);
    BuildCity(game);
    DefaultOptions(game);
    game.x = 0.f;
    game.y = 0.f;
    game.z = 6.f;
    game.vy = 0.f;
    game.yaw = 0.f;
    game.pitch = -0.12f;
    game.grounded = true;
    game.paused = false;
    game.menu = 0;
    game.prevButtons = 0;
    game.time = 0.f;
    game.collected = 0;
    game.wonTune = false;
    LoadGame(game);
    game.booted = true;
    game.pad.Update(0.f, false);
    Render(game);
    std::snprintf(game.status, sizeof(game.status), "STUDS %02d/%02d", game.collected, kStudCount);
}

void GameFrame(float dt) {
    GameState& game = State();
    if (!game.booted) {
        GameBoot();
    }
    if (dt < 0.f) {
        dt = 0.f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    AssetsPump();
    game.time += dt;
    game.pad.Update(dt, false);
    unsigned buttons = game.pad.GetButtons();
    bool pauseEdge = (buttons & HOST_BTN_PLUS) && !(game.prevButtons & HOST_BTN_PLUS);
    if (pauseEdge) {
        game.paused = !game.paused;
    }
    if (game.paused) {
        Menu(game, buttons);
    } else {
        Simulate(game, dt);
    }
    game.prevButtons = buttons;
    if (game.pad.GetAnalogValue(NU_ANALOG_LEFT_STICK_X) == 0.f && (buttons & HOST_BTN_A) == 0) {
        game.pad.SetMotors(0.f, 0.f);
    }
    Render(game);
    HostAudioRender(dt);
    std::snprintf(game.status, sizeof(game.status), "STUDS %02d/%02d%s", game.collected, kStudCount,
                  game.paused ? " PAUSED" : "");
}

int GameStuds() {
    return State().collected;
}
int GameStudTotal() {
    return kStudCount;
}
void GameDebugSetPlayer(float x, float y, float z) {
    GameState& game = State();
    game.x = x;
    game.y = y;
    game.z = z;
    game.vy = 0.f;
}
void GameDebugSetYaw(float yaw) {
    State().yaw = yaw;
}
float GamePlayerX() {
    return State().x;
}
float GamePlayerY() {
    return State().y;
}
float GamePlayerZ() {
    return State().z;
}
bool GameGrounded() {
    return State().grounded;
}
const char* GameStatus() {
    return State().status;
}
