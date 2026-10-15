#include "host_state.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, js_beep, (float freq, float dur, float vol), {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) {
        return;
    }
    const ctx = Module.audioCtx || (Module.audioCtx = new AudioCtx());
    if (ctx.state === "suspended") {
        ctx.resume();
    }
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = "square";
    osc.frequency.value = freq;
    gain.gain.value = Math.max(0, Math.min(0.2, vol));
    osc.connect(gain);
    gain.connect(ctx.destination);
    const now = ctx.currentTime;
    osc.start(now);
    osc.stop(now + Math.max(0.02, dur));
});
#endif

namespace {

struct Voice {
    float freq;
    float time;
    float duration;
    bool music;
    bool active;
};

Voice g_voices[8];
float g_master = 1.f;
float g_music = 1.f;
float g_sfx = 1.f;
bool g_musicOn = true;

}  // namespace

void HostAudioSet(float master, float music, float sfx, bool musicOn) {
    g_master = std::max(0.f, master);
    g_music = std::max(0.f, music);
    g_sfx = std::max(0.f, sfx);
    g_musicOn = musicOn;
}

void HostAudioSilence() {
    for (Voice& voice : g_voices) {
        voice.active = false;
    }
}

void HostAudioPlay(float freq, float duration, bool music) {
    if (freq <= 0.f || duration <= 0.f) {
        return;
    }
    if (music && !g_musicOn) {
        return;
    }
    float level = g_master * (music ? g_music : g_sfx);
    if (level <= 0.f) {
        return;
    }
    for (Voice& voice : g_voices) {
        if (!voice.active) {
            voice.freq = freq;
            voice.time = 0.f;
            voice.duration = duration;
            voice.music = music;
            voice.active = true;
#ifdef __EMSCRIPTEN__
            js_beep(freq, duration, level);
#endif
            return;
        }
    }
}

float HostAudioRender(float dt) {
    if (dt < 0.f) {
        dt = 0.f;
    }
    float sample = 0.f;
    for (Voice& voice : g_voices) {
        if (!voice.active) {
            continue;
        }
        if (voice.music && !g_musicOn) {
            voice.active = false;
            continue;
        }
        float level = g_master * (voice.music ? g_music : g_sfx);
        float env = 1.f - (voice.time / voice.duration);
        if (env < 0.f) {
            env = 0.f;
        }
        sample += std::sin(voice.time * voice.freq * 6.2831853f) * env * level;
        voice.time += dt;
        if (voice.time >= voice.duration) {
            voice.active = false;
        }
    }
    if (sample > 1.f) {
        sample = 1.f;
    }
    if (sample < -1.f) {
        sample = -1.f;
    }
    return sample;
}
