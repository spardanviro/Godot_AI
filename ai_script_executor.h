#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class AIScriptExecutor : public RefCounted {
	GDCLASS(AIScriptExecutor, RefCounted);

	// Dangerous API calls to block.
	Vector<String> blocklist;

	void _init_blocklist();

protected:
	static void _bind_methods();

public:
	// Wrap raw code in an EditorScript template.
	String wrap_in_editor_script(const String &p_code, const String &p_action_name = "AI Action") const;

	// Check code for dangerous API calls. Returns empty string if safe, error message if blocked.
	String check_safety(const String &p_code) const;

	// Compile and execute GDScript code in the editor context.
	// Returns a Dictionary with "success" (bool), "output" (String), "error" (String).
	Dictionary execute(const String &p_code, const String &p_action_name = "AI Action");

	// Compile-only check (no execution). Returns error message or empty string.
	String compile_check(const String &p_code) const;

	AIScriptExecutor();
};
