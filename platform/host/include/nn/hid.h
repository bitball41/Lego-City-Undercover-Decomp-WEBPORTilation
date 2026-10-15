#pragma once

#include <nn/types.h>
#include <nn/util/util_BitFlagSet.h>

namespace nn::hid {

enum class NpadStyleTag {
    NpadStyleFullKey = 0,
    NpadStyleHandheld = 1,
    NpadStyleJoyDual = 2,
    NpadStyleJoyLeft = 3,
    NpadStyleJoyRight = 4,
};

typedef nn::util::BitFlagSet<32, NpadStyleTag> NpadStyleSet;

NpadStyleSet GetNpadStyleSet(const u32& id);

}  // namespace nn::hid
