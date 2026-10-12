#include "ai_safety_policy.h"

#include "core/object/class_db.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"
#include "modules/gdscript/gdscript_tokenizer.h"

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

// The assistant's own classes (AIAgentController, AIPermissionManager, the
// settings dialog...) hold the permission table and the provider. Named as
// an identifier or looked up by a string, they are off limits.
static bool _is_assistant_class(const String &p_name) {
	return p_name.begins_with("AI") && p_name.length() > 2 && ClassDB::class_exists(p_name);
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
		"call", "callv", "call_deferred", "get", "get_indexed", "set",
		// The main loop is the editor itself, not the edited scene.
		"get_main_loop", nullptr
	};
	static const char *denied_classes[] = {
		"Expression", "JavaClassWrapper", "JavaScriptBridge", "GDExtensionManager",
		"EditorSettings", nullptr
	};
	static const char *denied_methods[] = {
		// Network I/O from inside the editor.
		"request", "request_raw", "connect_to_host", "connect_to_url", "create_client",
		"create_server", "listen", "put_packet",
		// Editor settings hold the permission table; the editor's own data
		// directories hold the credential store.
		"get_editor_settings", "get_editor_paths",
		// Turning res:// or user:// into a real path is the first step of
		// walking out of the project (to the editor's config, for one).
		"get_user_data_dir", "get_config_dir", "get_data_dir", "get_cache_dir",
		"get_executable_path", "get_system_dir",
		// The editor's own interface tree, where the assistant's nodes live,
		// and the switches on them that decide what generated code may do.
		"get_main_loop", "get_base_control", "get_editor_main_screen",
		"get_window", "get_last_exclusive_window",
		"set_auto_approve", "configure_provider", "confirm_permission", nullptr
	};
	static const char *dynamic_call[] = { "call", "callv", "call_deferred", "callp", nullptr };
	static const char *dangerous_names[] = {
		"execute", "execute_with_pipe", "create_process", "create_instance", "shell_open",
		"shell_show_in_file_manager", "kill", "set_environment", "get_singleton",
		"request", "request_raw", "connect_to_host", "connect_to_url", "get_editor_settings",
		"get_editor_paths", "get_main_loop", "get_base_control", "get_editor_main_screen",
		"set_auto_approve", "configure_provider", "confirm_permission", nullptr
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

	// Names that hold the edited scene's root: `var root = get_scene()`.
	// Its parent is the editor, so get_parent() on it is refused below.
	static const char *scene_root_calls[] = { "get_scene", "get_edited_scene_root", "get_viewport", nullptr };
	HashSet<StringName> scene_root_names;
	for (int i = 0; i + 2 < tokens.size(); i++) {
		if (tokens[i].type != Token::IDENTIFIER) {
			continue;
		}
		// `name =`, `name :=` or `name: Type =`, then the call within the next few tokens.
		int equal = -1;
		for (int j = i + 1; j <= i + 3 && j < tokens.size(); j++) {
			if (tokens[j].type == Token::EQUAL) {
				equal = j;
				break;
			}
		}
		for (int j = equal + 1; equal > 0 && j <= equal + 3 && j < tokens.size(); j++) {
			if (tokens[j].type == Token::IDENTIFIER && _in_list(tokens[j].get_identifier(), scene_root_calls)) {
				scene_root_names.insert(tokens[i].get_identifier());
			}
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
			if (_is_assistant_class(t.literal)) {
				return "Blocked: Generated code may not look up or use the AI assistant's own classes (\"" + String(t.literal) + "\").";
			}
			continue;
		}
		if (t.type != Token::IDENTIFIER) {
			continue;
		}
		const StringName name = t.get_identifier();

		if (_is_assistant_class(name)) {
			return "Blocked: Generated code may not use the AI assistant's own classes ('" + String(name) + "').";
		}
		// The parent of the scene root (or of its viewport) is the editor.
		if (after_period && name == SNAME("get_parent") && i >= 2) {
			const bool on_call = i >= 4 && tokens[i - 2].type == Token::PARENTHESIS_CLOSE && tokens[i - 3].type == Token::PARENTHESIS_OPEN &&
					tokens[i - 4].type == Token::IDENTIFIER && _in_list(tokens[i - 4].get_identifier(), scene_root_calls);
			const bool on_name = tokens[i - 2].type == Token::IDENTIFIER && scene_root_names.has(tokens[i - 2].get_identifier());
			if (on_call || on_name) {
				return "Blocked: the parent of the edited scene's root is the editor itself. Generated code works inside the scene.";
			}
		}
		// get_tree().root is the editor's window, not the scene being edited.
		if (after_period && (name == SNAME("root") || name == SNAME("get_root")) && i >= 4 &&
				tokens[i - 2].type == Token::PARENTHESIS_CLOSE && tokens[i - 3].type == Token::PARENTHESIS_OPEN &&
				tokens[i - 4].type == Token::IDENTIFIER && tokens[i - 4].get_identifier() == SNAME("get_tree")) {
			return "Blocked: get_tree().root is the editor itself when code runs in the editor. Work on the edited scene (EditorInterface.get_edited_scene_root()) instead.";
		}

		if (after_period && name == SNAME("globalize_path")) {
			// The real path of a project folder is only needed to create that
			// folder, and that exact form is all that is accepted:
			//   make_dir[_recursive]_absolute(ProjectSettings.globalize_path("res://dir"))
			// Anywhere else the result could be walked upwards, out of the project.
			static const char *folder_calls[] = { "make_dir_recursive_absolute", "make_dir_absolute", "dir_exists_absolute", nullptr };
			const bool wrapped = i >= 4 && i + 4 < tokens.size() &&
					tokens[i - 2].type == Token::IDENTIFIER && tokens[i - 2].get_identifier() == SNAME("ProjectSettings") &&
					tokens[i - 3].type == Token::PARENTHESIS_OPEN &&
					tokens[i - 4].type == Token::IDENTIFIER && _in_list(tokens[i - 4].get_identifier(), folder_calls) &&
					tokens[i + 1].type == Token::PARENTHESIS_OPEN &&
					tokens[i + 2].type == Token::LITERAL && tokens[i + 2].literal.get_type() == Variant::STRING &&
					tokens[i + 3].type == Token::PARENTHESIS_CLOSE && tokens[i + 4].type == Token::PARENTHESIS_CLOSE;
			if (!wrapped) {
				return "Blocked: globalize_path() may only be used as DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(\"res://folder\")). Use res:// and user:// paths directly everywhere else; FileAccess and DirAccess accept them.";
			}
			continue;
		}
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

String AISafetyPolicy::check(const String &p_code) {
	// Match on a whitespace-free copy so `OS . execute (` is caught too.
	const String compact = p_code.replace(" ", "").replace("\t", "");

	// Always blocked: system-level operations. Deleting files is not here; the
	// permission system decides that (PERM_FILE_DELETE).
	static const char *blocked_calls[] = { "OS.execute", "OS.shell_open", "OS.kill", "OS.create_process", ".shell_execute", nullptr };
	for (int i = 0; blocked_calls[i]; i++) {
		if (compact.find(blocked_calls[i]) != -1) {
			return "Blocked: Code contains dangerous API call '" + String(blocked_calls[i]) + "'. This operation is not allowed for safety reasons.";
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
