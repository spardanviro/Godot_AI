#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Text-level handling of the code blocks in a reply and of the script paths
// in error output. Nothing here touches the editor.
class AICodeText {
public:
	// One-line description of what a code block does, for the chat summary.
	static String summarize_code(const String &p_code);
	// Unique res://*.gd paths mentioned in error output, in order of appearance.
	static Vector<String> extract_script_paths(const String &p_error_text);
	// Turns a code block into the contents of a .gd file. A block that is
	// already a script is kept as is; EditorScript body code is wrapped so the
	// file parses and can be run from the script editor. r_file_name receives a
	// suggested name (p_index keeps names from different blocks apart).
	static String build_saved_script(const String &p_code, int p_index, String &r_file_name);
	// Models sometimes repeat the same block twice in a row.
	static Vector<String> dedupe_consecutive(const Vector<String> &p_blocks);
};
