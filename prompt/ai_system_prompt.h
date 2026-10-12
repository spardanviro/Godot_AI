#pragma once

#include "../providers/ai_provider.h"

#include "core/string/ustring.h"

// The static part of the system prompt. Its text lives in
// knowledge/system_prompt.md; see AIKnowledge.
class AISystemPrompt {
public:
	// The base prompt with the project's res://godot_ai.md (if any) inserted.
	// Everything in it is stable between turns, so providers with prompt
	// caching can treat it as the cacheable prefix.
	// p_tools: the variant for a model that works with tools instead of
	// writing code blocks to be run.
	static String get_base_prompt(bool p_tools = false);

	// The template holds both variants: what is only for one of them sits
	// between marker lines (SCRIPT-ONLY / TOOLS-ONLY). Returns the text of
	// one variant, without the markers (pure).
	static String select_variant(const String &p_template, bool p_tools);

	// Inserts the project instructions into a prompt template (pure).
	static String build(const String &p_template, const String &p_project_instructions);

	// Marks where the cacheable static content ends.
	static constexpr const char *CACHE_BOUNDARY = AIProvider::CACHE_BOUNDARY;
	// Where the project's godot_ai.md goes in the template.
	static constexpr const char *PROJECT_INSTRUCTIONS_PLACEHOLDER = "{{project_instructions}}";
	static constexpr const char *PROJECT_INSTRUCTIONS_FILE = "res://godot_ai.md";
};
