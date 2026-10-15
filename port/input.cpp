#include "host_pad.h"
#include "nn/hid.h"
#include "nn/oe.h"
#include "nn/settings.h"
#include "nucore/NuInputDevice.h"
#include "nucore/NuInputDevicePS.h"

#include <cstring>

namespace {

struct HostPad {
    bool connected;
    unsigned buttons;
    float analog[NU_ANALOG_MAX];
    float motion[NU_MOTION_MAX];
    float low;
    float high;
};

HostPad g_pads[9];
char g_language[8] = {'e', 'n', '-', 'G', 'B', 0, 0, 0};

HostPad* Pad(unsigned port) {
    if (port > 8) {
        return nullptr;
    }
    return &g_pads[port];
}

}  // namespace

void HostSetPad(int port, unsigned buttons, float lx, float ly, float rx, float ry) {
    HostPad* pad = Pad(static_cast<unsigned>(port));
    if (pad == nullptr) {
        return;
    }
    pad->connected = true;
    pad->buttons = buttons;
    pad->analog[NU_ANALOG_LEFT_STICK_X] = lx;
    pad->analog[NU_ANALOG_LEFT_STICK_Y] = ly;
    pad->analog[NU_ANALOG_RIGHT_STICK_X] = rx;
    pad->analog[NU_ANALOG_RIGHT_STICK_Y] = ry;
}

void HostSetLanguage(const char* code) {
    std::memset(g_language, 0, sizeof(g_language));
    if (code == nullptr) {
        return;
    }
    for (int i = 0; i < 7 && code[i] != '\0'; ++i) {
        g_language[i] = code[i];
    }
}

const char* HostLanguageCode() {
    return g_language;
}

namespace NuInputDevicePS {
u32 m_npadIDs[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};

int ClassInitPS() {
    return 1;
}
void UpdateAllPS(float) {}
int GetTypePS(unsigned int port) {
    HostPad* pad = Pad(port);
    return pad != nullptr && pad->connected ? 9 : 0;
}
void ReadButtonsPS(unsigned int port, unsigned int* buttons) {
    if (buttons == nullptr) {
        return;
    }
    HostPad* pad = Pad(port);
    *buttons = pad != nullptr && pad->connected ? pad->buttons : 0;
}
void ReadAnalogValuesPS(unsigned int port, float* analogValues) {
    if (analogValues == nullptr) {
        return;
    }
    HostPad* pad = Pad(port);
    if (pad == nullptr || !pad->connected) {
        std::memset(analogValues, 0, sizeof(float) * NU_ANALOG_MAX);
        return;
    }
    std::memcpy(analogValues, pad->analog, sizeof(float) * NU_ANALOG_MAX);
}
void ReadMotionValuesPS(unsigned int port, float* motionValues) {
    if (motionValues == nullptr) {
        return;
    }
    HostPad* pad = Pad(port);
    if (pad == nullptr) {
        std::memset(motionValues, 0, sizeof(float) * NU_MOTION_MAX);
        return;
    }
    std::memcpy(motionValues, pad->motion, sizeof(float) * NU_MOTION_MAX);
}
void SetMotorsPS(unsigned int port, float lowFreq, float highFreq) {
    HostPad* pad = Pad(port);
    if (pad == nullptr) {
        return;
    }
    pad->low = lowFreq;
    pad->high = highFreq;
}
}  // namespace NuInputDevicePS

namespace nn::hid {
NpadStyleSet GetNpadStyleSet(const u32& id) {
    NpadStyleSet style;
    if (id < 9 && g_pads[id].connected) {
        style.Set(0, true);
    }
    return style;
}
}  // namespace nn::hid

namespace nn::oe {
nn::settings::LanguageCode GetDesiredLanguage() {
    nn::settings::LanguageCode code{};
    std::memcpy(code.code, g_language, 8);
    return code;
}
}  // namespace nn::oe

NuInputDevice::NuInputDevice(unsigned int port)
    : m_Port(port),
      m_CafeSpecificId(0),
      m_Type(0),
      m_LastValidType(0),
      m_AttachmentType(0),
      m_Caps(0),
      m_IsConnected(false),
      m_IsIntercepted(false),
      m_Buttons(0),
      m_AnalogValues{},
      m_MotionValues{},
      m_PrevTouchData{},
      m_TouchData{},
      m_MouseData{},
      m_LowFreqMotor(0.f),
      m_HighFreqMotor(0.f),
      m_RumbleKilled(false),
      m_RumbleIdleTimer(0.f),
      m_RumbleDuration(0.f),
      m_HasHeadphones(false),
      m_Volume(1.f),
      m_Translators{},
      m_TranslatorCount(0),
      m_SortKey(static_cast<int>(port)) {}

void NuInputDevice::Update(float, bool) {
    if (!NuInputDevicePS::IsConnectedPS(m_Port)) {
        SetDisconnected();
        return;
    }
    m_IsConnected = true;
    m_Type = NuInputDevicePS::GetTypePS(m_Port);
    m_LastValidType = m_Type;
    m_Caps = NuInputDevicePS::GetCapsPS(m_Port);
    unsigned int buttons = 0;
    NuInputDevicePS::ReadButtonsPS(m_Port, &buttons);
    m_Buttons = buttons;
    NuInputDevicePS::ReadAnalogValuesPS(m_Port, m_AnalogValues);
    NuInputDevicePS::ReadMotionValuesPS(m_Port, m_MotionValues);
    NuInputDevicePS::ReadTouchDataPS(m_Port, &m_TouchData);
    NuInputDevicePS::ReadMouseDataPS(m_Port, &m_MouseData);
    float stickDead = NuInputDevicePS::GetStickDeadZone(m_Port);
    DeadZone(NU_ANALOG_LEFT_STICK_X, stickDead);
    DeadZone(NU_ANALOG_LEFT_STICK_Y, stickDead);
    DeadZone(NU_ANALOG_RIGHT_STICK_X, stickDead);
    DeadZone(NU_ANALOG_RIGHT_STICK_Y, stickDead);
    if (m_RumbleKilled) {
        m_LowFreqMotor = 0.f;
        m_HighFreqMotor = 0.f;
    }
    NuInputDevicePS::SetMotorsPS(m_Port, m_LowFreqMotor, m_HighFreqMotor);
    m_HasHeadphones = NuInputDevicePS::HasHeadphonesConnectedPS(m_Port);
    m_Volume = NuInputDevicePS::GetVolumePS(m_Port);
}

void NuInputDevice::Clear() {
    m_Buttons = 0;
    std::memset(m_AnalogValues, 0, sizeof(m_AnalogValues));
    std::memset(m_MotionValues, 0, sizeof(m_MotionValues));
    std::memset(&m_TouchData, 0, sizeof(m_TouchData));
    std::memset(&m_MouseData, 0, sizeof(m_MouseData));
}

void NuInputDevice::AddTranslator(NuInputDeviceTranslator* translator) {
    if (translator == nullptr || m_TranslatorCount >= 2) {
        return;
    }
    m_Translators[m_TranslatorCount++] = translator;
}

void NuInputDevice::ConvertToEmulatedTouchFromMouse() {}

void NuInputDevice::ProcessTouchData() {
    m_PrevTouchData = m_TouchData;
}
