#pragma once

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// The assistant's settings. Every "ai_assistant/*" value is read and written
// through here and nowhere else, so the rest of the module does not know that
// they live in the editor settings — and can be given other values in a test.
//
// Names are given without the "ai_assistant/" prefix ("autorun"); a name that
// carries it is taken as is. Without an editor (unit tests, a non-editor
// build) every read returns its default and writes do nothing.
class AISettings {
public:
	static Variant get(const String &p_name, const Variant &p_default);
	static bool has(const String &p_name);
	static void set(const String &p_name, const Variant &p_value);
	// Writes the settings file now instead of when the editor next does.
	static void save();
	// Whether there is anything to read from or write to.
	static bool is_available();

	// A setting of the editor itself, by its full name ("network/http_proxy/host").
	static Variant get_editor(const String &p_full_name, const Variant &p_default);

	// Stands in for the editor settings until cleared: reads and writes go to
	// these values (full names as keys are accepted too) and nothing is saved.
	static void set_overrides_for_tests(const Dictionary &p_values);
	static void clear_overrides_for_tests();
	static Dictionary get_overrides_for_tests();

	static constexpr const char *PREFIX = "ai_assistant/";
};
