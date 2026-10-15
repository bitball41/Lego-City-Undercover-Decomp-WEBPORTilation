#pragma once

#include <cstdint>

// Host-side pad bits fed into NuInputDevice. These are the browser/keyboard
// mapping until the original Switch bitfield is matched from a dump.
enum HostButton : unsigned {
    HOST_BTN_A = 1u << 0,
    HOST_BTN_B = 1u << 1,
    HOST_BTN_X = 1u << 2,
    HOST_BTN_Y = 1u << 3,
    HOST_BTN_L = 1u << 4,
    HOST_BTN_R = 1u << 5,
    HOST_BTN_ZL = 1u << 6,
    HOST_BTN_ZR = 1u << 7,
    HOST_BTN_PLUS = 1u << 8,
    HOST_BTN_MINUS = 1u << 9,
    HOST_BTN_UP = 1u << 10,
    HOST_BTN_DOWN = 1u << 11,
    HOST_BTN_LEFT = 1u << 12,
    HOST_BTN_RIGHT = 1u << 13,
};

void HostSetPad(int port, unsigned buttons, float lx, float ly, float rx, float ry);
void HostSetLanguage(const char* code);
const char* HostLanguageCode();
