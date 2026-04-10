#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// AIGotchasIndex
// On-demand Godot gotchas lookup. Given a user message, returns only the
// gotcha entries whose keywords appear in that message (word-boundary matched).
// Maximum ~3000 chars returned to avoid over-injecting context.
class AIGotchasIndex {
public:
	// Returns relevant gotcha text for p_message, or empty string if none match.
	static String get_for_message(const String &p_message);

	struct GotchaEntry {
		Vector<String> keywords;
		String content;
	};

private:
	static const Vector<GotchaEntry> &_get_entries();
	static bool _word_in_string(const String &p_haystack, const String &p_word);
};

#endif // TOOLS_ENABLED
