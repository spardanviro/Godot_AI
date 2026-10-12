#include "ai_script_wrapper.h"

#include "core/string/char_utils.h"
#include "core/templates/vector.h"

// The wrapper indents with a tab, and GDScript rejects a file that mixes tabs
// and spaces, so space-indented model output (common with 4-space habits) has
// to be converted. Lines inside triple-quoted strings are data and left alone.
static String _normalize_indentation(const String &p_code) {
	Vector<String> lines = p_code.replace("\r", "").split("\n");

	// Indent unit = smallest space indentation in use (usually 4 or 2).
	int unit = 0;
	for (int i = 0; i < lines.size(); i++) {
		int spaces = 0;
		while (spaces < lines[i].length() && lines[i][spaces] == ' ') {
			spaces++;
		}
		if (spaces > 0 && spaces < lines[i].length() && (unit == 0 || spaces < unit)) {
			unit = spaces;
		}
	}
	if (unit == 0) {
		return String("\n").join(lines); // No space indentation anywhere.
	}

	bool in_multiline_string = false;
	for (int i = 0; i < lines.size(); i++) {
		const bool starts_inside_string = in_multiline_string;
		const int quote_marks = lines[i].count("\"\"\"") + lines[i].count("'''");
		if (quote_marks % 2 == 1) {
			in_multiline_string = !in_multiline_string;
		}
		if (starts_inside_string) {
			continue;
		}

		int pos = 0;
		int tabs = 0;
		int spaces = 0;
		while (pos < lines[i].length() && (lines[i][pos] == ' ' || lines[i][pos] == '\t')) {
			if (lines[i][pos] == '\t') {
				tabs++;
			} else {
				spaces++;
			}
			pos++;
		}
		if (spaces > 0) {
			lines.write[i] = String("\t").repeat(tabs + (spaces + unit / 2) / unit) + lines[i].substr(pos);
		}
	}
	return String("\n").join(lines);
}

String AIScriptWrapper::wrap(const String &p_code) {
	String wrapped;
	wrapped += "@tool\n";
	wrapped += "extends EditorScript\n\n";
	wrapped += "func _run():\n";

	// Indent each line of the user code.
	Vector<String> lines = _normalize_indentation(p_code).split("\n");
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges().is_empty()) {
			wrapped += "\n";
		} else {
			wrapped += "\t" + lines[i] + "\n";
		}
	}

	return wrapped;
}
