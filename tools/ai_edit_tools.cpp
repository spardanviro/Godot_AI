#include "ai_toolbox.h"

#include "ai_tool_values.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_parser.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#endif

// The tools that change the project. Every change to the scene is one action
// of the editor's undo history, named after what it did, so Ctrl+Z takes it
// back like anything the user did by hand. Without an editor (unit tests) the
// same changes are applied directly.

namespace {

String fail(bool &r_ok, const String &p_message) {
	r_ok = false;
	return "Error: " + p_message;
}

Dictionary dictionary_argument(const Dictionary &p_arguments, const char *p_name) {
	const Variant value = p_arguments.get(p_name, Variant());
	return value.get_type() == Variant::DICTIONARY ? Dictionary(value) : Dictionary();
}

PackedStringArray strings_argument(const Dictionary &p_arguments, const char *p_name) {
	PackedStringArray strings;
	const Variant value = p_arguments.get(p_name, Variant());
	if (value.get_type() == Variant::ARRAY) {
		const Array array = value;
		for (int i = 0; i < array.size(); i++) {
			const String text = String(array[i]).strip_edges();
			if (!text.is_empty()) {
				strings.push_back(text);
			}
		}
	}
	return strings;
}

String path_of(const Node *p_root, const Node *p_node) {
	return p_node == p_root ? String(".") : String(p_root->get_path_to(p_node));
}

#ifdef TOOLS_ENABLED
EditorUndoRedoManager *undo_manager(bool p_use_editor) {
	return p_use_editor ? EditorUndoRedoManager::get_singleton() : nullptr;
}
#endif

// Sets properties as one undoable action, or directly without an editor.
void set_properties(Object *p_object, const Dictionary &p_values, const String &p_action, bool p_use_editor) {
#ifdef TOOLS_ENABLED
	if (EditorUndoRedoManager *undo = undo_manager(p_use_editor)) {
		undo->create_action(p_action);
		for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
			undo->add_do_property(p_object, *key, p_values[*key]);
			undo->add_undo_property(p_object, *key, p_object->get(*key));
		}
		undo->commit_action();
		return;
	}
#endif
	for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
		p_object->set(*key, p_values[*key]);
	}
}

// Adds a node that is not in the tree yet, owned by the scene root, and then
// sets its properties — in that order, and in the order given: a Control's
// anchors and layout are worked out against its parent.
void add_node(Node *p_parent, Node *p_node, Node *p_root, const Dictionary &p_values, const String &p_action, bool p_use_editor) {
#ifdef TOOLS_ENABLED
	if (EditorUndoRedoManager *undo = undo_manager(p_use_editor)) {
		undo->create_action(p_action);
		undo->add_do_method(p_parent, "add_child", p_node, true);
		undo->add_do_method(p_node, "set_owner", p_root);
		for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
			undo->add_do_property(p_node, *key, p_values[*key]);
		}
		undo->add_do_reference(p_node);
		undo->add_undo_method(p_parent, "remove_child", p_node);
		undo->commit_action();
		return;
	}
#endif
	p_parent->add_child(p_node, true);
	p_node->set_owner(p_root);
	for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
		p_node->set(*key, p_values[*key]);
	}
}

// A Control ignores "anchors_preset" unless its layout mode is "Anchors",
// which the Inspector switches to by itself when a preset is picked. So does
// this: the mode goes first, then the preset, then the rest as given.
Dictionary with_layout_mode(const Object *p_object, const Dictionary &p_values) {
	static const int LAYOUT_MODE_ANCHORS = 1;
	if (!p_values.has("anchors_preset") || p_values.has("layout_mode") || !ClassDB::is_parent_class(p_object->get_class_name(), "Control")) {
		return p_values;
	}
	Dictionary ordered;
	ordered["layout_mode"] = LAYOUT_MODE_ANCHORS;
	ordered["anchors_preset"] = p_values["anchors_preset"];
	for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
		ordered[*key] = p_values[*key];
	}
	return ordered;
}

void collect_owned(Node *p_node, const Node *p_owner, Vector<Node *> &r_owned) {
	if (p_node->get_owner() == p_owner) {
		r_owned.push_back(p_node);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		collect_owned(p_node->get_child(i), p_owner, r_owned);
	}
}

String describe_values(const Dictionary &p_values) {
	PackedStringArray names;
	for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
		names.push_back(*key);
	}
	return names.is_empty() ? String("nothing") : String(", ").join(names);
}

} // namespace

