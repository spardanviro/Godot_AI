#include "ai_script_executor.h"

#include "ai_code_fixes.h"
#include "ai_safety_policy.h"
#include "ai_script_wrapper.h"

#include "core/templates/hash_set.h"
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

String AIScriptExecutor::wrap_in_editor_script(const String &p_code, const String & /*p_action_name*/) const {
	return AIScriptWrapper::wrap(p_code);
}

String AIScriptExecutor::check_safety(const String &p_code) const {
	return AISafetyPolicy::check(p_code);
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
	return AICodeFixes::apply(p_code);
}

AIScriptExecutor::AIScriptExecutor() {
}
