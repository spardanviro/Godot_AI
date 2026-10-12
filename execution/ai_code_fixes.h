#pragma once

#include "core/string/ustring.h"

// Repairs for mistakes models keep making in generated GDScript, applied to
// the text before it is compiled: set_owner() written before add_child(),
// and a lambda left standing on its own. Code that has neither comes back
// unchanged.
class AICodeFixes {
public:
	static String apply(const String &p_code);
};
