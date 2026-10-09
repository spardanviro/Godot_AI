#ifdef TOOLS_ENABLED

#include "ai_checkpoint_manager.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/node.h"

static const char *CHECKPOINT_ROOT = "user://ai_checkpoints";

// Files a generated script realistically writes: source, scenes, resources and
// config. Binary assets are left out to keep a snapshot cheap enough to take
// before every execution.
static bool _is_snapshot_candidate(const String &p_file) {
	static const char *extensions[] = {
		"gd", "tscn", "tres", "godot", "cfg", "gdshader", "gdshaderinc", "json",
		"md", "txt", "csv", "cs", "import", "yaml", "yml", "toml", "ini", "xml",
		"uid", "translation", "po", nullptr
	};
	const String ext = p_file.get_extension().to_lower();
	for (int i = 0; extensions[i]; i++) {
		if (ext == extensions[i]) {
			return true;
		}
	}
	return false;
}

static void _list_project_files(const String &p_dir, Vector<String> &r_files) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	const PackedStringArray files = da->get_files();
	for (int i = 0; i < files.size(); i++) {
		r_files.push_back(p_dir.path_join(files[i]));
	}
	const PackedStringArray dirs = da->get_directories();
	for (int i = 0; i < dirs.size(); i++) {
		if (dirs[i].begins_with(".")) {
			continue; // .godot (import cache), .git, ...
		}
		_list_project_files(p_dir.path_join(dirs[i]), r_files);
	}
}

static String _backup_path(const String &p_backup_dir, const String &p_res_path) {
	return p_backup_dir.path_join(p_res_path.trim_prefix("res://"));
}

static bool _same_contents(const String &p_a, const String &p_b) {
	return FileAccess::get_file_as_bytes(p_a) == FileAccess::get_file_as_bytes(p_b);
}

static void _remove_dir_recursive(const String &p_dir) {
	Ref<DirAccess> da = DirAccess::open(p_dir);
	if (da.is_null()) {
		return;
	}
	const PackedStringArray files = da->get_files();
	for (int i = 0; i < files.size(); i++) {
		DirAccess::remove_absolute(p_dir.path_join(files[i]));
	}
	const PackedStringArray dirs = da->get_directories();
	for (int i = 0; i < dirs.size(); i++) {
		_remove_dir_recursive(p_dir.path_join(dirs[i]));
	}
	DirAccess::remove_absolute(p_dir);
}

void AICheckpointManager::_bind_methods() {
	ClassDB::bind_method(D_METHOD("begin_file_snapshot"), &AICheckpointManager::begin_file_snapshot);
	ClassDB::bind_method(D_METHOD("create_files_checkpoint", "description"), &AICheckpointManager::create_files_checkpoint);
	ClassDB::bind_method(D_METHOD("restore_checkpoint", "id"), &AICheckpointManager::restore_checkpoint);
	ClassDB::bind_method(D_METHOD("get_checkpoint_count"), &AICheckpointManager::get_checkpoint_count);
}

void AICheckpointManager::_discard_file_snapshot(FileSnapshot &p_snapshot) {
	if (p_snapshot.valid && !p_snapshot.backup_dir.is_empty()) {
		_remove_dir_recursive(p_snapshot.backup_dir);
	}
	p_snapshot = FileSnapshot();
}

void AICheckpointManager::begin_file_snapshot() {
	_discard_file_snapshot(pending_files);

	const int MAX_FILES = 4000;
	const uint64_t MAX_FILE_BYTES = 512 * 1024;
	const uint64_t MAX_TOTAL_BYTES = 64 * 1024 * 1024;

	FileSnapshot snap;
	snap.valid = true;
	snap.backup_dir = String(CHECKPOINT_ROOT).path_join(
			"files_" + itos(OS::get_singleton()->get_process_id()) + "_" + itos(next_id) + "_" + itos(Time::get_singleton()->get_ticks_msec()));

	Vector<String> all_files;
	_list_project_files("res://", all_files);

	uint64_t total_bytes = 0;
	for (int i = 0; i < all_files.size(); i++) {
		const String &path = all_files[i];
		snap.existing.insert(path);
		if (!_is_snapshot_candidate(path)) {
			continue;
		}
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
		if (f.is_null()) {
			continue;
		}
		const uint64_t size = f->get_length();
		f.unref();
		if (size > MAX_FILE_BYTES || snap.backed_up.size() >= MAX_FILES || total_bytes + size > MAX_TOTAL_BYTES) {
			snap.complete = false;
			continue;
		}
		const String dest = _backup_path(snap.backup_dir, path);
		DirAccess::make_dir_recursive_absolute(dest.get_base_dir());
		if (DirAccess::copy_absolute(path, dest) == OK) {
			snap.backed_up.push_back(path);
			total_bytes += size;
		} else {
			snap.complete = false;
		}
	}
	pending_files = snap;
}

