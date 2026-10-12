#pragma once

#include "core/string/ustring.h"

// Generated code is the body of a function. To run it, it is put inside an
// EditorScript: a fixed header, then the code indented by one tab (and with
// its own indentation turned into tabs, which GDScript requires to be
// uniform).
class AIScriptWrapper {
public:
	static String wrap(const String &p_code);

	// Lines the wrapper puts before the code: a line the parser reports as N
	// is line N - HEADER_LINES of the code.
	static const int HEADER_LINES = 4;
};
