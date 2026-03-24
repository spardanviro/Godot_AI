#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class AIContextCollector : public RefCounted {
	GDCLASS(AIContextCollector, RefCounted);

protected:
	static void _bind_methods();

	String _describe_node_recursive(class Node *p_node, int p_depth, int p_max_depth) const;

public:
	String get_scene_tree_description() const;
	String get_selected_nodes_info() const;
	String get_project_structure() const;
	String get_current_script_info() const;
	String build_context_prompt() const;

	// N4: Detailed node dump for object attachment.
	String get_detailed_node_dump(class Node *p_node) const;

	// N4: Resource description for attachment.
	String get_resource_description(const String &p_path) const;

	AIContextCollector();
};
