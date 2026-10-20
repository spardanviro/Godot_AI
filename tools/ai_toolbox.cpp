#include "ai_toolbox.h"

#include "../execution/ai_permission_manager.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/variant/variant_parser.h"
#include "scene/main/node.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#endif

namespace {

const int DEFAULT_TREE_DEPTH = 8;
const int MAX_TREE_DEPTH = 32;
const int DEFAULT_FILE_LINES = 200;
const int ERROR_COUNT = 20;

// --- Results ---

String fail(bool &r_ok, const String &p_message) {
	r_ok = false;
	return "Error: " + p_message;
}

String clip(const String &p_text, int p_limit) {
	return p_text.length() > p_limit ? p_text.left(p_limit) + "... (" + itos(p_text.length() - p_limit) + " more characters)" : p_text;
}

String value_text(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value;
		if (!object) {
			return "null";
		}
		Resource *resource = Object::cast_to<Resource>(object);
		if (resource && !resource->get_path().is_empty()) {
			return object->get_class() + "(" + resource->get_path() + ")";
		}
		Node *node = Object::cast_to<Node>(object);
		return node ? object->get_class() + "(" + String(node->get_name()) + ")" : "<" + object->get_class() + ">";
	}
	String text;
	VariantWriter::write_to_string(p_value, text);
	return clip(text, AIToolbox::MAX_VALUE_CHARS);
}

String script_path(const Node *p_node) {
	const Ref<Script> script = p_node->get_script();
	return script.is_valid() ? script->get_path() : String();
}

int int_argument(const Dictionary &p_arguments, const char *p_name, int p_default, int p_min, int p_max) {
	const Variant value = p_arguments.get(p_name, p_default);
	return value.is_num() ? CLAMP(int(value), p_min, p_max) : p_default;
}

void write_tree(const Node *p_node, int p_depth, int p_max_depth, int &r_count, String &r_out) {
	if (r_count >= AIToolbox::MAX_TREE_NODES) {
		return;
	}
	r_count++;
	String line = String("  ").repeat(p_depth) + String(p_node->get_name()) + " (" + p_node->get_class() + ")";
	const String script = script_path(p_node);
	if (!script.is_empty()) {
		line += " script=" + script;
	}
	if (!p_node->get_scene_file_path().is_empty() && p_depth > 0) {
		line += " instance_of=" + p_node->get_scene_file_path();
	}
	const int children = p_node->get_child_count();
	if (p_depth >= p_max_depth && children > 0) {
		line += " [" + itos(children) + " children not shown]";
	}
	r_out += line + "\n";
	if (p_depth >= p_max_depth) {
		return;
	}
	for (int i = 0; i < children; i++) {
		write_tree(p_node->get_child(i), p_depth + 1, p_max_depth, r_count, r_out);
	}
}

} // namespace

// =============================================================================
// The tools
// =============================================================================

bool AIToolbox::has_tool(const String &p_name) const {
	for (const AIToolSpec &tool : get_specs()) {
		if (tool.name == p_name) {
			return true;
		}
	}
	return false;
}

AIToolSpec::Kind AIToolbox::get_kind(const String &p_name) const {
	for (const AIToolSpec &tool : get_specs()) {
		if (tool.name == p_name) {
			return tool.kind;
		}
	}
	return AIToolSpec::KIND_READ;
}

int AIToolbox::get_permission_category(const AIToolCall &p_call) const {
	const String &name = p_call.name;
	if (name == "create_node" || name == "instantiate_scene") {
		// A node that comes with a script is a script being attached.
		Dictionary arguments;
		parse_arguments(p_call.arguments, arguments);
		const bool with_script = !String(arguments.get("script", "")).strip_edges().is_empty();
		return with_script ? AIPermissionManager::PERM_MODIFY_SCRIPTS : AIPermissionManager::PERM_CREATE_NODES;
	}
	if (name == "set_node_properties" || name == "rename_node" || name == "move_node" || name == "connect_signal") {
		return AIPermissionManager::PERM_MODIFY_PROPERTIES;
	}
	if (name == "delete_node") {
		return AIPermissionManager::PERM_DELETE_NODES;
	}
	if (name == "attach_script") {
		return AIPermissionManager::PERM_MODIFY_SCRIPTS;
	}
	// A generated asset is a file written into the project.
	if (name == "save_scene" || name == "generate_image" || name == "generate_speech") {
		return AIPermissionManager::PERM_FILE_WRITE;
	}
	if (name == "remember") {
		return AIPermissionManager::PERM_REMEMBER;
	}
	if (name == "set_project_setting" || name == "add_input_action") {
		return AIPermissionManager::PERM_CHANGE_PROJECT_SETTINGS;
	}
	if (name == "write_file") {
		Dictionary arguments;
		parse_arguments(p_call.arguments, arguments);
		const String extension = String(arguments.get("path", "")).get_extension().to_lower();
		return extension == "gd" || extension == "cs" || extension == "gdshader" ? AIPermissionManager::PERM_MODIFY_SCRIPTS : AIPermissionManager::PERM_FILE_WRITE;
	}
	return -1;
}

