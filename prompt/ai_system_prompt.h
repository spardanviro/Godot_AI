#pragma once

#include "../providers/ai_provider.h"

#include "core/string/ustring.h"

// The static part of the system prompt. Its text lives in
// knowledge/system_prompt.md; see AIKnowledge.
class AISystemPrompt {
public:
	// How the model is to work, which decides what the prompt teaches.
	enum Variant {
		VARIANT_SCRIPT, // No tools: code blocks to be run, text markers for the web, assets and notes.
		VARIANT_LOOK_UP, // Tools that look things up, nothing that changes the project (ASK and PLAN modes).
		VARIANT_TOOLS, // Tools for everything.
	};

	// The base prompt with the project's res://godot_ai.md (if any) inserted.
	// Everything in it is stable between turns, so providers with prompt
	// caching can treat it as the cacheable prefix.
	static String get_base_prompt(Variant p_variant = VARIANT_SCRIPT);

	// The template holds every variant: what is not for all of them sits
	// between marker lines. SCRIPT-ONLY is for a model that writes code
	// blocks, TOOLS-ONLY for one that edits with tools, MARKERS-ONLY for one
	// that has no tools at all. Returns the text of one variant, without the
	// marker lines (pure).
	static String select_variant(const String &p_template, Variant p_variant);

	// Inserts the project instructions into a prompt template (pure).
	static String build(const String &p_template, const String &p_project_instructions);

	// Marks where the cacheable static content ends.
	static constexpr const char *CACHE_BOUNDARY = AIProvider::CACHE_BOUNDARY;
	// Where the project's godot_ai.md goes in the template.
	static constexpr const char *PROJECT_INSTRUCTIONS_PLACEHOLDER = "{{project_instructions}}";
	static constexpr const char *PROJECT_INSTRUCTIONS_FILE = "res://godot_ai.md";
};
