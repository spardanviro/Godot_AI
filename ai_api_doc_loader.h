#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

// Lazy-loaded Godot API documentation.
// Detects class names mentioned in the user message and injects compact
// API references, avoiding the token cost of static per-class documentation.
class AIAPIDocLoader {
public:
	// Returns a compact API reference block for every Godot class name found in
	// p_message. Returns an empty string if no known class is mentioned.
	static String get_docs_for_message(const String &p_message);

private:
	static const HashMap<StringName, String> &_get_class_docs();
	static bool _word_in_string(const String &p_haystack, const String &p_word);
};

#endif // TOOLS_ENABLED