bool AIToolbox::parse_arguments(const String &p_arguments, Dictionary &r_arguments) {
	r_arguments = Dictionary();
	if (p_arguments.strip_edges().is_empty()) {
		return true;
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_arguments) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_arguments = json->get_data();
	return true;
}

String AIToolbox::_value_text(const Variant &p_value) {
	return value_text(p_value);
}

String AIToolbox::call(const String &p_name, const String &p_arguments, bool &r_ok) const {
	r_ok = true;
	Dictionary arguments;
	if (!parse_arguments(p_arguments, arguments)) {
		return fail(r_ok, "the arguments are not a JSON object: " + clip(p_arguments, 200));
	}

	String result;
	if (p_name == "get_scene_tree") {
		result = _get_scene_tree(arguments, r_ok);
	} else if (p_name == "get_node_properties") {
		result = _get_node_properties(arguments, r_ok);
	} else if (p_name == "read_file") {
		result = _read_file(arguments, r_ok);
	} else if (p_name == "list_files") {
		result = _list_files(arguments, r_ok);
	} else if (p_name == "get_errors") {
		result = _get_errors(arguments, r_ok);
	} else if (p_name == "create_node") {
		result = _create_node(arguments, r_ok);
	} else if (p_name == "instantiate_scene") {
		result = _instantiate_scene(arguments, r_ok);
	} else if (p_name == "set_node_properties") {
		result = _set_node_properties(arguments, r_ok);
	} else if (p_name == "rename_node") {
		result = _rename_node(arguments, r_ok);
	} else if (p_name == "move_node") {
		result = _move_node(arguments, r_ok);
	} else if (p_name == "delete_node") {
		result = _delete_node(arguments, r_ok);
	} else if (p_name == "connect_signal") {
		result = _connect_signal(arguments, r_ok);
	} else if (p_name == "attach_script") {
		result = _attach_script(arguments, r_ok);
	} else if (p_name == "write_file") {
		result = _write_file(arguments, r_ok);
	} else if (p_name == "save_scene") {
		result = _save_scene(arguments, r_ok);
	} else if (p_name == "set_project_setting") {
		result = _set_project_setting(arguments, r_ok);
	} else if (p_name == "add_input_action") {
		result = _add_input_action(arguments, r_ok);
	} else if (has_tool(p_name)) {
		return fail(r_ok, "'" + p_name + "' is carried out by the agent, not by the toolbox.");
	} else {
		return fail(r_ok, "there is no tool named '" + p_name + "'.");
	}
	return clip(result, MAX_RESULT_CHARS);
}

String AIToolbox::describe_call(const AIToolCall &p_call) {
	static const int MAX_ARGUMENT_CHARS = 60;
	String arguments;
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_call.arguments) == OK && json->get_data().get_type() == Variant::DICTIONARY) {
		const Dictionary values = json->get_data();
		for (const Variant *key = values.next(nullptr); key; key = values.next(key)) {
			const Variant value = values[*key];
			String text = value.get_type() == Variant::STRING ? String(value) : JSON::stringify(value, "", false);
			// A whole file or script is not something to read in a one-line summary.
			const int lines = text.count("\n") + 1;
			if (lines > 1) {
				text = "<" + itos(lines) + " lines>";
			} else if (text.length() > MAX_ARGUMENT_CHARS) {
				text = text.left(MAX_ARGUMENT_CHARS) + "...";
			}
			arguments += (arguments.is_empty() ? "" : ", ") + String(*key) + ": " + text;
		}
	}
	return p_call.name + "(" + arguments + ")";
}

Node *AIToolbox::_get_scene_root() const {
	if (test_scene_root) {
		return test_scene_root;
	}
#ifdef TOOLS_ENABLED
	// Without the editor proper (unit tests) there is no scene to ask for.
	if (EditorNode::get_singleton() && EditorInterface::get_singleton()) {
		return EditorInterface::get_singleton()->get_edited_scene_root();
	}
#endif
	return nullptr;
}

