#pragma once

#include "core/string/ustring.h"

class AISystemPrompt {
public:
	// Returns the full static base prompt.
	// Everything in this string is the "cacheable" portion — it does not include
	// any project-specific or session-specific context (that is injected later
	// in _get_system_prompt by the panel). Providers that support prompt caching
	// (Anthropic cache_control, OpenAI prefix caching) can treat this entire
	// string as the static prefix that does not change between turns.
	static String get_base_prompt();

	// Marker string inserted into the base prompt to delineate where the
	// globally-cacheable static content ends and per-session content begins.
	// Currently unused in the API request (the split happens conceptually), but
	// available so providers can find the boundary if they implement caching.
	static constexpr const char *CACHE_BOUNDARY = "<!-- GODOTAI_CACHE_BOUNDARY -->";
};