bool AIToolbox::_uses_editor() const {
#ifdef TOOLS_ENABLED
	return !test_scene_root && EditorNode::get_singleton() && EditorUndoRedoManager::get_singleton();
#else
	return false;
#endif
}

Node *AIToolbox::_find_node(const String &p_path, String &r_error) const {
	Node *root = _get_scene_root();
	if (!root) {
		r_error = "no scene is open in the editor.";
		return nullptr;
	}
	const String path = p_path.strip_edges();
	const String root_name = root->get_name();
	// A path that starts at the root's own name ("Main/Player") is accepted too.
	Node *node = path.is_empty() || path == "." || path == root_name ? root : root->get_node_or_null(NodePath(path));
	if (!node && path.begins_with(root_name + "/")) {
		node = root->get_node_or_null(NodePath(path.substr(root_name.length() + 1)));
	}
	if (!node || (node != root && !root->is_ancestor_of(node))) {
		r_error = "there is no node at '" + path + "' in the edited scene. Call get_scene_tree to see the paths.";
		return nullptr;
	}
	return node;
}

// =============================================================================
// Nodes
// =============================================================================

// Finishes a node that is about to be added: script and groups, and the
// values its properties are to get (r_values; set once it is in the tree).
// On false nothing was added and the caller frees the node.
static bool prepare_node(Node *p_node, const Dictionary &p_arguments, Dictionary &r_values, String &r_error) {
	if (!AIToolValues::convert_properties(p_node, dictionary_argument(p_arguments, "properties"), r_values, r_error)) {
		return false;
	}
	r_values = with_layout_mode(p_node, r_values);
	const String script_path = String(p_arguments.get("script", "")).strip_edges();
	if (!script_path.is_empty()) {
		const Ref<Script> script = ResourceLoader::exists(script_path) ? ResourceLoader::load(script_path, "Script") : Ref<Resource>();
		if (script.is_null()) {
			r_error = "'" + script_path + "' is not a script that exists. Write it first with write_file.";
			return false;
		}
		p_node->set_script(script);
	}
	for (const String &group : strings_argument(p_arguments, "groups")) {
		p_node->add_to_group(group, true);
	}
	return true;
}

String AIToolbox::_add_prepared_node(Node *p_node, const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *root = _get_scene_root();
	Dictionary values;
	if (!prepare_node(p_node, p_arguments, values, error)) {
		memdelete(p_node);
		return fail(r_ok, error);
	}
	const String wanted_name = String(p_arguments.get("name", "")).validate_node_name().strip_edges();
	if (!wanted_name.is_empty()) {
		p_node->set_name(wanted_name);
	}

	if (!root) {
#ifdef TOOLS_ENABLED
		// An empty scene: the node becomes its root.
		if (_uses_editor() && String(p_arguments.get("parent", ".")).strip_edges().trim_suffix(".").is_empty()) {
			EditorInterface::get_singleton()->add_root_node(p_node);
			for (const Variant *key = values.next(nullptr); key; key = values.next(key)) {
				p_node->set(*key, values[*key]);
			}
			return "Created the scene root " + String(p_node->get_name()) + " (" + p_node->get_class() + "). The scene has no file yet: save_scene with a path.";
		}
#endif
		memdelete(p_node);
		return fail(r_ok, "no scene is open in the editor.");
	}
	Node *parent = _find_node(p_arguments.get("parent", "."), error);
	if (!parent) {
		memdelete(p_node);
		return fail(r_ok, error);
	}
	add_node(parent, p_node, root, values, "AI: Add " + String(p_node->get_name()), _uses_editor());
	String out = "Created " + path_of(root, p_node) + " (" + p_node->get_class() + ").";
	if (!wanted_name.is_empty() && String(p_node->get_name()) != wanted_name) {
		out += " The name '" + wanted_name + "' was taken, so it is called '" + String(p_node->get_name()) + "'.";
	}
	return out;
}

String AIToolbox::_create_node(const Dictionary &p_arguments, bool &r_ok) const {
	const String class_name = String(p_arguments.get("class", "")).strip_edges();
	String error;
	if (class_name.is_empty()) {
		return fail(r_ok, "'class' is required.");
	}
	if (!AIToolValues::is_instantiable(class_name, "Node", error)) {
		return fail(r_ok, error);
	}
	Node *node = Object::cast_to<Node>(ClassDB::instantiate(class_name));
	if (!node) {
		return fail(r_ok, "'" + class_name + "' could not be created.");
	}
	if (String(p_arguments.get("name", "")).strip_edges().is_empty()) {
		node->set_name(class_name);
	}
	return _add_prepared_node(node, p_arguments, r_ok);
}