String AIToolbox::_get_scene_tree(const Dictionary &p_arguments, bool &r_ok) const {
	const Node *root = _get_scene_root();
	if (!root) {
		return fail(r_ok, "no scene is open in the editor.");
	}
	String out;
	if (!root->get_scene_file_path().is_empty()) {
		out += "Scene file: " + root->get_scene_file_path() + "\n";
	}
	int count = 0;
	write_tree(root, 0, int_argument(p_arguments, "max_depth", DEFAULT_TREE_DEPTH, 0, MAX_TREE_DEPTH), count, out);
	if (count >= MAX_TREE_NODES) {
		out += "(stopped after " + itos(MAX_TREE_NODES) + " nodes; ask for a smaller max_depth or read a node's children with get_node_properties)\n";
	}
	return out;
}

String AIToolbox::_get_node_properties(const Dictionary &p_arguments, bool &r_ok) const {
	if (String(p_arguments.get("path", "")).strip_edges().is_empty()) {
		return fail(r_ok, "'path' is required.");
	}
	String error;
	Node *node = _find_node(p_arguments.get("path", ""), error);
	if (!node) {
		return fail(r_ok, error);
	}
	Node *root = _get_scene_root();

	String out = "Node: " + (node == root ? String(".") : String(root->get_path_to(node))) + "\nClass: " + node->get_class() + "\n";
	const String script = script_path(node);
	if (!script.is_empty()) {
		out += "Script: " + script + "\n";
	}
	List<Node::GroupInfo> groups;
	node->get_groups(&groups);
	PackedStringArray group_names;
	for (const Node::GroupInfo &group : groups) {
		if (group.persistent) {
			group_names.push_back(group.name);
		}
	}
	if (!group_names.is_empty()) {
		out += "Groups: " + String(", ").join(group_names) + "\n";
	}
	PackedStringArray child_names;
	for (int i = 0; i < node->get_child_count(); i++) {
		child_names.push_back(node->get_child(i)->get_name());
	}
	out += "Children: " + (child_names.is_empty() ? String("(none)") : String(", ").join(child_names)) + "\n";

	// Connections saved with the scene, as the Node dock shows them.
	List<MethodInfo> signals;
	node->get_signal_list(&signals);
	for (const MethodInfo &signal : signals) {
		List<Object::Connection> connections;
		node->get_signal_connection_list(signal.name, &connections);
		for (const Object::Connection &connection : connections) {
			const Node *target = Object::cast_to<Node>(connection.callable.get_object());
			if ((connection.flags & Object::CONNECT_PERSIST) && target && (target == root || root->is_ancestor_of(target))) {
				out += "Connection: " + signal.name + " -> " + (target == root ? String(".") : String(root->get_path_to(target))) + "::" + String(connection.callable.get_method()) + "()\n";
			}
		}
	}

	const Variant requested = p_arguments.get("properties", Variant());
	if (requested.get_type() == Variant::ARRAY && !Array(requested).is_empty()) {
		out += "Properties:\n";
		const Array names = requested;
		for (int i = 0; i < names.size() && i < MAX_PROPERTIES; i++) {
			const String name = names[i];
			bool valid = false;
			const Variant value = node->get(name, &valid);
			out += "  " + name + " = " + (valid ? value_text(value) : String("(no such property)")) + "\n";
		}
		return out;
	}

	out += "Properties that differ from the class default:\n";
	List<PropertyInfo> properties;
	node->get_property_list(&properties);
	int listed = 0;
	for (const PropertyInfo &info : properties) {
		if (!(info.usage & PROPERTY_USAGE_STORAGE) || !(info.usage & PROPERTY_USAGE_EDITOR) || info.name == "script") {
			continue;
		}
		const Variant value = node->get(info.name);
		bool has_default = false;
		const Variant default_value = ClassDB::class_get_default_property_value(node->get_class_name(), info.name, &has_default);
		if (has_default && default_value == value) {
			continue;
		}
		if (listed >= MAX_PROPERTIES) {
			out += "  (more not shown; name the properties you need)\n";
			break;
		}
		out += "  " + info.name + " = " + value_text(value) + "\n";
		listed++;
	}
	if (listed == 0) {
		out += "  (none)\n";
	}
	return out;
}

