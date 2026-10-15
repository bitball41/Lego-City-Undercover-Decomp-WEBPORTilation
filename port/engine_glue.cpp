#include "NuUnknownDefaultBase.h"
#include "NuUnknownDefaultBaseNoAutoDelete.h"
#include "host_state.h"
#include "legoapi/optionssave.h"
#include "legoapi/players.h"
#include "script/actions/CityPDA_Scanner/CityPDA_CommunicatorScriptAction.h"
#include "script/actions/SAction_AtkMngr_Command.h"

#include <cstring>

MechCharacter* player = nullptr;
MechCharacter* player2 = nullptr;
volatile int bricktotal_superbuildtarget = 0;

class SkyDiveAddon {
public:
    static bool m_bEnableWindSound;
};

bool SkyDiveAddon::m_bEnableWindSound = true;

void Hint_CancelCurrent(float) {}

void cAttackManager::CalcSuspendPositions() {}

const char* CityPDA_CommunicatorScriptAction::GetName() const {
    return "PDA";
}

NuUnknownDefaultBase::~NuUnknownDefaultBase() = default;
NuUnknownDefaultBaseNoAutoDelete::~NuUnknownDefaultBaseNoAutoDelete() = default;

namespace {
HostVideo g_video{50, 50, 1.f};
}

HostVideo& HostVideoState() {
    return g_video;
}

void NuVideoSetBrightness(int brightness, int contrast) {
    g_video.brightness = brightness;
    g_video.contrast = contrast;
}

void SetUISafeZone(float safeZone) {
    g_video.safeZone = safeZone;
}

void OPTIONSSAVE::CycleOptionsSelectedSplitMode(bool value) {
    int mode = static_cast<int>(split_screen_mode);
    mode += value ? 1 : -1;
    if (mode < 0) {
        mode = 0;
    }
    if (mode > 3) {
        mode = 3;
    }
    split_screen_mode = static_cast<cSplitScreenSegmentInterfaceMgr::eOptionsSelectedSplitMode>(mode);
}