void AICheckpointManager::_restore_files(const FileSnapshot &p_snapshot) {
	if (!p_snapshot.valid) {
		return;
	}
	last_report.files_partial = !p_snapshot.complete;

	// Put back every file that was changed or deleted since the snapshot.
	for (int i = 0; i < p_snapshot.backed_up.size(); i++) {
		const String &path = p_snapshot.backed_up[i];
		const String backup = _backup_path(p_snapshot.backup_dir, path);
		if (!FileAccess::exists(backup)) {
			continue;
		}
		if (FileAccess::exists(path) && _same_contents(path, backup)) {
			continue;
		}
		DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		if (DirAccess::copy_absolute(backup, path) == OK) {
			last_report.files_restored++;
		}
	}

	// Files that did not exist then were created since. They go to the system
	// trash rather than being deleted, in case one of them is the user's own.
	Vector<String> current_files;
	_list_project_files("res://", current_files);
	for (int i = 0; i < current_files.size(); i++) {
		if (p_snapshot.existing.has(current_files[i])) {
			continue;
		}
		const String global = ProjectSettings::get_singleton()->globalize_path(current_files[i]);
		if (OS::get_singleton()->move_to_trash(global) != OK) {
			DirAccess::remove_absolute(current_files[i]);
		}
		last_report.files_removed++;
	}
}

void AICheckpointManager::_push_checkpoint(Checkpoint &p_cp) {
	p_cp.timestamp = Time::get_singleton()->get_ticks_msec();
	p_cp.id = next_id++;
	p_cp.files = pending_files; // Take ownership of the pre-execution file snapshot.
	pending_files = FileSnapshot();

	checkpoints.push_back(p_cp);
	while (checkpoints.size() > max_checkpoints) {
		_remove_checkpoint(0);
	}
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

	// PackedScene::pack() stores built-in sub-resources (materials, shapes,
	// meshes) by reference, so editing one in place after this point would edit
	// the checkpoint too. Writing it out now freezes their current values.
	DirAccess::make_dir_recursive_absolute(CHECKPOINT_ROOT);
	const String snapshot_file = String(CHECKPOINT_ROOT).path_join(
			"cp_" + itos(OS::get_singleton()->get_process_id()) + "_" + itos(next_id) + ".tscn");
	if (ResourceSaver::save(packed, snapshot_file) == OK) {
		cp.snapshot_file = snapshot_file;
	} else {
		WARN_PRINT("[Godot AI] Could not write checkpoint snapshot; in-place sub-resource edits will not be revertible for this checkpoint.");
	}

	_push_checkpoint(cp);
	print_line("[Godot AI] Checkpoint created: " + p_description + " (" + itos(checkpoints.size()) + " total)");
	return true;
}

int AICheckpointManager::create_scene_open_checkpoint(const String &p_scene_path, const String &p_description) {
	Checkpoint cp;
	cp.type = TYPE_SCENE_OPEN;
	cp.scene_path = p_scene_path;
	cp.description = p_description;
	_push_checkpoint(cp);

	const int new_id = get_latest_id();
	print_line("[Godot AI] Scene-open checkpoint created: " + p_description + " -> " + p_scene_path + " (id " + itos(new_id) + ")");
	return new_id;
}

int AICheckpointManager::create_files_checkpoint(const String &p_description) {
	if (!pending_files.valid) {
		return -1;
	}
	Checkpoint cp;
	cp.type = TYPE_FILES;
	cp.description = p_description;
	_push_checkpoint(cp);
	return get_latest_id();
}

bool AICheckpointManager::restore_latest() {
	if (checkpoints.is_empty()) {
		return false;
	}
	return restore_checkpoint(get_latest_id());
}

int AICheckpointManager::_find_index(int p_id) const {
	for (int i = 0; i < checkpoints.size(); i++) {
		if (checkpoints[i].id == p_id) {
			return i;
		}
	}
	return -1;
}

