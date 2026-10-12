#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

class AIContextCollector : public RefCounted {
	GDCLASS(AIContextCollector, RefCounted);

protected:
	static void _bind_methods();

	String _describe_node_recursive(class Node *p_node, int p_depth, int p_max_depth) const;

	// File state cache — avoids regenerating formatted script info when sources
	// haven't changed between API calls (inspired by Claude Code's readFileState).
	// Key: script resource path.  Value: last-seen source code hash + cached output.
	struct ScriptCacheEntry {
		uint32_t source_hash = 0;
		String   formatted;    // Cached output of the formatting pass.
	};
	mutable HashMap<String, ScriptCacheEntry> _script_cache;

	// Returns a cheap fingerprint of a string (djb2-style).
	static uint32_t _hash_string(const String &p_s);

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

	// Performance monitoring snapshot.
	String get_performance_snapshot() const;

	// Post-compaction file restoration: returns the source of the top N currently
	// open scripts so they can be re-injected into the context summary after
	// compression, preventing the AI from "forgetting" recently edited files.
	// max_scripts: how many scripts to include (default 3).
	// max_chars_each: char limit per script (default 4000 ≈ ~1000 tokens).
	String get_open_scripts_snapshot(int max_scripts = 3, int max_chars_each = 4000) const;

	AIContextCollector();
};