String AIToolbox::_instantiate_scene(const Dictionary &p_arguments, bool &r_ok) const {
	const String scene_path = String(p_arguments.get("scene", "")).strip_edges();
	if (!scene_path.begins_with("res://") || !ResourceLoader::exists(scene_path)) {
		return fail(r_ok, "'" + scene_path + "' is not a scene file of the project.");
	}
	const Node *root = _get_scene_root();
	if (root && root->get_scene_file_path() == scene_path) {
		return fail(r_ok, "a scene cannot contain an instance of itself.");
	}
	const Ref<PackedScene> scene = ResourceLoader::load(scene_path, "PackedScene");
	if (scene.is_null()) {
		return fail(r_ok, "'" + scene_path + "' could not be loaded as a scene.");
	}
	Node *node = scene->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (!node) {
		return fail(r_ok, "'" + scene_path + "' could not be instantiated.");
	}
	return _add_prepared_node(node, p_arguments, r_ok);
}

String AIToolbox::_set_node_properties(const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *node = _find_node(p_arguments.get("path", ""), error);
	if (!node) {
		return fail(r_ok, error);
	}
	Dictionary values;
	if (!AIToolValues::convert_properties(node, dictionary_argument(p_arguments, "properties"), values, error)) {
		return fail(r_ok, error);
	}
	values = with_layout_mode(node, values);
	const PackedStringArray add_groups = strings_argument(p_arguments, "add_groups");
	const PackedStringArray remove_groups = strings_argument(p_arguments, "remove_groups");
	if (values.is_empty() && add_groups.is_empty() && remove_groups.is_empty()) {
		return fail(r_ok, "nothing to set: give 'properties', 'add_groups' or 'remove_groups'.");
	}

	const String action = "AI: Set " + describe_values(values) + " of " + String(node->get_name());
#ifdef TOOLS_ENABLED
	if (EditorUndoRedoManager *undo = undo_manager(_uses_editor())) {
		undo->create_action(action);
		for (const Variant *key = values.next(nullptr); key; key = values.next(key)) {
			undo->add_do_property(node, *key, values[*key]);
			undo->add_undo_property(node, *key, node->get(*key));
		}
		for (const String &group : add_groups) {
			if (!node->is_in_group(group)) {
				undo->add_do_method(node, "add_to_group", group, true);
				undo->add_undo_method(node, "remove_from_group", group);
			}
		}
		for (const String &group : remove_groups) {
			if (node->is_in_group(group)) {
				undo->add_do_method(node, "remove_from_group", group);
				undo->add_undo_method(node, "add_to_group", group, true);
			}
		}
		undo->commit_action();
	} else
#endif
	{
		set_properties(node, values, action, false);
		for (const String &group : add_groups) {
			node->add_to_group(group, true);
		}
		for (const String &group : remove_groups) {
			node->remove_from_group(group);
		}
	}

	String out = "Set on " + path_of(_get_scene_root(), node) + ":\n";
	for (const Variant *key = values.next(nullptr); key; key = values.next(key)) {
		out += "  " + String(*key) + " = " + _value_text(node->get(*key)) + "\n";
	}
	if (!add_groups.is_empty()) {
		out += "  added to groups: " + String(", ").join(add_groups) + "\n";
	}
	if (!remove_groups.is_empty()) {
		out += "  removed from groups: " + String(", ").join(remove_groups) + "\n";
	}
	return out;
}

String AIToolbox::_rename_node(const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *node = _find_node(p_arguments.get("path", ""), error);
	if (!node) {
		return fail(r_ok, error);
	}
	const String name = String(p_arguments.get("name", "")).validate_node_name().strip_edges();
	if (name.is_empty()) {
		return fail(r_ok, "'name' is required.");
	}
	Node *parent = node->get_parent();
	if (parent && node != _get_scene_root() && parent->has_node(NodePath(name)) && parent->get_node(NodePath(name)) != node) {
		return fail(r_ok, "'" + String(parent->get_name()) + "' already has a child named '" + name + "'.");
	}
	Dictionary values;
	values["name"] = name;
	set_properties(node, values, "AI: Rename " + String(node->get_name()) + " to " + name, _uses_editor());
	return "Renamed to " + path_of(_get_scene_root(), node) + ". Scripts that used the old path were not changed.";
}

