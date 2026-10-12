#pragma once

#include "core/string/ustring.h"

// What changes between two versions of a text file, line by line, for a
// person to read before allowing the change:
//
//     func _ready() -> void:
//   -     speed = 100
//   +     speed = 200
//
// Unchanged lines are shown only around the changes; "..." stands for the
// ones left out.
class AITextDiff {
public:
	// Empty if the two are the same.
	static String describe(const String &p_old, const String &p_new, int p_context_lines = DEFAULT_CONTEXT_LINES);

	static const int DEFAULT_CONTEXT_LINES = 3;
	// Past this many line pairs the changed stretch is shown as all removed,
	// then all added, instead of being matched up line by line.
	static const int MAX_COMPARED_CELLS = 4000000;
};
