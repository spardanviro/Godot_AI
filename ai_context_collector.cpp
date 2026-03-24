#include "ai_context_collector.h"

#include "scene/main/node.h"
#include "core/io/resource_loader.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_interface.h"
#include "editor/editor_data.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#endif

void AIContextCollector::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_scene_tree_description"), &AIContextCollector::get_scene_tree_description);
	ClassDB::bind_method(D_METHOD("get_selected_nodes_info"), &AIContextCollector::get_selected_nodes_info);
	ClassDB::bind_method(D_METHOD("get_project_structure"), &AIContextCollector::get_project_structure);
	ClassDB::bind_method(D_METHOD("get_current_script_info"), &AIContextCollector::get_current_script_info);
	ClassDB::bind_method(D_METHOD("build_context_prompt"), &AIContextCollector::build_context_prompt);
}

String AIContextCollector::_describe_node_recursive(Node *p_node, int p_depth, int p_max_depth) const {
	if (!p_node || p_depth > p_max_depth) {
		return "";
	}

	String indent;
	for (int i = 0; i < p_depth; i++) {
		indent += "  ";
	}

	String result = indent + "- " + p_node->get_name() + " (" + p_node->get_class() + ")";

	if (p_node->has_method("get_position")) {
		Variant pos = p_node->call("get_position");
		result += " pos=" + String(pos);
	}
	if (p_node->get_class() == "Label" || p_node->get_class() == "Button" || p_node->get_class() == "LineEdit") {
		Variant text = p_node->get("text");
		if (text.get_type() == Variant::STRING && !String(text).is_empty()) {
			result += " text=\"" + String(text).left(30) + "\"";
		}
	}
	if (p_node->get_class() == "Sprite2D" || p_node->get_class() == "TextureRect") {
		Variant tex = p_node->get("texture");
		if (tex.get_type() != Variant::NIL) {
			result += " has_texture";
		}
	}
	if (p_node->has_method("is_visible") && !(bool)p_node->call("is_visible")) {
		result += " [hidden]";
	}

	result += "\n";

	for (int i = 0; i < p_node->get_child_count(); i++) {
		result += _describe_node_recursive(p_node->get_child(i), p_depth + 1, p_max_depth);
	}

	return result;
}

String AIContextCollector::get_scene_tree_description() const {
#ifdef TOOLS_ENABLED
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		return "No scene is currently open.";
	}

	String result = "Current scene tree:\n";
	result += _describe_node_recursive(root, 0, 10);
	return result;
#else
	return "Scene tree info not available outside editor.";
#endif
}

String AIContextCollector::get_selected_nodes_info() const {
#ifdef TOOLS_ENABLED
	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	if (!selection) {
		return "No selection available.";
	}

	TypedArray<Node> selected = selection->get_selected_nodes();

	if (selected.is_empty()) {
		return "No nodes selected.";
	}

	String result = "Selected nodes:\n";
	for (int i = 0; i < selected.size(); i++) {
		Node *node = Object::cast_to<Node>(selected[i]);
		if (!node) {
			continue;
		}
		result += "- " + node->get_name() + " (" + node->get_class() + ") path: " + String(node->get_path()) + "\n";

		List<PropertyInfo> props;
		node->get_property_list(&props);
		int count = 0;
		for (const PropertyInfo &pi : props) {
			if (!(pi.usage & PROPERTY_USAGE_EDITOR)) {
				continue;
			}
			if (pi.name.begins_with("metadata/") || pi.name.begins_with("script")) {
				continue;
			}
			if (count >= 20) {
				break;
			}
			Variant val = node->get(pi.name);
			String val_str = String(val);
			if (val_str.length() > 80) {
				val_str = val_str.left(80) + "...";
			}
			result += "  " + pi.name + " = " + val_str + "\n";
			count++;
		}
	}

	return result;
#else
	return "Selection info not available outside editor.";
#endif
}

String AIContextCollector::get_project_structure() const {
#ifdef TOOLS_ENABLED
	EditorFileSystem *efs = EditorInterface::get_singleton()->get_resource_file_system();
	if (!efs) {
		return "File system not available.";
	}

	String result = "Project files (res://):\n";

	EditorFileSystemDirectory *root = efs->get_filesystem();
	if (!root) {
		return result + "  (empty)\n";
	}

	for (int i = 0; i < root->get_subdir_count(); i++) {
		EditorFileSystemDirectory *subdir = root->get_subdir(i);
		result += "  [dir] " + subdir->get_name() + "/\n";
	}

	for (int i = 0; i < root->get_file_count(); i++) {
		result += "  " + root->get_file(i) + "\n";
	}

	return result;
#else
	return "Project structure not available outside editor.";
#endif
}

