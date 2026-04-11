#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "scene/resources/packed_scene.h"

class AICheckpointManager : public RefCounted {
	GDCLASS(AICheckpointManager, RefCounted);

public:
	enum CheckpointType {
		TYPE_SCENE,      // Normal: packed scene content saved before execution.
		TYPE_SCENE_OPEN, // No-scene-before: records which scene was opened by the code.
	};

	struct Checkpoint {
		CheckpointType type = TYPE_SCENE;
		Ref<PackedScene> scene_data;
		String scene_path;   // For TYPE_SCENE: file path of the edited scene.
		                     // For TYPE_SCENE_OPEN: path of the scene that was just created/opened.
		String description;
		uint64_t timestamp = 0;
	};

private:
	Vector<Checkpoint> checkpoints;
	int max_checkpoints = 10;

protected:
	static void _bind_methods();

public:
	// Create a checkpoint of the current scene state.
	// Returns false when no scene is open (nothing to snapshot).
	bool create_checkpoint(const String &p_description);

	// Create a "scene-open" checkpoint: records that p_scene_path was opened by
	// the code execution so that reverting can close + delete the scene.
	// Always succeeds. Returns the index of the new checkpoint.
	int create_scene_open_checkpoint(const String &p_scene_path, const String &p_description);

	// Restore the most recent checkpoint.
	bool restore_latest();

	// Restore a specific checkpoint by index.
	bool restore_checkpoint(int p_index);

	// Get the number of stored checkpoints.
	int get_checkpoint_count() const { return checkpoints.size(); }

	// Get description of a checkpoint.
	String get_checkpoint_description(int p_index) const;

	// Clear all checkpoints.
	void clear();

	AICheckpointManager();
};

#endif // TOOLS_ENABLED
