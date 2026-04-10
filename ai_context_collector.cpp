#include "ai_context_collector.h"

#include "core/object/class_db.h"
#include "scene/main/node.h"
#include "core/io/resource_loader.h"
#include "main/performance.h"
#include "servers/rendering/rendering_server.h"

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

static void _collect_dir_recursive(EditorFileSystemDirectory *p_dir, const String &p_indent, String &r_result, int p_depth) {
	// Cap depth to avoid massive output for deeply nested projects.
	if (p_depth > 6) {
		r_result += p_indent + "  ...\n";
		return;
	}

	// Subdirectories first.
	for (int i = 0; i < p_dir->get_subdir_count(); i++) {
		EditorFileSystemDirectory *sub = p_dir->get_subdir(i);
		r_result += p_indent + "[" + sub->get_name() + "/]\n";
		_collect_dir_recursive(sub, p_indent + "  ", r_result, p_depth + 1);
	}

	// Then files.
	for (int i = 0; i < p_dir->get_file_count(); i++) {
		r_result += p_indent + p_dir->get_file(i) + "\n";
	}
}

String AIContextCollector::get_project_structure() const {
#ifdef TOOLS_ENABLED
	EditorFileSystem *efs = EditorInterface::get_singleton()->get_resource_filesystem();
	if (!efs) {
		return "File system not available.";
	}

	String result = "Project files (res://):\n";

	EditorFileSystemDirectory *root = efs->get_filesystem();
	if (!root) {
		return result + "  (empty)\n";
	}

	_collect_dir_recursive(root, "  ", result, 0);

	return result;
#else
	return "Project structure not available outside editor.";
#endif
}

// djb2-style hash for cheap content fingerprinting.
uint32_t AIContextCollector::_hash_string(const String &p_s) {
	uint32_t h = 5381;
	int len = p_s.length();
	for (int i = 0; i < len; i++) {
		h = ((h << 5) + h) + (uint32_t)p_s[i];
	}
	return h;
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
				String cache_key = path.is_empty() ? "(unsaved)" : path;
				uint32_t new_hash = _hash_string(source);

				// Check cache: if the source hasn't changed, reuse the formatted output.
				if (_script_cache.has(cache_key)) {
					const ScriptCacheEntry &entry = _script_cache[cache_key];
					if (entry.source_hash == new_hash) {
						result += entry.formatted;
						continue;
					}
				}

				// Cache miss — format and store.
				Vector<String> lines = source.split("\n");
				int max_lines = MIN(lines.size(), 50);
				String formatted = "  Source (first " + itos(max_lines) + " lines):\n";
				for (int j = 0; j < max_lines; j++) {
					formatted += "    " + itos(j + 1) + ": " + lines[j] + "\n";
				}
				if (lines.size() > max_lines) {
					formatted += "    ... (" + itos(lines.size() - max_lines) + " more lines)\n";
				}

				ScriptCacheEntry &entry = _script_cache[cache_key];
				entry.source_hash = new_hash;
				entry.formatted    = formatted;
				result += formatted;
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

String AIContextCollector::get_performance_snapshot() const {
	Performance *perf = Performance::get_singleton();
	if (!perf) {
		return "";
	}

	String result = "Performance Snapshot:\n";
	result += "  FPS: " + itos((int)perf->get_monitor(Performance::TIME_FPS)) + "\n";
	result += "  Process Time: " + String::num(perf->get_monitor(Performance::TIME_PROCESS) * 1000.0, 2) + " ms\n";
	result += "  Physics Time: " + String::num(perf->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0, 2) + " ms\n";
	result += "  Static Memory: " + String::humanize_size((uint64_t)perf->get_monitor(Performance::MEMORY_STATIC)) + "\n";
	result += "  Object Count: " + itos((int)perf->get_monitor(Performance::OBJECT_COUNT)) + "\n";
	result += "  Resource Count: " + itos((int)perf->get_monitor(Performance::OBJECT_RESOURCE_COUNT)) + "\n";
	result += "  Node Count: " + itos((int)perf->get_monitor(Performance::OBJECT_NODE_COUNT)) + "\n";
	result += "  Orphan Nodes: " + itos((int)perf->get_monitor(Performance::OBJECT_ORPHAN_NODE_COUNT)) + "\n";
	return result;
}

String AIContextCollector::get_open_scripts_snapshot(int max_scripts, int max_chars_each) const {
#ifdef TOOLS_ENABLED
	ScriptEditor *se = ScriptEditor::get_singleton();
	if (!se) {
		return "";
	}
	Vector<Ref<Script>> scripts = se->get_open_scripts();
	if (scripts.is_empty()) {
		return "";
	}

	String result = "\n## Recently Open Scripts (restored after context compression)\n";
	int included = 0;
	for (int i = 0; i < scripts.size() && included < max_scripts; i++) {
		Ref<Script> scr = scripts[i];
		if (scr.is_null()) {
			continue;
		}
		String source = scr->get_source_code();
		if (source.is_empty()) {
			continue;
		}
		String path = scr->get_path();
		if (path.is_empty()) {
			path = "(unsaved)";
		}

		// Use file-state cache to avoid re-formatting unchanged scripts.
		String snapshot_key = "snapshot:" + path;
		uint32_t new_hash = _hash_string(source);
		if (_script_cache.has(snapshot_key)) {
			const ScriptCacheEntry &entry = _script_cache[snapshot_key];
			if (entry.source_hash == new_hash) {
				result += entry.formatted;
				included++;
				continue;
			}
		}

		String block = "\n### " + path + "\n```gdscript\n";
		if (source.length() > max_chars_each) {
			block += source.left(max_chars_each);
			block += "\n# ... [truncated — " + itos(source.length() - max_chars_each) + " more chars]\n";
		} else {
			block += source;
		}
		block += "\n```\n";

		ScriptCacheEntry &entry = _script_cache[snapshot_key];
		entry.source_hash = new_hash;
		entry.formatted    = block;

		result += block;
		included++;
	}

	if (included == 0) {
		return "";
	}
	return result;
#else
	return "";
#endif
}

AIContextCollector::AIContextCollector() {
}