String AIToolbox::_move_node(const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *root = _get_scene_root();
	Node *node = _find_node(p_arguments.get("path", ""), error);
	if (!node) {
		return fail(r_ok, error);
	}
	if (node == root) {
		return fail(r_ok, "the scene root cannot be moved.");
	}
	Node *parent = _find_node(p_arguments.get("parent", ""), error);
	if (!parent) {
		return fail(r_ok, "'parent': " + error);
	}
	if (parent == node || node->is_ancestor_of(parent)) {
		return fail(r_ok, "a node cannot be moved under itself.");
	}
	Node *old_parent = node->get_parent();
	const int old_index = node->get_index();
	const String name = node->get_name();
	if (parent != old_parent && parent->has_node(NodePath(name))) {
		return fail(r_ok, "'" + path_of(root, parent) + "' already has a child named '" + name + "'. Rename one of them first.");
	}
	const Variant index_argument = p_arguments.get("index", Variant());
	const int last = parent->get_child_count() - (parent == old_parent ? 1 : 0);
	const int index = index_argument.is_num() ? CLAMP(int(index_argument), 0, last) : last;
	if (parent == old_parent && index == old_index) {
		return path_of(root, node) + " is already there.";
	}

	// Where it is on screen is kept, as the editor does when a node is
	// dragged to another parent. (Only a node in the tree has such a place.)
	StringName placement;
	if (parent != old_parent && node->is_inside_tree() && parent->is_inside_tree()) {
		if (node->is_class("Node2D") || node->is_class("Node3D")) {
			placement = "global_transform";
		} else if (node->is_class("Control")) {
			placement = "global_position";
		}
	}
	const Variant placed = placement == StringName() ? Variant() : node->get(placement);
	// The owners under the node are cleared when it leaves its parent; its
	// own is dropped here, since a node that is added while owned by what is
	// not yet its ancestor draws a warning. All are set again once it is in.
	Vector<Node *> owned;
	collect_owned(node, root, owned);
	const String old_path = path_of(root, node);

#ifdef TOOLS_ENABLED
	if (EditorUndoRedoManager *undo = undo_manager(_uses_editor())) {
		undo->create_action("AI: Move " + name);
		undo->add_do_method(old_parent, "remove_child", node);
		undo->add_do_method(node, "set_owner", Variant());
		undo->add_do_method(parent, "add_child", node, true);
		undo->add_do_method(parent, "move_child", node, index);
		undo->add_undo_method(parent, "remove_child", node);
		undo->add_undo_method(node, "set_owner", Variant());
		undo->add_undo_method(old_parent, "add_child", node, true);
		undo->add_undo_method(old_parent, "move_child", node, old_index);
		for (Node *owned_node : owned) {
			undo->add_do_method(owned_node, "set_owner", root);
			undo->add_undo_method(owned_node, "set_owner", root);
		}
		if (placement != StringName()) {
			undo->add_do_property(node, placement, placed);
			undo->add_undo_property(node, placement, placed);
		}
		undo->commit_action();
		return "Moved " + old_path + " to " + path_of(root, node) + ". Scripts that used the old path were not changed.";
	}
#endif
	old_parent->remove_child(node);
	node->set_owner(nullptr);
	parent->add_child(node, true);
	parent->move_child(node, index);
	for (Node *owned_node : owned) {
		owned_node->set_owner(root);
	}
	if (placement != StringName()) {
		node->set(placement, placed);
	}
	return "Moved " + old_path + " to " + path_of(root, node) + ". Scripts that used the old path were not changed.";
}