bool AIToolbox::is_readable_project_file(const String &p_path, String &r_reason) {
	static const char *text_extensions[] = {
		"gd", "tscn", "tres", "gdshader", "gdshaderinc", "md", "txt", "json", "cfg", "csv", "yaml", "yml", "toml", "xml",
		"godot", "import", "cs", "translation", "po", "ini", nullptr
	};
	// Where signing keys and credentials are usually found.
	static const char *withheld_names[] = { "export_presets.cfg", "export_credentials.cfg", nullptr };

	if (!p_path.begins_with("res://")) {
		r_reason = "only files inside the project (res://) can be read.";
		return false;
	}
	if (p_path.contains("..") || p_path.contains("\\")) {
		r_reason = "the path may not contain '..' or backslashes.";
		return false;
	}
	const String file = p_path.get_file();
	for (const String &segment : p_path.trim_prefix("res://").split("/")) {
		// .godot is the engine's cache; other dotfiles (.env) are not project content.
		if (segment.begins_with(".")) {
			r_reason = "hidden files and folders are not read.";
			return false;
		}
	}
	for (int i = 0; withheld_names[i]; i++) {
		if (file.to_lower() == withheld_names[i]) {
			r_reason = "'" + file + "' can hold signing keys and passwords, so it is not read.";
			return false;
		}
	}
	const String extension = file.get_extension().to_lower();
	for (int i = 0; text_extensions[i]; i++) {
		if (extension == text_extensions[i]) {
			return true;
		}
	}
	r_reason = "'." + extension + "' is not a text file type this tool reads.";
	return false;
}

String AIToolbox::_read_file(const Dictionary &p_arguments, bool &r_ok) const {
	const String path = String(p_arguments.get("path", "")).strip_edges();
	String reason;
	if (!is_readable_project_file(path, reason)) {
		return fail(r_ok, reason);
	}
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null()) {
		return fail(r_ok, "'" + path + "' does not exist or cannot be opened. Call list_files to see what is there.");
	}
	const PackedStringArray lines = file->get_as_utf8_string().replace("\r\n", "\n").split("\n");
	const int start = int_argument(p_arguments, "start_line", 1, 1, MAX(1, lines.size()));
	const int count = int_argument(p_arguments, "max_lines", DEFAULT_FILE_LINES, 1, MAX_FILE_LINES);
	const int end = MIN(lines.size(), start - 1 + count);

	String out = path + " (" + itos(lines.size()) + " lines)\n";
	for (int i = start - 1; i < end; i++) {
		out += itos(i + 1) + ": " + lines[i] + "\n";
	}
	if (end < lines.size()) {
		out += "(" + itos(lines.size() - end) + " more lines; call again with start_line=" + itos(end + 1) + ")\n";
	}
	return out;
}

String AIToolbox::_list_files(const Dictionary &p_arguments, bool &r_ok) const {
	String dir_path = String(p_arguments.get("dir", "res://")).strip_edges();
	if (dir_path.is_empty()) {
		dir_path = "res://";
	}
	if (!dir_path.begins_with("res://") || dir_path.contains("..") || dir_path.contains("\\")) {
		return fail(r_ok, "only directories inside the project (res://) can be listed.");
	}
	for (const String &segment : dir_path.trim_prefix("res://").split("/", false)) {
		if (segment.begins_with(".")) {
			return fail(r_ok, "hidden folders are not listed.");
		}
	}
	Ref<DirAccess> dir = DirAccess::open(dir_path);
	if (dir.is_null()) {
		return fail(r_ok, "'" + dir_path + "' is not a directory of the project.");
	}
	PackedStringArray folders;
	PackedStringArray files;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		if (dir->current_is_dir()) {
			folders.push_back(name + "/");
		} else if (!name.ends_with(".import") && !name.ends_with(".uid")) {
			files.push_back(name);
		}
	}
	dir->list_dir_end();
	folders.sort();
	files.sort();
	folders.append_array(files);

	String out = dir_path + " (" + itos(folders.size()) + " entries)\n";
	for (int i = 0; i < folders.size() && i < MAX_LIST_ENTRIES; i++) {
		out += folders[i] + "\n";
	}
	if (folders.size() > MAX_LIST_ENTRIES) {
		out += "(" + itos(folders.size() - MAX_LIST_ENTRIES) + " more not shown)\n";
	}
	return out;
}

String AIToolbox::_get_errors(const Dictionary & /*p_arguments*/, bool &r_ok) const {
	if (error_monitor.is_null()) {
		return fail(r_ok, "errors are not being recorded.");
	}
	const String console = error_monitor->get_recent_console_errors(ERROR_COUNT).strip_edges();
	const String game = error_monitor->get_recent_debugger_errors(ERROR_COUNT).strip_edges();
	String out = "Editor console:\n" + (console.is_empty() ? String("(no recent errors)") : console) + "\n";
	out += "\nLast run of the game:\n" + (game.is_empty() ? String("(no recent errors)") : game) + "\n";
	return out;
}
