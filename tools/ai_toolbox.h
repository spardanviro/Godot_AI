#pragma once

#include "../common/ai_tool_types.h"
#include "../execution/ai_error_monitor.h"

class Node;

// The tools a model can call instead of writing code: typed, named
// operations on the editor.
//
//   - Tools that look: the scene tree, a node's properties, the project's
//     files, recent errors. So that the model checks instead of guessing.
//   - Tools that edit: add, change, connect and delete nodes, attach
//     scripts, write files. Each scene change is one undoable editor action,
//     and each falls under one of the user's permission categories.
//   - Host tools (running the game, a screenshot, an arbitrary script) are
//     only described here; the agent loop carries them out.
//
// A tool never fails by raising: what went wrong is the result, as text, and
// the model reads it like any other.
class AIToolbox {
public:
	// p_read_only: only the tools that look (ASK and PLAN modes).
	Vector<AIToolSpec> get_specs(bool p_read_only = false) const;
	bool has_tool(const String &p_name) const;
	// KIND_READ for a name that is not a tool: calling it only yields an error.
	AIToolSpec::Kind get_kind(const String &p_name) const;
	// The AIPermissionManager::Category a call falls under, or -1 if it needs
	// no permission. Depends on the arguments: writing a script is not
	// writing a data file.
	int get_permission_category(const AIToolCall &p_call) const;

	// Runs a tool that looks or edits. p_arguments is the JSON text the model
	// sent. r_ok is false when the result is an error message.
	String call(const String &p_name, const String &p_arguments, bool &r_ok) const;
	// A one-line description of a call for the transcript: `name(key: value)`.
	static String describe_call(const AIToolCall &p_call);

	void set_error_monitor(const Ref<AIErrorMonitor> &p_error_monitor) { error_monitor = p_error_monitor; }
	// Tests: the tree that stands in for the edited scene.
	void set_scene_root_for_tests(Node *p_root) { test_scene_root = p_root; }

	// Whether a file may be read by the model: a text file inside the
	// project that is not one of the places secrets are kept. r_reason says
	// why not.
	static bool is_readable_project_file(const String &p_path, String &r_reason);
	// Narrower: also not a file the editor owns.
	static bool is_writable_project_file(const String &p_path, String &r_reason);
	static bool parse_arguments(const String &p_arguments, Dictionary &r_arguments);

	static const int MAX_TREE_NODES = 300;
	static const int MAX_PROPERTIES = 80;
	static const int MAX_VALUE_CHARS = 300;
	static const int MAX_FILE_LINES = 400;
	static const int MAX_RESULT_CHARS = 20000;
	static const int MAX_LIST_ENTRIES = 200;

private:
	Ref<AIErrorMonitor> error_monitor;
	Node *test_scene_root = nullptr;

	Node *_get_scene_root() const;
	// Whether changes go through the editor's undo history (not in unit tests).
	bool _uses_editor() const;
	Node *_find_node(const String &p_path, String &r_error) const;
	static String _value_text(const Variant &p_value);
	String _add_prepared_node(Node *p_node, const Dictionary &p_arguments, bool &r_ok) const;
	String _create_node(const Dictionary &p_arguments, bool &r_ok) const;
	String _instantiate_scene(const Dictionary &p_arguments, bool &r_ok) const;
	String _set_node_properties(const Dictionary &p_arguments, bool &r_ok) const;
	String _rename_node(const Dictionary &p_arguments, bool &r_ok) const;
	String _delete_node(const Dictionary &p_arguments, bool &r_ok) const;
	String _connect_signal(const Dictionary &p_arguments, bool &r_ok) const;
	String _attach_script(const Dictionary &p_arguments, bool &r_ok) const;
	String _write_file(const Dictionary &p_arguments, bool &r_ok) const;
	String _save_scene(const Dictionary &p_arguments, bool &r_ok) const;
	String _get_scene_tree(const Dictionary &p_arguments, bool &r_ok) const;
	String _get_node_properties(const Dictionary &p_arguments, bool &r_ok) const;
	String _read_file(const Dictionary &p_arguments, bool &r_ok) const;
	String _list_files(const Dictionary &p_arguments, bool &r_ok) const;
	String _get_errors(const Dictionary &p_arguments, bool &r_ok) const;
};
