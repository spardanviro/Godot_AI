#ifdef TOOLS_ENABLED

#include "ai_checkpoint_manager.h"
#include "core/io/resource_saver.h"
#include "core/os/time.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "scene/main/node.h"

void AICheckpointManager::_bind_methods() {
}

bool AICheckpointManager::create_checkpoint(const String &p_description) {
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!scene_root) {
		return false;
	}

	Ref<PackedScene> packed;
	packed.instantiate();
	Error err = packed->pack(scene_root);
	if (err != OK) {
		ERR_PRINT("[Godot AI] Failed to pack scene for checkpoint.");
		return false;
	}

	Checkpoint cp;
	cp.scene_data = packed;
	cp.scene_path = scene_root->get_scene_file_path();
	cp.description = p_description;
	cp.timestamp = Time::get_singleton()->get_ticks_msec();

	checkpoints.push_back(cp);

	// Limit checkpoint count.
	while (checkpoints.size() > max_checkpoints) {
		checkpoints.remove_at(0);
	}

	print_line("[Godot AI] Checkpoint created: " + p_description + " (" + itos(checkpoints.size()) + " total)");
	return true;
}

bool AICheckpointManager::restore_latest() {
	if (checkpoints.is_empty()) {
		return false;
	}
	return restore_checkpoint(checkpoints.size() - 1);
}

bool AICheckpointManager::restore_checkpoint(int p_index) {
	if (p_index < 0 || p_index >= checkpoints.size()) {
		print_line("[Godot AI] Checkpoint restore failed: invalid index " + itos(p_index) + " (total: " + itos(checkpoints.size()) + ")");
		return false;
	}

	const Checkpoint &cp = checkpoints[p_index];
	if (cp.scene_data.is_null()) {
		print_line("[Godot AI] Checkpoint restore failed: scene_data is null");
		return false;
	}

	// Save the checkpoint PackedScene to a file, then reload via editor API.
	String scene_path = cp.scene_path;
	bool is_temp = scene_path.is_empty();
	if (is_temp) {
		scene_path = "res://_ai_checkpoint_restore.tscn";
	}

	print_line("[Godot AI] Restoring checkpoint: scene_path=" + scene_path + " is_temp=" + String(is_temp ? "true" : "false"));

	// Save the packed scene to disk.
	Error err = ResourceSaver::save(cp.scene_data, scene_path);
	if (err != OK) {
		print_line("[Godot AI] Checkpoint restore failed: ResourceSaver error " + itos((int)err) + " for " + scene_path);
		return false;
	}

	// Try reload first (works when the scene tab is still open at the same path).
	// If that doesn't work (e.g., add_root_node changed the scene), fall back to open.
	Node *current_root = EditorInterface::get_singleton()->get_edited_scene_root();
	String current_path = current_root ? current_root->get_scene_file_path() : "";

	if (!is_temp && current_path == scene_path) {
		// Same scene tab is still open — just reload from disk.
		EditorInterface::get_singleton()->reload_scene_from_path(scene_path);
	} else {
		// Scene tab changed (add_root_node was used) or unsaved scene — open from file.
		EditorInterface::get_singleton()->open_scene_from_path(scene_path);
	}

	// Store description before removing checkpoints.
	String desc = cp.description;
	while (checkpoints.size() > p_index) {
		checkpoints.remove_at(checkpoints.size() - 1);
	}

	print_line("[Godot AI] Checkpoint restored: " + desc);
	return true;
}

String AICheckpointManager::get_checkpoint_description(int p_index) const {
	if (p_index < 0 || p_index >= checkpoints.size()) {
		return "";
	}
	return checkpoints[p_index].description;
}

void AICheckpointManager::clear() {
	checkpoints.clear();
}

AICheckpointManager::AICheckpointManager() {
}

#endif // TOOLS_ENABLED
