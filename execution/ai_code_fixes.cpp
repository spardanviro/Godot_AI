#include "ai_code_fixes.h"

#include "ai_script_wrapper.h"

#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "modules/gdscript/gdscript_parser.h"

String AICodeFixes::apply(const String &p_code) {
	String code = p_code;

	// ── Fix 1: set_owner() before add_child() ──────────────────────────
	// Detect  X.set_owner(Y)  appearing BEFORE  Z.add_child(X)  and swap
	// the two lines so add_child comes first.  This is a very common AI
	// mistake that causes "Invalid owner. Owner must be an ancestor in the
	// tree." at runtime.
	{
		Vector<String> lines = code.split("\n");
		bool changed = true;
		// Multiple passes: one swap may expose another pair.
		for (int pass = 0; pass < 10 && changed; pass++) {
			changed = false;
			for (int i = 0; i < lines.size(); i++) {
				String stripped = lines[i].strip_edges();
				// Match  <node>.set_owner(...)
				int so_pos = stripped.find(".set_owner(");
				if (so_pos < 0) {
					continue;
				}
				// Extract the node name before .set_owner(
				String node_name = stripped.substr(0, so_pos).strip_edges();
				if (node_name.is_empty()) {
					continue;
				}
				// Look for a later line with  <something>.add_child(<node_name>)
				String add_child_suffix = ".add_child(" + node_name + ")";
				const String redeclare = "var " + node_name;
				// Only the current instance of the variable counts. If it was
				// already added before set_owner there is nothing to fix, and a
				// later add_child belongs to a re-declared variable of the same
				// name (e.g. the next loop) — moving it would break both.
				bool already_added = false;
				for (int j = i - 1; j >= 0; j--) {
					const String prev = lines[j].strip_edges();
					if (prev.find("add_child(" + node_name + ")") >= 0 || prev.find("add_child(" + node_name + ",") >= 0) {
						already_added = true;
						break;
					}
					if (prev.begins_with(redeclare + " ") || prev.begins_with(redeclare + ":") || prev.begins_with(redeclare + "=")) {
						break;
					}
				}
				if (already_added) {
					continue;
				}
				for (int j = i + 1; j < lines.size(); j++) {
					const String next = lines[j].strip_edges();
					if (next.begins_with(redeclare + " ") || next.begins_with(redeclare + ":") || next.begins_with(redeclare + "=")) {
						break;
					}
					if (lines[j].strip_edges().ends_with(add_child_suffix) ||
							lines[j].strip_edges().find(".add_child(" + node_name + ")") >= 0) {
						// Swap: move add_child line to just before set_owner line.
						String add_line = lines[j];
						lines.remove_at(j);
						lines.insert(i, add_line);
						changed = true;
						break;
					}
				}
			}
		}
		if (lines.size() > 0) {
			code = String();
			for (int j = 0; j < lines.size(); j++) {
				if (j > 0) {
					code += "\n";
				}
				code += lines[j];
			}
		}
	}

	// ── Fix 2: Standalone lambdas ──────────────────────────────────────
	// Wrap → parse → look for "Standalone lambdas" errors → fix lines → return.
	// The fix: insert  var _auto_cb_N =  before the  func  keyword on the
	// offending line, turning a standalone lambda expression into an assignment
	// statement which the parser accepts.
	//
	// We loop up to 5 times because one fix may unmask another on a later line
	// (unlikely but safe).
	// wrap_in_editor_script adds 4 header lines:
	//   line 1: @tool
	//   line 2: extends EditorScript
	//   line 3: (blank)
	//   line 4: func _run():
	//   line 5+: user code (each line indented by one tab)
	const int HEADER_LINES = AIScriptWrapper::HEADER_LINES;

	for (int attempt = 0; attempt < 5; attempt++) {
		String wrapped = AIScriptWrapper::wrap(code);

		GDScriptParser parser;
		parser.parse(wrapped, "", false);

		const List<GDScriptParser::ParserError> &errs = parser.get_errors();

		// Collect user-code line indices (0-based) that have standalone lambda errors.
		Vector<int> fix_lines;
		for (const List<GDScriptParser::ParserError>::Element *e = errs.front(); e; e = e->next()) {
			if (e->get().message.find("Standalone lambdas") >= 0) {
				int user_line_0 = e->get().start_line - HEADER_LINES - 1; // 0-based (field renamed start_line in 4.7)
				if (user_line_0 >= 0) {
					fix_lines.push_back(user_line_0);
				}
			}
		}

		if (fix_lines.is_empty()) {
			break; // nothing left to fix
		}

		Vector<String> lines = code.split("\n");
		int fix_id = attempt * 100;

		// Process from bottom to top so earlier fixes don't shift later indices.
		fix_lines.sort();
		for (int i = fix_lines.size() - 1; i >= 0; i--) {
			int idx = fix_lines[i];
			if (idx >= lines.size()) {
				continue;
			}
			const String &line = lines[idx];
			int func_pos = line.find("func");
			if (func_pos < 0) {
				continue;
			}
			String indent = line.substr(0, func_pos);
			String rest = line.substr(func_pos);
			lines.write[idx] = indent + "var _auto_cb_" + itos(fix_id++) + " = " + rest;
		}

		// Rejoin.
		code = String();
		for (int j = 0; j < lines.size(); j++) {
			if (j > 0) {
				code += "\n";
			}
			code += lines[j];
		}
	}

	// ── Fix 3: Deprecated EditorScript bare methods ────────────────────────
	// Replace `get_scene()` with `EditorInterface.get_edited_scene_root()` and
	// bare `add_root_node(` with `EditorInterface.add_root_node(` when not already
	// prefixed.  These deprecated methods cause hard runtime errors in Godot 4.7.
	{
		// get_scene() → EditorInterface.get_edited_scene_root()
		code = code.replace("get_scene()", "EditorInterface.get_edited_scene_root()");

		// Bare add_root_node( (not preceded by a dot) → EditorInterface.add_root_node(
		// We process line by line to avoid replacing inside string literals or comments.
		Vector<String> lines = code.split("\n");
		for (int i = 0; i < lines.size(); i++) {
			String &line = lines.write[i];
			// Skip comment lines.
			String stripped = line.strip_edges();
			if (stripped.begins_with("#")) {
				continue;
			}
			// Replace bare add_root_node( that is NOT preceded by a dot or word char.
			int pos = 0;
			while ((pos = line.find("add_root_node(", pos)) >= 0) {
				bool already_prefixed = (pos > 0 && (line[pos - 1] == '.' ||
						is_unicode_identifier_continue(line[pos - 1])));
				if (!already_prefixed) {
					line = line.substr(0, pos) + "EditorInterface.add_root_node(" +
							line.substr(pos + 14); // len("add_root_node(") == 14
					pos += 30; // skip past the replacement
				} else {
					pos++;
				}
			}
		}
		code = String();
		for (int i = 0; i < lines.size(); i++) {
			if (i > 0) {
				code += "\n";
			}
			code += lines[i];
		}
	}

	return code;
}
