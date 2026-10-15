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
    int seenMeshes;
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
    for (int i = 0; i < kStudCount; ++i) {
        game.studs[i].got = true;
    }
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
    game.collected = AssetsLoaded();
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
    const int palette[6][3] = {{210, 180, 40}, {40, 90, 180}, {180, 40, 40}, {40, 140, 70}, {150, 150, 160}, {90, 50, 30}};
    for (int i = 0; i < AssetsLoaded(); ++i) {
        const HostMesh* mesh = AssetsAt(i);
        if (mesh == nullptr || mesh->xyz.empty()) {
            continue;
        }
        float extent = std::max(mesh->maxX - mesh->minX, std::max(mesh->maxY - mesh->minY, mesh->maxZ - mesh->minZ));
        bool world = extent > 30.f;
        float scale = world ? 1.f : (extent > 0.001f ? 6.f / extent : 1.f);
        int column = i % 8;
        int row = i / 8;
        NUVEC pos = world ? NUVEC{0.f, 0.f, 0.f}
                          : NUVEC{static_cast<float>(column - 3) * 10.f, -mesh->minY * scale,
                                  14.f + static_cast<float>(row) * 10.f};
        const int* color = palette[i % 6];
        DrawMesh(mesh->xyz.data(), static_cast<int>(mesh->xyz.size() / 3), mesh->indices.data(),
                 static_cast<int>(mesh->indices.size()), pos, 0.f, scale, color[0], color[1], color[2]);
    }

    float zone = game.options.options.GetSafeZone();
    int insetX = static_cast<int>((1.f - zone) * kWidth * 0.5f);
    int insetY = static_cast<int>((1.f - zone) * kFbH * 0.5f);
    if (AssetsLoaded() == 0) {
        const char* note = AssetsStatus();
        if (note != nullptr && note[0] != 0) {
            std::snprintf(game.status, sizeof(game.status), "%s", note);
        } else {
            std::snprintf(game.status, sizeof(game.status), "DROP GAME DUMP");
        }
    } else {
        std::snprintf(game.status, sizeof(game.status), "MESHES %d", AssetsLoaded());
    }
    DrawText(16 + insetX, 16 + insetY, game.status, 255, 230, 40);
    if (AssetsLoaded() > 0 || AssetsPending() > 0) {
        char assets[64];
        std::snprintf(assets, sizeof(assets), "LOADED %d  WAIT %d", AssetsLoaded(), AssetsPending());
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
    game.seenMeshes = 0;
    LoadGame(game);
    game.booted = true;
    game.pad.Update(0.f, false);
    Render(game);
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
    if (AssetsLoaded() > game.seenMeshes) {
        const HostMesh* best = nullptr;
        float bestSpan = 0.f;
        for (int i = 0; i < AssetsLoaded(); ++i) {
            const HostMesh* mesh = AssetsAt(i);
            if (mesh == nullptr) {
                continue;
            }
            float span = std::max(mesh->maxX - mesh->minX, std::max(mesh->maxY - mesh->minY, mesh->maxZ - mesh->minZ));
            if (span > bestSpan) {
                bestSpan = span;
                best = mesh;
            }
        }
        if (best != nullptr && bestSpan > 20.f) {
            game.x = (best->minX + best->maxX) * 0.5f;
            game.z = (best->minZ + best->maxZ) * 0.5f - 8.f;
            game.y = best->minY > 0.f ? best->minY : 0.f;
            game.vy = 0.f;
            game.yaw = 0.f;
        }
        game.seenMeshes = AssetsLoaded();
    }
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
    if (game.paused) {
        std::snprintf(game.status + std::strlen(game.status), sizeof(game.status) - std::strlen(game.status), " PAUSED");
    }
}

int GameStuds() {
    return AssetsLoaded();
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
