#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

class AIResponseParser : public RefCounted {
	GDCLASS(AIResponseParser, RefCounted);

protected:
	static void _bind_methods();

public:
	struct ParseResult {
		Vector<String> text_segments;
		Vector<String> code_blocks;
	};

	// Parse AI response text and extract GDScript code blocks.
	// Returns a Dictionary with "text_segments" (Array of String) and "code_blocks" (Array of String).
	Dictionary parse(const String &p_response) const;

	// Check if a response contains executable code blocks.
	bool has_code_blocks(const String &p_response) const;

	// Extract just the first code block from a response.
	String extract_first_code_block(const String &p_response) const;

	AIResponseParser();
};