String AIToolbox::_delete_node(const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *root = _get_scene_root();
	Node *node = _find_node(p_arguments.get("path", ""), error);
	if (!node) {
		return fail(r_ok, error);
	}
	if (node == root) {
		return fail(r_ok, "the scene root cannot be deleted.");
	}
	Node *parent = node->get_parent();
	const String path = path_of(root, node);
	const int descendants = node->get_child_count();
#ifdef TOOLS_ENABLED
	if (EditorUndoRedoManager *undo = undo_manager(_uses_editor())) {
		// Leaving the tree clears the owner of every node under it; undo puts
		// each one back.
		Vector<Node *> owned;
		collect_owned(node, root, owned);
		undo->create_action("AI: Delete " + String(node->get_name()));
		undo->add_do_method(parent, "remove_child", node);
		undo->add_undo_method(parent, "add_child", node, true);
		undo->add_undo_method(parent, "move_child", node, node->get_index());
		for (Node *owned_node : owned) {
			undo->add_undo_method(owned_node, "set_owner", root);
		}
		undo->add_undo_reference(node);
		undo->commit_action();
		return "Deleted " + path + (descendants > 0 ? " and what was under it." : ".");
	}
#endif
	parent->remove_child(node);
	memdelete(node);
	return "Deleted " + path + (descendants > 0 ? " and what was under it." : ".");
}

String AIToolbox::_connect_signal(const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *source = _find_node(p_arguments.get("source", ""), error);
	if (!source) {
		return fail(r_ok, error);
	}
	Node *target = _find_node(p_arguments.get("target", ""), error);
	if (!target) {
		return fail(r_ok, error);
	}
	const String signal = String(p_arguments.get("signal", "")).strip_edges();
	const String method = String(p_arguments.get("method", "")).strip_edges();
	if (!source->has_signal(signal)) {
		return fail(r_ok, source->get_class() + " '" + String(source->get_name()) + "' has no signal '" + signal + "'.");
	}
	if (!method.is_valid_ascii_identifier()) {
		return fail(r_ok, "'" + method + "' is not a method name.");
	}
	const Callable callable(target, method);
	if (source->is_connected(signal, callable)) {
		return "Already connected: " + String(source->get_name()) + "." + signal + " -> " + String(target->get_name()) + "." + method + "().";
	}
#ifdef TOOLS_ENABLED
	if (EditorUndoRedoManager *undo = undo_manager(_uses_editor())) {
		undo->create_action("AI: Connect " + signal + " of " + String(source->get_name()));
		undo->add_do_method(source, "connect", signal, callable, (int)Object::CONNECT_PERSIST);
		undo->add_undo_method(source, "disconnect", signal, callable);
		undo->commit_action();
	} else
#endif
	{
		source->connect(signal, callable, Object::CONNECT_PERSIST);
	}
	String out = "Connected " + String(source->get_name()) + "." + signal + " -> " + String(target->get_name()) + "." + method + "().";
	if (!target->has_method(method)) {
		out += " Note: '" + String(target->get_name()) + "' has no method '" + method + "' yet; its script must define it.";
	}
	return out;
}

String AIToolbox::_attach_script(const Dictionary &p_arguments, bool &r_ok) const {
	String error;
	Node *node = _find_node(p_arguments.get("path", ""), error);
	if (!node) {
		return fail(r_ok, error);
	}
	const String script_path = String(p_arguments.get("script", "")).strip_edges();
	Ref<Script> script;
	if (!script_path.is_empty()) {
		if (!script_path.begins_with("res://") || !ResourceLoader::exists(script_path)) {
			return fail(r_ok, "'" + script_path + "' does not exist. Write it first with write_file.");
		}
		script = ResourceLoader::load(script_path, "Script");
		if (script.is_null()) {
			return fail(r_ok, "'" + script_path + "' is not a script.");
		}
		const StringName base = script->get_instance_base_type();
		if (base != StringName() && !ClassDB::is_parent_class(node->get_class_name(), base)) {
			return fail(r_ok, "the script extends " + String(base) + ", which a " + node->get_class() + " is not.");
		}
	}
	Dictionary values;
	values["script"] = script;
	set_properties(node, values, script.is_null() ? "AI: Remove script of " + String(node->get_name()) : "AI: Attach script to " + String(node->get_name()), _uses_editor());
	return script.is_null() ? "Removed the script of " + path_of(_get_scene_root(), node) + "." : "Attached " + script_path + " to " + path_of(_get_scene_root(), node) + ".";
}

// =============================================================================
// Files
// =============================================================================

bool AIToolbox::is_writable_project_file(const String &p_path, String &r_reason) {
	if (!is_readable_project_file(p_path, r_reason)) {
		return false;
	}
	const String file = p_path.get_file().to_lower();
	const String extension = file.get_extension();
	if (file == "project.godot") {
		r_reason = "project.godot is changed through ProjectSettings (run_editor_script), not by writing the file.";
		return false;
	}
	if (extension == "import" || extension == "tscn" || extension == "tres") {
		r_reason = "'." + extension + "' files are written by the editor. Change a scene with the node tools and save_scene.";
		return false;
	}
	return true;
}

