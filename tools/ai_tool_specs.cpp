#include "ai_toolbox.h"

// What the model is told about each tool: its name, when to use it, and the
// JSON Schema of its arguments. The descriptions are prompt text — they are
// what the model decides by.

namespace {

Dictionary property(const char *p_type, const char *p_description) {
	Dictionary schema;
	schema["type"] = p_type;
	schema["description"] = p_description;
	return schema;
}

Dictionary string_list(const char *p_description, const char *p_item_description) {
	Dictionary schema = property("array", p_description);
	schema["items"] = property("string", p_item_description);
	return schema;
}

Dictionary object_schema(const Dictionary &p_properties, const PackedStringArray &p_required = PackedStringArray()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	if (!p_required.is_empty()) {
		schema["required"] = p_required;
	}
	return schema;
}

AIToolSpec spec(const char *p_name, AIToolSpec::Kind p_kind, const char *p_description, const Dictionary &p_parameters) {
	AIToolSpec tool;
	tool.name = p_name;
	tool.kind = p_kind;
	tool.description = p_description;
	tool.parameters = p_parameters;
	return tool;
}

const char *NODE_PATH = "Path of the node relative to the scene root, e.g. \"Player/Sprite2D\". \".\" is the root itself.";
const char *PROPERTIES =
		"Property values by name. Write engine types the way GDScript does, as text: \"Vector2(100, 50)\", \"Color(1, 0, 0)\". "
		"A resource is a res:// path, or a new one: {\"$resource\": \"RectangleShape2D\", \"size\": \"Vector2(32, 32)\"}.";

void add_read_tools(Vector<AIToolSpec> &r_specs) {
	Dictionary tree;
	tree["max_depth"] = property("integer", "How many levels below the root to list. Default 8.");
	r_specs.push_back(spec("get_scene_tree", AIToolSpec::KIND_READ,
			"Lists the nodes of the scene open in the editor as an indented tree: name, class, attached script, instanced scene. Call it before referring to nodes by path.",
			object_schema(tree)));

	Dictionary node;
	node["path"] = property("string", NODE_PATH);
	node["properties"] = string_list("Names of the properties to read. Omit to get every property that differs from the class default.", "A property name, e.g. \"position\".");
	r_specs.push_back(spec("get_node_properties", AIToolSpec::KIND_READ,
			"Reads the current property values of one node in the edited scene, plus its class, script, groups, children and signal connections. Use it to check a value instead of assuming it.",
			object_schema(node, { "path" })));

	Dictionary file;
	file["path"] = property("string", "A res:// path to a text file: a script, scene, resource or data file.");
	file["start_line"] = property("integer", "First line to return, counting from 1. Default 1.");
	file["max_lines"] = property("integer", "How many lines to return. Default 200, at most 400.");
	r_specs.push_back(spec("read_file", AIToolSpec::KIND_READ,
			"Reads a text file of the project, with line numbers. Read a script before changing it.",
			object_schema(file, { "path" })));

	Dictionary list;
	list["dir"] = property("string", "A res:// directory. Default \"res://\".");
	r_specs.push_back(spec("list_files", AIToolSpec::KIND_READ,
			"Lists the files and folders directly inside a project directory.",
			object_schema(list)));

	r_specs.push_back(spec("get_errors", AIToolSpec::KIND_READ,
			"Returns the most recent errors and warnings from the editor console and from the last run of the game.",
			object_schema(Dictionary())));
}

void add_edit_tools(Vector<AIToolSpec> &r_specs) {
	Dictionary create;
	create["parent"] = property("string", "Path of the node to add it under. Default \".\", the scene root. In an empty scene the new node becomes the root.");
	create["class"] = property("string", "The node's class, e.g. \"CharacterBody2D\".");
	create["name"] = property("string", "The node's name. If a sibling already has it, a number is appended; the result says the name used.");
	create["properties"] = property("object", PROPERTIES);
	create["script"] = property("string", "res:// path of a script to attach.");
	create["groups"] = string_list("Groups to add the node to.", "A group name.");
	r_specs.push_back(spec("create_node", AIToolSpec::KIND_EDIT,
			"Adds a new node to the edited scene and sets its properties. One call per node; add a parent before its children. The user can undo it.",
			object_schema(create, { "class" })));

	Dictionary instance;
	instance["parent"] = property("string", "Path of the node to add it under. Default \".\".");
	instance["scene"] = property("string", "res:// path of the .tscn to instance.");
	instance["name"] = property("string", "Name for the instance. Default: the scene's root name.");
	instance["properties"] = property("object", PROPERTIES);
	r_specs.push_back(spec("instantiate_scene", AIToolSpec::KIND_EDIT,
			"Adds an instance of a saved scene to the edited scene.",
			object_schema(instance, { "scene" })));

	Dictionary set;
	set["path"] = property("string", NODE_PATH);
	set["properties"] = property("object", PROPERTIES);
	set["add_groups"] = string_list("Groups to add the node to.", "A group name.");
	set["remove_groups"] = string_list("Groups to take the node out of.", "A group name.");
	r_specs.push_back(spec("set_node_properties", AIToolSpec::KIND_EDIT,
			"Changes properties (and groups) of a node in the edited scene. Either all of them are set or, if one is wrong, none.",
			object_schema(set, { "path" })));

	Dictionary rename;
	rename["path"] = property("string", NODE_PATH);
	rename["name"] = property("string", "The new name.");
	r_specs.push_back(spec("rename_node", AIToolSpec::KIND_EDIT,
			"Renames a node. Scripts that refer to it by path are not updated.",
			object_schema(rename, { "path", "name" })));

	Dictionary remove;
	remove["path"] = property("string", "Path of the node to delete, relative to the scene root. The root itself cannot be deleted.");
	r_specs.push_back(spec("delete_node", AIToolSpec::KIND_EDIT,
			"Deletes a node and everything under it from the edited scene. Only when the user asked for it to go.",
			object_schema(remove, { "path" })));

	Dictionary connect;
	connect["source"] = property("string", "Path of the node that emits the signal.");
	connect["signal"] = property("string", "Name of the signal, e.g. \"pressed\" or \"body_entered\".");
	connect["target"] = property("string", "Path of the node whose method is called.");
	connect["method"] = property("string", "Name of the method on the target, e.g. \"_on_button_pressed\".");
	r_specs.push_back(spec("connect_signal", AIToolSpec::KIND_EDIT,
			"Connects a signal of one node to a method of another, saved with the scene (as in the editor's Node dock). The method has to exist in the target's script.",
			object_schema(connect, { "source", "signal", "target", "method" })));

	Dictionary attach;
	attach["path"] = property("string", NODE_PATH);
	attach["script"] = property("string", "res:// path of the .gd file. Empty removes the node's script.");
	r_specs.push_back(spec("attach_script", AIToolSpec::KIND_EDIT,
			"Attaches a script file to a node of the edited scene. Write the file first with write_file.",
			object_schema(attach, { "path", "script" })));

	Dictionary write;
	write["path"] = property("string", "res:// path of the file. Folders are created as needed. Put files in a folder, not in res:// itself.");
	write["content"] = property("string", "The whole content of the file. For a script, the complete script: this replaces the file.");
	r_specs.push_back(spec("write_file", AIToolSpec::KIND_EDIT,
			"Creates or replaces a text file of the project: a script (.gd), shader, or data file. A script that does not parse is not written; the result gives the errors. Read an existing file before replacing it.",
			object_schema(write, { "path", "content" })));

	Dictionary save;
	save["path"] = property("string", "res:// path of the .tscn. Needed only the first time, for a scene that has no file yet.");
	r_specs.push_back(spec("save_scene", AIToolSpec::KIND_EDIT,
			"Saves the edited scene. Do it when the work on the scene is done, or before run_scene.",
			object_schema(save)));
}

void add_host_tools(Vector<AIToolSpec> &r_specs) {
	Dictionary run;
	run["scene"] = property("string", "res:// path of the scene to run. Default: the edited scene.");
	run["seconds"] = property("integer", "How long to let it run before reporting, 1 to 10. Default 3.");
	r_specs.push_back(spec("run_scene", AIToolSpec::KIND_HOST,
			"Runs a scene of the game for a few seconds, stops it, and returns the errors it raised. Use it to check that what you built starts and runs. It cannot play the game or see it.",
			object_schema(run)));

	r_specs.push_back(spec("take_screenshot", AIToolSpec::KIND_HOST,
			"Captures the editor's viewport and shows it to you. Use it to check layout and appearance.",
			object_schema(Dictionary())));

	Dictionary script;
	script["code"] = property("string",
			"GDScript: the body of an EditorScript's _run(), without the func line. Get the scene with EditorInterface.get_edited_scene_root(); "
			"after add_child() set owner to the scene root; end with print() of what was done.");
	script["description"] = property("string", "One sentence on what the code does, shown to the user who has to allow it.");
	r_specs.push_back(spec("run_editor_script", AIToolSpec::KIND_HOST,
			"Runs GDScript in the editor. For what no other tool can do: project settings, input actions, animations, tile maps, bulk changes. "
			"It is checked against the safety policy and the user's permissions, and may need the user's approval. Prefer the specific tools.",
			object_schema(script, { "code", "description" })));
}

} // namespace

Vector<AIToolSpec> AIToolbox::get_specs(bool p_read_only) const {
	Vector<AIToolSpec> specs;
	add_read_tools(specs);
	if (!p_read_only) {
		add_edit_tools(specs);
		add_host_tools(specs);
	}
	return specs;
}
