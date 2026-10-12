#pragma once

#include "ai_localization.h"

#include "core/string/print_string.h"

#define AI_LOG(msg) print_line(String("[Godot AI] ") + msg)
#define AI_ERR(msg) ERR_PRINT(String("[Godot AI] ") + msg)
#define AI_WARN(msg) WARN_PRINT(String("[Godot AI] ") + msg)
// Inline two-language text for messages that have no entry in AILocalization.
#define AI_L(en, zh) (AILocalization::get_language() == "zh" ? String::utf8(zh) : String::utf8(en))
