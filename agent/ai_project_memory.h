#pragma once

#include "core/string/ustring.h"

// res://godot_ai.md: project instructions written by the user, plus quirks the
// model discovered and asked to remember ([WRITE_MEMORY: ...]). The file is
// loaded into every future system prompt.
class AIProjectMemory {
public:
	// Returns p_existing_text with the note filed under the quirks section
	// (created if missing). Pure.
	static String insert_note(const String &p_existing_text, const String &p_note, const String &p_date);

	// Appends a note to the file. p_note must already be sanitised (see
	// AIResponseAnalysis::sanitize_memory_note). Returns false if there is no
	// project, or the file exists but cannot be read (writing then would
	// replace its contents with just this one entry) or cannot be written.
	static bool append_note(const String &p_note);
};
