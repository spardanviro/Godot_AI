#include "ai_script_executor.h"
#include "core/object/class_db.h"

#include "core/config/engine.h"
#include "core/error/error_list.h"
#include "core/error/error_macros.h"
#include "core/object/script_language.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "modules/gdscript/gdscript_tokenizer.h"
#include "core/string/char_utils.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/script/editor_script.h"
#endif

// Error capture for script execution.
static thread_local String _captured_errors;
// Warnings are kept apart: a script that ran to completion with a warning has
// already changed the scene, so calling it a failure makes the caller re-run it.
static thread_local String _captured_warnings;

static String _normalize_indentation(const String &p_code);

static void _ai_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	if (p_type == ERR_HANDLER_WARNING) {
		if (!_captured_warnings.is_empty()) {
			_captured_warnings += "\n";
		}
		_captured_warnings += "[WARNING] " + String(p_error);
		if (p_message && p_message[0]) {
			_captured_warnings += ": " + String(p_message);
		}
		return;
	}
	if (p_type == ERR_HANDLER_ERROR || p_type == ERR_HANDLER_SCRIPT) {
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

// ── Token-level policy ──────────────────────────────────────────────────────
// The substring checks in check_safety() can be dodged by aliasing or spacing.
// This pass works on the real token stream (so comments and string contents are
// not confused with code) and enforces rules on how sensitive globals are used:
//
//   - OS / Engine / ClassDB may only appear as `Name.method(...)` with a method
//     from a known-harmless set. Used any other way — assigned, passed, indexed
//     — they could be aliased, so that is rejected. With no way to hold the
//     object or one of its Callables, the dangerous methods are unreachable.
//   - Classes that evaluate arbitrary code or reach outside the engine, and
//     access to EditorSettings (API keys), are rejected outright.
//   - Network calls are rejected: a generated script may create networking
//     nodes for the game, but not talk to the network from inside the editor.
//   - String literals may not name locations outside the project.
//
// What this cannot do: prove anything about values computed at run time. A
// path or method name assembled from pieces ("C" + ":/") is invisible to any
// static check. It raises the bar for ordinary and for casually hostile
// output; it is not a sandbox.

static bool _in_list(const StringName &p_name, const char *const *p_list) {
	for (int i = 0; p_list[i]; i++) {
		if (p_name == p_list[i]) {
			return true;
		}
	}
	return false;
}

static String _check_string_literal(const String &p_value) {
	const String v = p_value.strip_edges();
	if (v.length() >= 3 && is_ascii_alphabet_char(v[0]) && v[1] == ':' && (v[2] == '/' || v[2] == '\\')) {
		return "an absolute drive path (\"" + v.left(40) + "\")";
	}
	if (v.begins_with("\\\\") || v.to_lower().begins_with("file://")) {
		return "a path outside the project (\"" + v.left(40) + "\")";
	}
	// "/root/..." is a node path; any other rooted path is a filesystem path.
	if (v.length() > 1 && v[0] == '/' && is_ascii_alphabet_char(v[1]) && !v.begins_with("/root")) {
		return "an absolute filesystem path (\"" + v.left(40) + "\")";
	}
	if ((v.begins_with("res://") || v.begins_with("user://")) && (v.find("../") >= 0 || v.find("..\\") >= 0)) {
		return "a path that climbs out of the project (\"" + v.left(40) + "\")";
	}
	return String();
}

static String _check_token_policy(const String &p_code) {
	typedef GDScriptTokenizer::Token Token;

	static const char *os_allowed[] = {
		"get_name", "get_ticks_msec", "get_ticks_usec", "delay_msec", "delay_usec",
		"has_feature", "get_locale", "get_locale_language", "get_processor_count",
		"get_processor_name", "get_static_memory_usage", "get_model_name", "get_version",
		"get_distribution_name", "is_debug_build", "get_unix_time", "get_cmdline_args",
		"is_stdout_verbose", "get_video_adapter_driver_info", "get_thread_caller_id", nullptr
	};
	static const char *classdb_allowed[] = {
		"class_exists", "get_class_list", "get_parent_class", "is_parent_class",
		"get_inheriters_from_class", "class_has_method", "class_get_method_list",
		"class_get_property_list", "class_has_signal", "class_get_signal_list",
		"can_instantiate", "class_get_integer_constant_list", "class_has_integer_constant", nullptr
	};
	static const char *engine_denied[] = {
		"get_singleton", "register_singleton", "unregister_singleton", "get_singleton_list",
		"call", "callv", "call_deferred", "get", "get_indexed", "set", nullptr
	};
	static const char *denied_classes[] = {
		"Expression", "JavaClassWrapper", "JavaScriptBridge", "GDExtensionManager",
		"EditorSettings", nullptr
	};
	static const char *denied_methods[] = {
		// Network I/O from inside the editor.
		"request", "request_raw", "connect_to_host", "connect_to_url", "create_client",
		"create_server", "listen", "put_packet",
		// Editor settings hold the API keys.
		"get_editor_settings", nullptr
	};
	static const char *dynamic_call[] = { "call", "callv", "call_deferred", "callp", nullptr };
	static const char *dangerous_names[] = {
		"execute", "execute_with_pipe", "create_process", "create_instance", "shell_open",
		"shell_show_in_file_manager", "kill", "set_environment", "get_singleton",
		"request", "request_raw", "connect_to_host", "connect_to_url", "get_editor_settings", nullptr
	};

	Vector<Token> tokens;
	{
		GDScriptTokenizerText tokenizer;
		tokenizer.set_source_code(p_code);
		for (int guard = 0; guard < 200000; guard++) {
			Token t = tokenizer.scan();
			if (t.type == Token::TK_EOF) {
				break;
			}
			// Layout tokens carry no meaning here; errors are the compiler's job.
			if (t.type == Token::NEWLINE || t.type == Token::INDENT || t.type == Token::DEDENT || t.type == Token::ERROR) {
				continue;
			}
			tokens.push_back(t);
		}
	}

	for (int i = 0; i < tokens.size(); i++) {
		const Token &t = tokens[i];
		const bool after_period = i > 0 && tokens[i - 1].type == Token::PERIOD;
		const bool has_next = i + 1 < tokens.size();

		if (t.type == Token::LITERAL && t.literal.get_type() == Variant::STRING) {
			const String problem = _check_string_literal(t.literal);
			if (!problem.is_empty()) {
				return "Blocked: Code contains " + problem + ". Generated code may only work inside the project (res://, user://).";
			}
			continue;
		}
		if (t.type != Token::IDENTIFIER) {
			continue;
		}
		const StringName name = t.get_identifier();

		if (after_period) {
			// `.method` on any receiver.
			if (_in_list(name, denied_methods)) {
				return "Blocked: Code calls '" + String(name) + "()', which is not allowed in editor-executed code (network access or editor settings).";
			}
			if (_in_list(name, dynamic_call) && i + 2 < tokens.size() && tokens[i + 1].type == Token::PARENTHESIS_OPEN) {
				const Token &arg = tokens[i + 2];
				const bool is_text = arg.type == Token::LITERAL &&
						(arg.literal.get_type() == Variant::STRING || arg.literal.get_type() == Variant::STRING_NAME);
				if (is_text && _in_list(StringName(String(arg.literal)), dangerous_names)) {
					return "Blocked: Code calls '" + String(arg.literal) + "' through " + String(name) + "(), which is not allowed.";
				}
			}
			continue;
		}

		if (_in_list(name, denied_classes)) {
			return "Blocked: Code uses '" + String(name) + "', which is not allowed in editor-executed code.";
		}

		const bool is_os = name == SNAME("OS");
		const bool is_classdb = name == SNAME("ClassDB");
		const bool is_engine = name == SNAME("Engine");
		if (is_os || is_classdb || is_engine) {
			// Must be `Name.method`. Anything else could alias the singleton.
			if (!has_next || tokens[i + 1].type != Token::PERIOD || i + 2 >= tokens.size() || tokens[i + 2].type != Token::IDENTIFIER) {
				return "Blocked: '" + String(name) + "' may only be used as " + String(name) + ".method(...); it cannot be stored, passed or indexed.";
			}
			const StringName method = tokens[i + 2].get_identifier();
			if ((is_os && !_in_list(method, os_allowed)) ||
					(is_classdb && !_in_list(method, classdb_allowed)) ||
					(is_engine && _in_list(method, engine_denied))) {
				return "Blocked: '" + String(name) + "." + String(method) + "' is not allowed in editor-executed code.";
			}
			i += 2; // The method name was just checked against this receiver's own list.
			continue;
		}

		if (name == SNAME("Callable") && has_next && tokens[i + 1].type == Token::PARENTHESIS_OPEN) {
			// Callable(object, method): the method must be a harmless literal.
			int depth = 0;
			for (int j = i + 1; j < tokens.size(); j++) {
				if (tokens[j].type == Token::PARENTHESIS_OPEN) {
					depth++;
				} else if (tokens[j].type == Token::PARENTHESIS_CLOSE) {
					if (--depth == 0) {
						break; // Callable() or Callable(x): nothing to check.
					}
				} else if (tokens[j].type == Token::COMMA && depth == 1) {
					const bool literal_name = j + 2 < tokens.size() && tokens[j + 1].type == Token::LITERAL &&
							(tokens[j + 1].literal.get_type() == Variant::STRING || tokens[j + 1].literal.get_type() == Variant::STRING_NAME) &&
							tokens[j + 2].type == Token::PARENTHESIS_CLOSE;
					if (!literal_name) {
						return "Blocked: Callable(object, method) needs a literal method name here; use the method reference (object.method) instead.";
					}
					if (_in_list(StringName(String(tokens[j + 1].literal)), dangerous_names)) {
						return "Blocked: Callable to '" + String(tokens[j + 1].literal) + "' is not allowed.";
					}
					break;
				}
			}
		}
	}
	return String();
}

String AIScriptExecutor::check_safety(const String &p_code) const {
	// Match on a whitespace-free copy so `OS . execute (` is caught too.
	const String compact = p_code.replace(" ", "").replace("\t", "");

	for (int i = 0; i < blocklist.size(); i++) {
		if (compact.find(blocklist[i]) != -1) {
			return "Blocked: Code contains dangerous API call '" + blocklist[i] + "'. This operation is not allowed for safety reasons.";
		}
	}

	// Process-spawning methods by name, so an alias (`var o = OS; o.execute()`)
	// does not get past the class-qualified patterns above.
	static const char *blocked_methods[] = {
		".execute(", ".execute_with_pipe(", ".create_process(", ".create_instance(",
		".shell_open(", ".shell_show_in_file_manager(", ".set_environment(", nullptr
	};
	// Reflection and dynamic dispatch that reach the same APIs indirectly.
	static const char *blocked_indirect[] = {
		"Engine.get_singleton(", "ClassDB.instantiate(", "ClassDB.class_call_static(",
		"JavaClassWrapper", "JavaScriptBridge",
		"\"execute\"", "'execute'", "\"create_process\"", "'create_process'",
		"\"shell_open\"", "'shell_open'", "\"execute_with_pipe\"", "'execute_with_pipe'",
		nullptr
	};
	for (int i = 0; blocked_methods[i]; i++) {
		if (compact.find(blocked_methods[i]) != -1) {
			return "Blocked: Code calls '" + String(blocked_methods[i]).trim_prefix(".").trim_suffix("(") + "()', which can start external processes. This operation is not allowed for safety reasons.";
		}
	}
	for (int i = 0; blocked_indirect[i]; i++) {
		if (compact.find(blocked_indirect[i]) != -1) {
			return "Blocked: Code uses '" + String(blocked_indirect[i]) + "', which can reach blocked system APIs indirectly.";
		}
	}

	// The assistant's own settings hold the API keys and the permission table.
	// Generated code has no reason to touch them, and reading or rewriting them
	// is how injected instructions would steal a key or lift the restrictions.
	if (compact.find("ai_assistant/") != -1) {
		return "Blocked: Generated code may not read or change the AI assistant's own settings (API keys, endpoint, permissions).";
	}

	return _check_token_policy(p_code);
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
	_captured_warnings = "";
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
		result["output"] = _captured_warnings.is_empty()
				? String("Script executed successfully.")
				: "Script executed successfully (with warnings):\n" + _captured_warnings;
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