bool AICheckpointManager::restore_checkpoint(int p_id) {
	const int p_index = _find_index(p_id);
	if (p_index < 0) {
		print_line("[Godot AI] Checkpoint restore failed: checkpoint " + itos(p_id) + " no longer exists (evicted or already rolled back).");
		return false;
	}

	last_report = RestoreReport();
	const Checkpoint cp = checkpoints[p_index]; // Copy: the vector is truncated below.
	EditorInterface *ei = EditorInterface::get_singleton();

	// ── TYPE_SCENE_OPEN: close the scene that the code opened ──────────────
	if (cp.type == TYPE_SCENE_OPEN) {
		// Only close the scene this checkpoint recorded. If the user has switched
		// to another tab, closing "the current scene" would discard their work.
		Node *open_root = ei->get_edited_scene_root();
		const String open_path = open_root ? open_root->get_scene_file_path() : String();
		if (!open_root || open_path != cp.scene_path) {
			print_line("[Godot AI] Scene-open revert skipped: the scene it opened (" + cp.scene_path + ") is not the active tab.");
			return false;
		}
		print_line("[Godot AI] Reverting scene-open checkpoint: closing scene " + cp.scene_path);
		ei->close_scene();
		_restore_files(cp.files);

	} else if (cp.type == TYPE_FILES) {
		_restore_files(cp.files);

	} else {
		// ── TYPE_SCENE: restore packed scene state ──────────────────────────
		if (cp.scene_data.is_null()) {
			print_line("[Godot AI] Checkpoint restore failed: scene_data is null");
			return false;
		}

		// Files first, so the scene is reloaded against the restored scripts.
		_restore_files(cp.files);

		const bool was_unsaved = cp.scene_path.is_empty();
		// An unsaved scene has no file to reload, so it is restored through a
		// temporary one that is removed again once the scene is open.
		const String scene_path = was_unsaved ? String("res://_ai_checkpoint_restore.tscn") : cp.scene_path;

		Error err = ERR_FILE_NOT_FOUND;
		if (!cp.snapshot_file.is_empty() && FileAccess::exists(cp.snapshot_file)) {
			err = DirAccess::copy_absolute(cp.snapshot_file, scene_path);
		}
		if (err != OK) {
			err = ResourceSaver::save(cp.scene_data, scene_path);
		}
		if (err != OK) {
			print_line("[Godot AI] Checkpoint restore failed: could not write " + scene_path + " (error " + itos((int)err) + ")");
			return false;
		}

		Node *current_root = ei->get_edited_scene_root();
		const String current_path = current_root ? current_root->get_scene_file_path() : String();

		if (was_unsaved) {
			// Replace the unsaved tab the code modified with the restored copy.
			if (current_root && current_path.is_empty()) {
				ei->close_scene();
			}
			ei->open_scene_from_path(scene_path);
			Node *restored_root = ei->get_edited_scene_root();
			if (restored_root && restored_root->get_scene_file_path() == scene_path) {
				restored_root->set_scene_file_path(String()); // Back to "unsaved".
			}
			DirAccess::remove_absolute(scene_path);
		} else if (current_path == scene_path) {
			ei->reload_scene_from_path(scene_path);
		} else {
			ei->open_scene_from_path(scene_path);
		}
	}

	while (checkpoints.size() > p_index) {
		_remove_checkpoint(checkpoints.size() - 1);
	}

	if (last_report.files_restored > 0 || last_report.files_removed > 0) {
		if (EditorFileSystem *efs = ei->get_resource_filesystem()) {
			efs->scan();
		}
	}

	print_line("[Godot AI] Checkpoint restored: " + cp.description +
			" (files restored: " + itos(last_report.files_restored) + ", removed: " + itos(last_report.files_removed) + ")");
	return true;
}

String AICheckpointManager::get_checkpoint_description(int p_id) const {
	const int p_index = _find_index(p_id);
	if (p_index < 0) {
		return "";
	}
	return checkpoints[p_index].description;
}

void AICheckpointManager::_remove_checkpoint(int p_index) {
	const String snapshot_file = checkpoints[p_index].snapshot_file;
	if (!snapshot_file.is_empty() && FileAccess::exists(snapshot_file)) {
		DirAccess::remove_absolute(snapshot_file);
	}
	FileSnapshot files = checkpoints[p_index].files;
	_discard_file_snapshot(files);
	checkpoints.remove_at(p_index);
}

void AICheckpointManager::clear() {
	while (!checkpoints.is_empty()) {
		_remove_checkpoint(checkpoints.size() - 1);
	}
	_discard_file_snapshot(pending_files);
}

AICheckpointManager::AICheckpointManager() {
}

AICheckpointManager::~AICheckpointManager() {
	clear();
}

#endif // TOOLS_ENABLED
