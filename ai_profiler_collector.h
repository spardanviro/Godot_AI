#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

// Collects performance data from Godot's Performance singleton.
class AIProfilerCollector : public RefCounted {
	GDCLASS(AIProfilerCollector, RefCounted);

protected:
	static void _bind_methods();

public:
	// Collect a snapshot of current performance metrics.
	String collect_performance_snapshot() const;

	AIProfilerCollector();
};

#endif // TOOLS_ENABLED