String AIToolbox::_write_file(const Dictionary &p_arguments, bool &r_ok) const {
	const String path = String(p_arguments.get("path", "")).strip_edges();
	String reason;
	if (!is_writable_project_file(path, reason)) {
		return fail(r_ok, reason);
	}
	if (p_arguments.get("content", Variant()).get_type() != Variant::STRING) {
		return fail(r_ok, "'content' is required: the whole text of the file.");
	}
	const String content = String(p_arguments["content"]).replace("\r\n", "\n");
	String compile_errors;

	// A script that does not parse would break whatever it is attached to.
	if (path.get_extension().to_lower() == "gd") {
		GDScriptParser parser;
		parser.parse(content, path, false);
		String errors;
		for (const GDScriptParser::ParserError &error : parser.get_errors()) {
			errors += "  line " + itos(error.start_line) + ": " + error.message + "\n";
		}
		if (!errors.is_empty()) {
			return fail(r_ok, "the script was not written because it does not parse:\n" + errors);
		}
		// It parses; whether it compiles (types, names) is the analyzer's
		// word. Such a script is still written — what it lacks may be a file
		// that comes next — but the model is told.
		GDScriptAnalyzer analyzer(&parser);
		if (analyzer.analyze() != OK) {
			for (const GDScriptParser::ParserError &error : parser.get_errors()) {
				compile_errors += "  line " + itos(error.start_line) + ": " + error.message + "\n";
			}
		}
	}

	const bool existed = FileAccess::exists(path);
	const Error dir_error = DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	Ref<FileAccess> file = dir_error == OK || dir_error == ERR_ALREADY_EXISTS ? FileAccess::open(path, FileAccess::WRITE) : Ref<FileAccess>();
	if (file.is_null()) {
		return fail(r_ok, "'" + path + "' could not be opened for writing.");
	}
	file->store_string(content);
	file.unref();

#ifdef TOOLS_ENABLED
	if (_uses_editor()) {
		// A script already in use picks up the new source at once.
		if (ResourceCache::has(path)) {
			const Ref<Script> script = ResourceCache::get_ref(path);
			if (script.is_valid()) {
				script->set_source_code(content);
				script->reload(true);
			}
		}
		if (EditorFileSystem::get_singleton()) {
			EditorFileSystem::get_singleton()->update_file(path);
		}
	}
#endif
	String out = String(existed ? "Replaced " : "Created ") + path + " (" + itos(content.split("\n").size()) + " lines).";
	if (!compile_errors.is_empty()) {
		out += "\nIt was written, but it does not compile yet:\n" + compile_errors + "Fix these and write the file again, unless they are about a file you are about to create.";
	}
	return out;
}

String AIToolbox::_save_scene(const Dictionary &p_arguments, bool &r_ok) const {
#ifdef TOOLS_ENABLED
	if (!_uses_editor()) {
		return fail(r_ok, "there is no editor to save with.");
	}
	const Node *root = _get_scene_root();
	if (!root) {
		return fail(r_ok, "no scene is open in the editor.");
	}
	const String path = String(p_arguments.get("path", "")).strip_edges();
	if (path.is_empty()) {
		if (root->get_scene_file_path().is_empty()) {
			return fail(r_ok, "the scene has no file yet: give 'path', e.g. res://scenes/main.tscn.");
		}
		const Error error = EditorInterface::get_singleton()->save_scene();
		return error == OK ? "Saved " + root->get_scene_file_path() + "." : fail(r_ok, "the scene could not be saved (error " + itos(error) + ").");
	}
	if (!root->get_scene_file_path().is_empty() && root->get_scene_file_path() != path) {
		return fail(r_ok, "the scene already has a file, " + root->get_scene_file_path() + ". Call save_scene without 'path' to save it there.");
	}
	if (!path.begins_with("res://") || path.contains("..") || path.get_extension().to_lower() != "tscn") {
		return fail(r_ok, "'path' has to be a .tscn of the project, e.g. res://scenes/main.tscn.");
	}
	DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	EditorInterface::get_singleton()->save_scene_as(path, false);
	return root->get_scene_file_path() == path ? "Saved as " + path + "." : fail(r_ok, "the scene could not be saved as " + path + ".");
#else
	return fail(r_ok, "there is no editor to save with.");
#endif
}
