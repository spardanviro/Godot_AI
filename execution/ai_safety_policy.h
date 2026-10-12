#pragma once

#include "core/string/ustring.h"

// What generated code is not allowed to contain, checked on its text before
// it is compiled: process spawning, reflection that reaches the same APIs,
// the assistant's own classes and settings, the editor's root, network calls
// from inside the editor, paths outside the project. The rules and their
// limits are described in ai_safety_policy.cpp.
//
// This is a static check, not a sandbox: it cannot see what a script
// computes at run time.
class AISafetyPolicy {
public:
	// Empty if the code passes, otherwise the reason it is rejected.
	static String check(const String &p_code);
};
