#include "ai_script_executor.h"
#include "core/object/class_db.h"

#include "core/config/engine.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/object/script_language.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/script/editor_script.h"
#endif

// Error capture for script execution.
static thread_local String _captured_errors;

static void _ai_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	if (p_type == ERR_HANDLER_ERROR || p_type == ERR_HANDLER_SCRIPT || p_type == ERR_HANDLER_WARNING) {
		if (!_captured_errors.is_empty()) {
			_captured_errors += "\n";
		}

		// Type prefix.
		if (p_type == ERR_HANDLER_WARNING) {
			_captured_errors += "[WARNING] ";
		} else {
			_captured_errors += "[ERROR] ";
		}

		// Error message and detail.
		_captured_errors += String(p_error);
		if (p_message && p_message[0]) {
			_captured_errors += ": " + String(p_message);
		}

		// Location info.
		String file_str = p_file ? String(p_file) : "";
		String func_str = p_function ? String(p_function) : "";
		if (!file_str.is_empty()) {
			_captured_errors += " (at " + file_str + ":" + itos(p_line);
			if (!func_str.is_empty()) {
				_captured_errors += " in " + func_str;
			}
			_captured_errors += ")";
		}

		// Try to capture GDScript backtrace.
		Vector<Ref<ScriptBacktrace>> backtraces = ScriptServer::capture_script_backtraces(false);
		for (int bt = 0; bt < backtraces.size(); bt++) {
			const Ref<ScriptBacktrace> &trace = backtraces[bt];
			if (trace.is_null() || trace->is_empty()) {
				continue;
			}
			String stack_line;
			for (int f = 0; f < trace->get_frame_count(); f++) {
				if (!stack_line.is_empty()) {
					stack_line += String::utf8(" → ");
				}
				stack_line += trace->get_frame_file(f) + ":" + itos(trace->get_frame_line(f)) + " @ " + trace->get_frame_function(f) + "()";
			}
			if (!stack_line.is_empty()) {
				_captured_errors += "\n  Stack: " + stack_line;
			}
		}
	}
}

void AIScriptExecutor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("wrap_in_editor_script", "code", "action_name"), &AIScriptExecutor::wrap_in_editor_script, DEFVAL("AI Action"));
	ClassDB::bind_method(D_METHOD("check_safety", "code"), &AIScriptExecutor::check_safety);
	ClassDB::bind_method(D_METHOD("execute", "code", "action_name"), &AIScriptExecutor::execute, DEFVAL("AI Action"));
	ClassDB::bind_method(D_METHOD("compile_check", "code"), &AIScriptExecutor::compile_check);
}

void AIScriptExecutor::_init_blocklist() {
	// Always blocked — dangerous system-level operations.
	blocklist.push_back("OS.execute");
	blocklist.push_back("OS.shell_open");
	blocklist.push_back("OS.kill");
	blocklist.push_back("OS.create_process");
	blocklist.push_back(".shell_execute");
	// Note: DirAccess.remove / remove_absolute are now handled by the
	// permission system (PERM_FILE_DELETE) instead of being hard-blocked.
}

String AIScriptExecutor::wrap_in_editor_script(const String &p_code, const String &p_action_name) const {
	String wrapped;
	wrapped += "@tool\n";
	wrapped += "extends EditorScript\n\n";
	wrapped += "func _run():\n";

	// Indent each line of the user code.
	Vector<String> lines = p_code.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].strip_edges().is_empty()) {
			wrapped += "\n";
		} else {
			wrapped += "\t" + lines[i] + "\n";
		}
	}

	return wrapped;
}

String AIScriptExecutor::check_safety(const String &p_code) const {
	for (int i = 0; i < blocklist.size(); i++) {
		if (p_code.find(blocklist[i]) != -1) {
			return "Blocked: Code contains dangerous API call '" + blocklist[i] + "'. This operation is not allowed for safety reasons.";
		}
	}
	return "";
}

