#pragma once

#include "script/common/ScriptVariableTags.h"

struct ScriptSlot {
    float number;
    int flag;
    const char* text;
};

struct ScriptContext {
    ScriptSlot args[32];
    int argCount;
    ScriptSlot rets[16];

    float ReadFloat(int index) const {
        if (index < 0 || index >= 32) {
            return 0.f;
        }
        return args[index].number;
    }
    bool ReadBool(int index) const {
        if (index < 0 || index >= 32) {
            return false;
        }
        return args[index].flag != 0 || args[index].number != 0.f;
    }
    void SetNumberArg(int index, float value) {
        if (index < 0 || index >= 32) {
            return;
        }
        args[index].number = value;
        args[index].flag = value != 0.f ? 1 : 0;
        args[index].text = nullptr;
        if (index >= argCount) {
            argCount = index + 1;
        }
    }
    void SetBoolArg(int index, bool value) {
        if (index < 0 || index >= 32) {
            return;
        }
        args[index].number = value ? 1.f : 0.f;
        args[index].flag = value ? 1 : 0;
        args[index].text = nullptr;
        if (index >= argCount) {
            argCount = index + 1;
        }
    }
    float ReturnFloat(int index) const {
        if (index < 0 || index >= 16) {
            return 0.f;
        }
        return rets[index].number;
    }
    bool ReturnBool(int index) const {
        if (index < 0 || index >= 16) {
            return false;
        }
        return rets[index].flag != 0;
    }

    template <typename TTag, typename TValue>
    void SetReturn(int index, TValue value);
};
