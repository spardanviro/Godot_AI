#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"
#include "scene/resources/packed_scene.h"

class AICheckpointManager : public RefCounted {
	GDCLASS(AICheckpointManager, RefCounted);

public:
	enum CheckpointType {
		TYPE_SCENE,      // Normal: packed scene content saved before execution.
		TYPE_SCENE_OPEN, // No-scene-before: records which scene was opened by the code.
		TYPE_FILES,      // No scene involved: only the project files are rolled back.
	};

	// Copy of the project's text files taken right before code runs, so a revert
	// can also undo what the code wrote, changed or deleted on disk.
	struct FileSnapshot {
		String backup_dir;            // user:// directory holding the copies.
		Vector<String> backed_up;     // res:// paths that were copied.
		HashSet<String> existing;     // Every res:// file that existed at snapshot time.
		bool complete = true;         // False if the size/count caps cut the copy short.
		bool valid = false;
	};

	// What a restore did, for the message shown to the user.
	struct RestoreReport {
		int files_restored = 0; // Rewritten from the snapshot (changed or deleted since).
		int files_removed = 0;  // Created since the snapshot; moved to the system trash.
		bool files_partial = false;
	};

	struct Checkpoint {
		CheckpointType type = TYPE_SCENE;
		Ref<PackedScene> scene_data;
		String scene_path;   // For TYPE_SCENE: file path of the edited scene.
		                     // For TYPE_SCENE_OPEN: path of the scene that was just created/opened.
		String description;
		uint64_t timestamp = 0;
		int id = -1; // Stable handle; unlike a vector index it survives eviction and rollback.
		String snapshot_file; // user:// copy written at checkpoint time (TYPE_SCENE only).
		FileSnapshot files;
	};

private:
	Vector<Checkpoint> checkpoints;
	int max_checkpoints = 10;
	int next_id = 0;
	FileSnapshot pending_files; // Taken by begin_file_snapshot(), adopted by the next checkpoint.
	RestoreReport last_report;

	int _find_index(int p_id) const;
	// Removes a checkpoint together with its snapshot files.
	void _remove_checkpoint(int p_index);
	void _push_checkpoint(Checkpoint &p_cp);
	static void _discard_file_snapshot(FileSnapshot &p_snapshot);
	void _restore_files(const FileSnapshot &p_snapshot);

protected:
	static void _bind_methods();

public:
	// Snapshot the project's text files. Call right before executing code; the
	// next checkpoint created (of any type) takes ownership of the snapshot.
	void begin_file_snapshot();

	// Create a checkpoint of the current scene state.
	// Returns false when no scene is open (nothing to snapshot).
	bool create_checkpoint(const String &p_description);

	// Create a "scene-open" checkpoint: records that p_scene_path was opened by
	// the code execution so that reverting can close the scene.
	// Always succeeds. Returns the id of the new checkpoint.
	int create_scene_open_checkpoint(const String &p_scene_path, const String &p_description);

	// Create a checkpoint that only carries the pending file snapshot.
	// Returns its id, or -1 if there is no pending snapshot.
	int create_files_checkpoint(const String &p_description);

	// Restore the most recent checkpoint.
	bool restore_latest();

	// Restore a specific checkpoint by id. Fails if it has been evicted or rolled past.
	bool restore_checkpoint(int p_id);

	const RestoreReport &get_last_restore_report() const { return last_report; }

	// Get the number of stored checkpoints.
	int get_checkpoint_count() const { return checkpoints.size(); }

	// Id of the most recently created checkpoint, or -1 if there is none.
	int get_latest_id() const { return checkpoints.is_empty() ? -1 : checkpoints[checkpoints.size() - 1].id; }

	// Get description of a checkpoint.
	String get_checkpoint_description(int p_id) const;

	// Clear all checkpoints.
	void clear();

	AICheckpointManager();
	~AICheckpointManager();
};

#endif // TOOLS_ENABLED
