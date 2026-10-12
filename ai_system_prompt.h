#pragma once

#include "core/string/ustring.h"

// The static part of the system prompt. Its text lives in
// knowledge/system_prompt.md; see AIKnowledge.
class AISystemPrompt {
public:
	// The base prompt with the project's res://godot_ai.md (if any) inserted.
	// Everything in it is stable between turns, so providers with prompt
	// caching can treat it as the cacheable prefix.
	static String get_base_prompt();

	// Inserts the project instructions into a prompt template (pure).
	static String build(const String &p_template, const String &p_project_instructions);

	// Marks where the cacheable static content ends.
	static constexpr const char *CACHE_BOUNDARY = "<!-- GODOTAI_CACHE_BOUNDARY -->";
	// Where the project's godot_ai.md goes in the template.
	static constexpr const char *PROJECT_INSTRUCTIONS_PLACEHOLDER = "{{project_instructions}}";
	static constexpr const char *PROJECT_INSTRUCTIONS_FILE = "res://godot_ai.md";
};
