#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "scene/resources/packed_scene.h"

class AICheckpointManager : public RefCounted {
	GDCLASS(AICheckpointManager, RefCounted);

public:
	struct Checkpoint {
		Ref<PackedScene> scene_data;
		String scene_path; // File path of the edited scene.
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
	bool create_checkpoint(const String &p_description);

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
