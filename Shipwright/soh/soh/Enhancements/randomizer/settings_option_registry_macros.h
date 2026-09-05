#pragma once

#include "settings_option_lists.h"

// Registration DSL shared by the responsibility-specific registry modules.
#define OPT_U8(rsk, ...) mOptions[rsk] = Option::U8(rsk, __VA_ARGS__)
#define OPT_BOOL(rsk, ...) mOptions[rsk] = Option::Bool(rsk, __VA_ARGS__)
#define OPT_TRICK(rsk, ...) mTrickSettings[rsk] = TrickSetting::LogicTrick(rsk, __VA_ARGS__)
#define OPT_CALLBACK(rsk, body) mOptions[rsk].SetCallback([this](WidgetInfo & info) body)
#define OPT_CALLBACK_FN(rsk, fn) mOptions[rsk].SetCallback(fn)
