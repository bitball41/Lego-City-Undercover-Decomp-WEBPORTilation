#pragma once

struct HostVideo {
    int brightness;
    int contrast;
    float safeZone;
};

HostVideo& HostVideoState();

void HostAudioSet(float master, float music, float sfx, bool musicOn);
void HostAudioPlay(float freq, float duration, bool music);
float HostAudioRender(float dt);
void HostAudioSilence();

void HostStorageUseMemory(bool enabled);
void HostStorageWrite(const void* data, int size);
int HostStorageRead(void* data, int maxSize);
