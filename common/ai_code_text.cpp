#include "ai_code_text.h"

#include "core/io/ip_address.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"

String AICodeText::summarize_code(const String &p_code) {
	// The last print("...") literal is usually the script's own summary.
	static const String PRINT_OPEN = "print(\"";
	String last_print;
	int search_from = 0;
	while (true) {
		const int print_pos = p_code.find(PRINT_OPEN, search_from);
		if (print_pos < 0) {
			break;
		}
		const int text_start = print_pos + PRINT_OPEN.length();
		const int text_end = p_code.find("\"", text_start);
		if (text_end > text_start) {
			last_print = p_code.substr(text_start, text_end - text_start);
			search_from = text_end + 1;
		} else {
			search_from = text_start + 1;
		}
	}
	if (!last_print.is_empty()) {
		return last_print;
	}

	if (p_code.find("add_child") >= 0) {
		static const String NAME_ASSIGN = ".name = \"";
		const int name_pos = p_code.find(NAME_ASSIGN);
		if (name_pos >= 0) {
			const int name_start = name_pos + NAME_ASSIGN.length();
			const int name_end = p_code.find("\"", name_start);
			if (name_end > name_start) {
				return "Added node '" + p_code.substr(name_start, name_end - name_start) + "' to the scene.";
			}
		}
		return "Node added to the scene.";
	}
	if (p_code.find("remove_child") >= 0) {
		return "Node removed from the scene.";
	}
	if (p_code.find("ResourceSaver.save") >= 0) {
		return "Resource saved to disk.";
	}
	if (p_code.find(".source_code") >= 0) {
		return "Script created or modified.";
	}
	if (p_code.find(".position") >= 0 || p_code.find(".rotation") >= 0) {
		return "Node transform modified.";
	}
	return "Code executed successfully.";
}

Vector<String> AICodeText::extract_script_paths(const String &p_error_text) {
	Vector<String> paths;
	HashSet<String> seen;
	int pos = 0;
	while (true) {
		pos = p_error_text.find("res://", pos);
		if (pos < 0) {
			break;
		}
		int end = pos + 6;
		while (end < p_error_text.length()) {
			const char32_t c = p_error_text[end];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'' || c == ')' || c == ']') {
				break;
			}
			// "path.gd:42" — stop before the line number.
			if (c == ':' && end + 1 < p_error_text.length() && is_digit(p_error_text[end + 1])) {
				break;
			}
			end++;
		}
		const String path = p_error_text.substr(pos, end - pos);
		if (path.ends_with(".gd") && !seen.has(path)) {
			seen.insert(path);
			paths.push_back(path);
		}
		pos = end;
	}
	return paths;
}

String AICodeText::build_saved_script(const String &p_code, int p_index, String &r_file_name) {
	static const char *declaration_starts[] = {
		"@tool", "extends ", "class_name ", "func ", "var ", "const ", "signal ",
		"enum ", "class ", "static ", "@export", "@onready", nullptr
	};
	static const String EXTENDS = "extends ";

	const Vector<String> lines = p_code.split("\n");
	r_file_name = "ai_generated_" + itos(p_index) + ".gd";

	// The first line that is not blank or a comment decides.
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}
		for (int d = 0; declaration_starts[d]; d++) {
			if (!line.begins_with(declaration_starts[d])) {
				continue;
			}
			if (line.begins_with(EXTENDS)) {
				r_file_name = line.substr(EXTENDS.length()).strip_edges().to_lower() + "_script_" + itos(p_index) + ".gd";
			}
			return p_code;
		}
		break;
	}

	r_file_name = "ai_editor_script_" + itos(p_index) + ".gd";
	String script = "@tool\nextends EditorScript\n\nfunc _run():\n";
	for (int i = 0; i < lines.size(); i++) {
		script += "\t" + lines[i] + "\n";
	}
	return script;
}

Vector<String> AICodeText::dedupe_consecutive(const Vector<String> &p_blocks) {
	Vector<String> unique;
	for (int i = 0; i < p_blocks.size(); i++) {
		if (i == 0 || p_blocks[i] != p_blocks[i - 1]) {
			unique.push_back(p_blocks[i]);
		}
	}
	return unique;
}
