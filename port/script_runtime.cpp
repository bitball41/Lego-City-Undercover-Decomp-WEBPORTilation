#include "script/actions/SAction.h"
#include "script/common/ScriptContext.h"
#include "script/commands/SCmdAdd.h"
#include "script/commands/SCmdAnd.h"
#include "script/commands/SCmdDiv.h"
#include "script/commands/SCmdEqualityOp.h"
#include "script/commands/SCmdMul.h"
#include "script/commands/SCmdOr.h"
#include "script/commands/SCmdSub.h"
#include "script/common/ScriptVariableTags.h"

#include <type_traits>

template <>
void ScriptContext::SetReturn<SVarBool, bool>(int index, bool value) {
    if (index < 0 || index >= 16) {
        return;
    }
    rets[index].number = value ? 1.f : 0.f;
    rets[index].flag = value ? 1 : 0;
    rets[index].text = nullptr;
}

template <>
void ScriptContext::SetReturn<SVarFloat, float>(int index, float value) {
    if (index < 0 || index >= 16) {
        return;
    }
    rets[index].number = value;
    rets[index].flag = value != 0.f ? 1 : 0;
    rets[index].text = nullptr;
}

template <typename Derived>
ActionState SCmdArithmeticOp<Derived>::Exec(ScriptContext& context) {
    float left = context.ReadFloat(0);
    float right = context.ReadFloat(1);
    if (std::is_same<Derived, SCmdDiv>::value && right == 0.f) {
        context.SetReturn<SVarFloat>(0, 0.f);
        return ACTION_FINISHED;
    }
    context.SetReturn<SVarFloat>(0, Derived::Op(left, right));
    return ACTION_FINISHED;
}

template ActionState SCmdArithmeticOp<SCmdAdd>::Exec(ScriptContext&);
template ActionState SCmdArithmeticOp<SCmdSub>::Exec(ScriptContext&);
template ActionState SCmdArithmeticOp<SCmdMul>::Exec(ScriptContext&);
template ActionState SCmdArithmeticOp<SCmdDiv>::Exec(ScriptContext&);

template <typename Derived>
ActionState SCmdLogicOp<Derived>::Exec(ScriptContext& context) {
    bool result = Derived::Op(context.ReadBool(0), context.ReadBool(1));
    context.SetReturn<SVarBool>(0, result);
    return ACTION_FINISHED;
}

template ActionState SCmdLogicOp<SCmdAnd>::Exec(ScriptContext&);
template ActionState SCmdLogicOp<SCmdOr>::Exec(ScriptContext&);

ActionState SCmdEqualityOp::Exec(ScriptContext& context) {
    bool result = Op(context.ReadFloat(0), context.ReadFloat(1));
    context.SetReturn<SVarBool>(0, result);
    return ACTION_FINISHED;
}

void ScriptCommand::ConvertTypes(ScriptContext& context) {
    if (m_converters == nullptr) {
        return;
    }
    SCmdParams inputs;
    GetInputs(inputs);
    int count = inputs.m_CurrentParamIndex;
    if (count < 0) {
        count = 0;
    }
    if (count > 64) {
        count = 64;
    }
    for (int i = 0; i < count; ++i) {
        if (m_converters[i] != nullptr) {
            m_converters[i]->Exec(context);
        }
    }
}
