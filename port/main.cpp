#include "assets.h"
#include "city_game.h"
#include "draw.h"
#include "host_pad.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define LCU_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define LCU_EXPORT
#endif

extern "C" {

LCU_EXPORT void host_boot(void) {
    GameBoot();
}

LCU_EXPORT void host_frame(float dt) {
    GameFrame(dt);
}

LCU_EXPORT void host_set_pad(unsigned buttons, float lx, float ly, float rx, float ry) {
    HostSetPad(0, buttons, lx, ly, rx, ry);
}

LCU_EXPORT void host_set_language(const char* code) {
    HostSetLanguage(code);
}

LCU_EXPORT const unsigned char* host_framebuffer(void) {
    return DrawPixels();
}

LCU_EXPORT int host_fb_width(void) {
    return DrawWidth();
}

LCU_EXPORT int host_fb_height(void) {
    return DrawHeight();
}

LCU_EXPORT int host_submit_asset(const char* name, const unsigned char* data, int size) {
    return AssetsSubmit(name, data, size);
}

LCU_EXPORT void host_clear_assets(void) {
    AssetsClear();
}

LCU_EXPORT int host_stud_count(void) {
    return GameStuds();
}

LCU_EXPORT const char* host_get_status(void) {
    return GameStatus();
}

LCU_EXPORT int host_mount_dir(const char* path) {
    return AssetsMountDirectory(path);
}

}  // extern "C"

#ifdef __EMSCRIPTEN__
int main(int, char**) {
    return 0;
}
#else
int main(int argc, char** argv) {
    bool test = false;
    int frames = 0;
    const char* shot = nullptr;
    const char* mount = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0) {
            test = true;
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = argv[++i];
        } else if (std::strcmp(argv[i], "--mount") == 0 && i + 1 < argc) {
            mount = argv[++i];
        }
    }
    if (test) {
        return GameRunTests();
    }
    if (mount != nullptr) {
        host_mount_dir(mount);
    }
    host_boot();
    if (frames <= 0) {
        frames = 1;
    }
    for (int i = 0; i < frames; ++i) {
        float ly = i < frames / 2 ? 1.f : 0.f;
        host_set_pad(0, 0.f, ly, 0.2f, 0.f);
        host_frame(1.f / 60.f);
    }
    if (shot != nullptr) {
        FILE* out = std::fopen(shot, "wb");
        if (out != nullptr) {
            int w = host_fb_width();
            int h = host_fb_height();
            std::fprintf(out, "P6\n%d %d\n255\n", w, h);
            const unsigned char* px = host_framebuffer();
            for (int i = 0; i < w * h; ++i) {
                std::fwrite(px + i * 4, 1, 3, out);
            }
            std::fclose(out);
        }
    }
    return 0;
}
#endif