String AIContextCollector::get_current_script_info() const {
#ifdef TOOLS_ENABLED
	ScriptEditor *se = ScriptEditor::get_singleton();
	if (!se) {
		return "No script editor available.";
	}

	Vector<Ref<Script>> scripts = se->get_open_scripts();
	if (scripts.is_empty()) {
		return "No scripts currently open.";
	}

	String result = "Open scripts:\n";
	for (int i = 0; i < scripts.size(); i++) {
		Ref<Script> scr = scripts[i];
		if (scr.is_null()) {
			continue;
		}
		String path = scr->get_path();
		result += "- " + (path.is_empty() ? "(unsaved)" : path) + " (" + scr->get_class() + ")\n";

		if (i == 0) {
			String source = scr->get_source_code();
			if (!source.is_empty()) {
				Vector<String> lines = source.split("\n");
				int max_lines = MIN(lines.size(), 50);
				result += "  Source (first " + itos(max_lines) + " lines):\n";
				for (int j = 0; j < max_lines; j++) {
					result += "    " + itos(j + 1) + ": " + lines[j] + "\n";
				}
				if (lines.size() > max_lines) {
					result += "    ... (" + itos(lines.size() - max_lines) + " more lines)\n";
				}
			}
		}
	}
	return result;
#else
	return "Script info not available outside editor.";
#endif
}

// N4: Detailed node dump for object attachment.
String AIContextCollector::get_detailed_node_dump(Node *p_node) const {
	if (!p_node) {
		return "Node is null.";
	}

	String result = "Attached Node: " + p_node->get_name() + " (" + p_node->get_class() + ")\n";
	result += "  Path: " + String(p_node->get_path()) + "\n";
	result += "  Child count: " + itos(p_node->get_child_count()) + "\n";

	// Script info.
	Ref<Script> scr = p_node->get_script();
	if (scr.is_valid()) {
		result += "  Script: " + scr->get_path() + "\n";
		String source = scr->get_source_code();
		if (!source.is_empty()) {
			Vector<String> lines = source.split("\n");
			int max_lines = MIN(lines.size(), 30);
			result += "  Script source (first " + itos(max_lines) + " lines):\n";
			for (int j = 0; j < max_lines; j++) {
				result += "    " + lines[j] + "\n";
			}
		}
	}

	// All editor-visible properties.
	result += "  Properties:\n";
	List<PropertyInfo> props;
	p_node->get_property_list(&props);
	int count = 0;
	for (const PropertyInfo &pi : props) {
		if (!(pi.usage & PROPERTY_USAGE_EDITOR)) {
			continue;
		}
		if (pi.name.begins_with("metadata/")) {
			continue;
		}
		if (count >= 50) {
			result += "    ... (truncated)\n";
			break;
		}
		Variant val = p_node->get(pi.name);
		String val_str = String(val);
		if (val_str.length() > 100) {
			val_str = val_str.left(100) + "...";
		}
		result += "    " + pi.name + " = " + val_str + "\n";
		count++;
	}

	// Children summary.
	if (p_node->get_child_count() > 0) {
		result += "  Children:\n";
		for (int i = 0; i < p_node->get_child_count() && i < 20; i++) {
			Node *child = p_node->get_child(i);
			result += "    - " + child->get_name() + " (" + child->get_class() + ")\n";
		}
	}

	return result;
}

// N4: Resource description for attachment.
String AIContextCollector::get_resource_description(const String &p_path) const {
#ifdef TOOLS_ENABLED
	if (!ResourceLoader::exists(p_path)) {
		return "Resource not found: " + p_path;
	}

	Ref<Resource> res = ResourceLoader::load(p_path);
	if (res.is_null()) {
		return "Failed to load resource: " + p_path;
	}

	String result = "Attached Resource: " + p_path + "\n";
	result += "  Type: " + res->get_class() + "\n";

	// If it's a script, show source.
	Ref<Script> scr = res;
	if (scr.is_valid()) {
		String source = scr->get_source_code();
		if (!source.is_empty()) {
			Vector<String> lines = source.split("\n");
			int max_lines = MIN(lines.size(), 60);
			result += "  Source (" + itos(max_lines) + " lines):\n";
			for (int j = 0; j < max_lines; j++) {
				result += "    " + itos(j + 1) + ": " + lines[j] + "\n";
			}
			if (lines.size() > max_lines) {
				result += "    ... (" + itos(lines.size() - max_lines) + " more lines)\n";
			}
		}
		return result;
	}

	// General properties.
	List<PropertyInfo> props;
	res->get_property_list(&props);
	int count = 0;
	for (const PropertyInfo &pi : props) {
		if (!(pi.usage & PROPERTY_USAGE_EDITOR)) {
			continue;
		}
		if (count >= 30) {
			break;
		}
		Variant val = res->get(pi.name);
		String val_str = String(val);
		if (val_str.length() > 80) {
			val_str = val_str.left(80) + "...";
		}
		result += "  " + pi.name + " = " + val_str + "\n";
		count++;
	}

	return result;
#else
	return "Resource info not available outside editor.";
#endif
}

String AIContextCollector::build_context_prompt() const {
	String context;
	context += "\n--- CURRENT EDITOR CONTEXT ---\n";
	context += get_scene_tree_description();
	context += "\n";
	context += get_selected_nodes_info();
	context += "\n";
	context += get_current_script_info();
	context += "\n";
	context += get_project_structure();
	context += "--- END CONTEXT ---\n";
	return context;
}

AIContextCollector::AIContextCollector() {
}
