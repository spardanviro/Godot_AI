#include "ai_script_executor.h"

#include "core/config/engine.h"
#include "core/error/error_macros.h"
#include "core/object/script_language.h"
#include "modules/gdscript/gdscript.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/script/editor_script.h"
#endif

// Error capture for script execution.
static thread_local String _captured_errors;

static void _ai_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	if (p_type == ERR_HANDLER_ERROR || p_type == ERR_HANDLER_SCRIPT) {
		if (!_captured_errors.is_empty()) {
			_captured_errors += "\n";
		}
		_captured_errors += String(p_error);
		if (p_message && p_message[0]) {
			_captured_errors += ": " + String(p_message);
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
	Ref<GDScript> gd_script;
	gd_script.instantiate();
	gd_script->set_source_code(wrapped);

	Error err = gd_script->reload(false);
	if (err != OK) {
		result["error"] = "Compilation failed. Check GDScript syntax.";
		return result;
	}

	// Install error handler to capture runtime errors.
	_captured_errors = "";
	ErrorHandlerList eh;
	eh.errfunc = _ai_error_handler;
	eh.userdata = nullptr;
	add_error_handler(&eh);

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
	String wrapped = wrap_in_editor_script(p_code);

	Ref<GDScript> gd_script;
	gd_script.instantiate();
	gd_script->set_source_code(wrapped);

	Error err = gd_script->reload(false);
	if (err != OK) {
		return "Compilation error in generated code.";
	}

	return "";
}

AIScriptExecutor::AIScriptExecutor() {
	_init_blocklist();
}
