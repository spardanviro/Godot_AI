#ifdef TOOLS_ENABLED

#include "ai_profiler_collector.h"
#include "core/object/class_db.h"
#include "main/performance.h"

void AIProfilerCollector::_bind_methods() {
}

String AIProfilerCollector::collect_performance_snapshot() const {
	Performance *perf = Performance::get_singleton();
	if (!perf) {
		return "Performance singleton not available.";
	}

	String result = "Performance Snapshot:\n";

	// Time metrics.
	result += "  FPS: " + String::num(perf->get_monitor(Performance::TIME_FPS), 1) + "\n";
	result += "  Process Time: " + String::num(perf->get_monitor(Performance::TIME_PROCESS) * 1000.0, 2) + " ms\n";
	result += "  Physics Time: " + String::num(perf->get_monitor(Performance::TIME_PHYSICS_PROCESS) * 1000.0, 2) + " ms\n";
	result += "  Navigation Time: " + String::num(perf->get_monitor(Performance::TIME_NAVIGATION_PROCESS) * 1000.0, 2) + " ms\n";

	// Memory.
	result += "  Static Memory: " + String::num(perf->get_monitor(Performance::MEMORY_STATIC) / 1048576.0, 2) + " MB\n";
	result += "  Static Max: " + String::num(perf->get_monitor(Performance::MEMORY_STATIC_MAX) / 1048576.0, 2) + " MB\n";
	result += "  Message Buffer Max: " + String::num(perf->get_monitor(Performance::MEMORY_MESSAGE_BUFFER_MAX) / 1048576.0, 2) + " MB\n";

	// Objects.
	result += "  Object Count: " + itos((int)perf->get_monitor(Performance::OBJECT_COUNT)) + "\n";
	result += "  Resource Count: " + itos((int)perf->get_monitor(Performance::OBJECT_RESOURCE_COUNT)) + "\n";
	result += "  Node Count: " + itos((int)perf->get_monitor(Performance::OBJECT_NODE_COUNT)) + "\n";
	result += "  Orphan Nodes: " + itos((int)perf->get_monitor(Performance::OBJECT_ORPHAN_NODE_COUNT)) + "\n";

	// Rendering.
	result += "  Draw Calls: " + itos((int)perf->get_monitor(Performance::RENDER_TOTAL_DRAW_CALLS_IN_FRAME)) + "\n";
	result += "  Total Objects: " + itos((int)perf->get_monitor(Performance::RENDER_TOTAL_OBJECTS_IN_FRAME)) + "\n";
	result += "  Total Primitives: " + itos((int)perf->get_monitor(Performance::RENDER_TOTAL_PRIMITIVES_IN_FRAME)) + "\n";
	result += "  Video Memory: " + String::num(perf->get_monitor(Performance::RENDER_VIDEO_MEM_USED) / 1048576.0, 2) + " MB\n";
	result += "  Texture Memory: " + String::num(perf->get_monitor(Performance::RENDER_TEXTURE_MEM_USED) / 1048576.0, 2) + " MB\n";
	result += "  Buffer Memory: " + String::num(perf->get_monitor(Performance::RENDER_BUFFER_MEM_USED) / 1048576.0, 2) + " MB\n";

	// Physics.
	result += "  Active 2D Objects: " + itos((int)perf->get_monitor(Performance::PHYSICS_2D_ACTIVE_OBJECTS)) + "\n";
	result += "  Active 3D Objects: " + itos((int)perf->get_monitor(Performance::PHYSICS_3D_ACTIVE_OBJECTS)) + "\n";

	// Navigation.
	result += "  Navigation Maps: " + itos((int)perf->get_monitor(Performance::NAVIGATION_ACTIVE_MAPS)) + "\n";
	result += "  Navigation Agents: " + itos((int)perf->get_monitor(Performance::NAVIGATION_AGENT_COUNT)) + "\n";

	return result;
}

AIProfilerCollector::AIProfilerCollector() {
}

#endif // TOOLS_ENABLED