Dictionary AIScriptExecutor::execute(const String &p_code, const String &p_action_name) {
	Dictionary result;
	result["success"] = false;
	result["output"] = "";
	result["error"] = "";

#ifdef TOOLS_ENABLED
	// Safety check.
	String safety_error = check_safety(p_code);
	if (!safety_error.is_empty()) {
		result["error"] = safety_error;
		return result;
	}

	// Wrap code.
	String wrapped = wrap_in_editor_script(p_code, p_action_name);

	// Create and compile GDScript.
	// Install error handler BEFORE reload() so parse errors are captured
	// into _captured_errors instead of being printed to the Output console.
	// Temporarily silence Engine error printing so the intentional probe
	// compile does not pollute the editor Output panel with red errors.
	_captured_errors = "";
	ErrorHandlerList eh;
	eh.errfunc = _ai_error_handler;
	eh.userdata = nullptr;
	add_error_handler(&eh);

	const bool old_print_errors = Engine::get_singleton()->is_printing_error_messages();
	Engine::get_singleton()->set_print_error_messages(false);

	Ref<GDScript> gd_script;
	gd_script.instantiate();
	gd_script->set_source_code(wrapped);

	Error err = gd_script->reload(false);

	Engine::get_singleton()->set_print_error_messages(old_print_errors);

	if (err != OK) {
		remove_error_handler(&eh);
		String parse_errors = _captured_errors.strip_edges();
		if (parse_errors.is_empty()) {
			parse_errors = "Unknown parse error — check GDScript 4 syntax.";
		}
		result["error"] = "GDScript parse error:\n" + parse_errors;
		return result;
	}

	// Create an EditorScript instance using Ref (proper RefCounted memory management).
	// This matches Godot's own pattern in editor_script_plugin.cpp.
	Ref<EditorScript> editor_script;
	editor_script.instantiate();
	editor_script->set_script(gd_script);
	editor_script->run();

	remove_error_handler(&eh);

	if (!_captured_errors.is_empty()) {
		result["success"] = false;
		result["error"] = "Runtime error: " + _captured_errors;
	} else {
		result["success"] = true;
		result["output"] = "Script executed successfully.";
	}
#else
	result["error"] = "Script execution is only available in the editor.";
#endif

	return result;
}

String AIScriptExecutor::compile_check(const String &p_code) const {
	// Use GDScriptParser + GDScriptAnalyzer DIRECTLY instead of GDScript::reload().
	//
	// GDScript::reload() calls _err_print_error() for every parse failure, which
	// notifies ALL registered error handlers — including the editor's Output panel
	// handler.  There is no way to suppress that notification without temporarily
	// removing every other handler from the list, which is not safe.
	//
	// By calling the parser and analyzer ourselves we read errors straight from
	// their in-memory lists; _err_print_error is never invoked, so the Output
	// panel stays silent during what is intentionally a probe/dry-run compile.
	String wrapped = wrap_in_editor_script(p_code);

	GDScriptParser parser;
	Error err = parser.parse(wrapped, "", false);

	// Collect parse-phase errors first.
	String errors;
	const List<GDScriptParser::ParserError> &parse_errs = parser.get_errors();
	for (const List<GDScriptParser::ParserError>::Element *e = parse_errs.front(); e; e = e->next()) {
		if (!errors.is_empty()) {
			errors += "\n";
		}
		errors += "Parse Error: " + e->get().message;
	}

	if (!errors.is_empty()) {
		return errors;
	}

	// If the parse phase passed, run the analyzer (catches type errors, inference
	// errors, standalone-lambda errors, etc.).
	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	if (err != OK) {
		// The analyzer writes its errors back into the parser's error list.
		const List<GDScriptParser::ParserError> &analyze_errs = parser.get_errors();
		for (const List<GDScriptParser::ParserError>::Element *e = analyze_errs.front(); e; e = e->next()) {
			if (!errors.is_empty()) {
				errors += "\n";
			}
			errors += "Parse Error: " + e->get().message;
		}
		if (errors.is_empty()) {
			errors = "Unknown analysis error — check GDScript 4 syntax.";
		}
	}

	return errors;
}

String AIScriptExecutor::auto_fix_code(const String &p_code) const {
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
				for (int j = i + 1; j < lines.size(); j++) {
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
	const int HEADER_LINES = 4;

	for (int attempt = 0; attempt < 5; attempt++) {
		String wrapped = wrap_in_editor_script(code);

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

AIScriptExecutor::AIScriptExecutor() {
	_init_blocklist();
}
