#ifdef TOOLS_ENABLED

#include "ai_assistant_plugin.h"
#include "ai_assistant_panel.h"
#include "ai_error_monitor.h"

#include "core/debugger/debugger_marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/debugger/script_editor_debugger.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/scene_tree_dock.h"
#include "scene/gui/popup_menu.h"

void AIAssistantPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_hook_scene_tree_menu"), &AIAssistantPlugin::_hook_scene_tree_menu);
	ClassDB::bind_method(D_METHOD("_focus_dock"), &AIAssistantPlugin::_focus_dock);
	ClassDB::bind_method(D_METHOD("_connect_debugger_signal"), &AIAssistantPlugin::_connect_debugger_signal);
	ClassDB::bind_method(D_METHOD("_on_debugger_started"), &AIAssistantPlugin::_on_debugger_started);
}

void AIAssistantPlugin::_focus_dock() {
	if (dock) {
		EditorDockManager::get_singleton()->focus_dock(dock);
	}
}

// Called deferred once the editor is fully set up so SceneTreeDock::singleton
// is guaranteed to be valid.
void AIAssistantPlugin::_hook_scene_tree_menu() {
	// get_context_menu() was removed in Godot 4.7; scene tree menu hooking is disabled.
}

void AIAssistantPlugin::_on_scene_menu_about_to_popup() {
	// get_context_menu() was removed in Godot 4.7; no-op.
}

void AIAssistantPlugin::_on_scene_menu_id_pressed(int p_id) {
	if (p_id != AI_MENU_MENTION_ID) {
		return;
	}
	if (panel) {
		panel->insert_mention_of_selected_node();
	}
}

// Connect to the ScriptEditorDebugger's debug_data and started signals.
// - debug_data: intercept runtime errors from the game subprocess.
// - started: clear stale error buffers the moment the game connects, before
//   any _ready() errors can arrive (fixes the 200 ms poll race condition).
// This is deferred because EditorDebuggerNode initializes after the plugin.
void AIAssistantPlugin::_connect_debugger_signal() {
	EditorDebuggerNode *dnode = EditorDebuggerNode::get_singleton();
	if (!dnode) {
		return;
	}
	// get_default_debugger() is always the first (index 0) debugger — the one used
	// for normal in-editor play. We connect here; for multiple-debugger setups this
	// is sufficient since the default one receives all standard game errors.
	ScriptEditorDebugger *dbg = dnode->get_default_debugger();
	if (!dbg) {
		return;
	}
	Callable cb_data = callable_mp(this, &AIAssistantPlugin::_on_debug_data);
	if (!dbg->is_connected("debug_data", cb_data)) {
		dbg->connect("debug_data", cb_data);
	}
	Callable cb_started = callable_mp(this, &AIAssistantPlugin::_on_debugger_started);
	if (!dbg->is_connected("started", cb_started)) {
		dbg->connect("started", cb_started);
	}
}

// Fired when the game process connects (before any _ready() errors arrive).
// Clears stale error buffers so Watch auto-fix only reacts to this run.
void AIAssistantPlugin::_on_debugger_started() {
	if (panel) {
		panel->on_game_session_started();
	}
}

// Receives every raw debugger message. We only care about "error" messages here.
// When an "error" arrives, we deserialize it and forward to AIErrorMonitor so the
// Watch auto-fix feature (_trigger_runtime_fix) can detect and act on it.
void AIAssistantPlugin::_on_debug_data(const String &p_msg, const Array &p_data) {
	if (p_msg != "error") {
		return;
	}
	if (!AIErrorMonitor::get_singleton()) {
		return;
	}

	DebuggerMarshalls::OutputError oe;
	if (!oe.deserialize(p_data)) {
		return;
	}

	// Build a human-readable error title — mirrors ScriptEditorDebugger::_msg_error logic.
	bool source_is_project_file = oe.source_file.begins_with("res://");
	String msg;
	if (!oe.source_func.is_empty() && source_is_project_file) {
		msg = oe.source_func + ": ";
	} else if (!oe.callstack.is_empty()) {
		const ScriptLanguage::StackInfo &frame = oe.callstack[0];
		String frame_txt = frame.file.get_file() + ":" + itos(frame.line) + " @ " + frame.func;
		if (!frame_txt.ends_with(")")) {
			frame_txt += "()";
		}
		msg = frame_txt + ": ";
	} else if (!oe.source_func.is_empty()) {
		msg = oe.source_func + ": ";
	}
	msg += oe.error_descr.is_empty() ? oe.error : oe.error_descr;

	// Extract the best res:// file+line from the callstack, falling back to source_file.
	String res_file;
	int res_line = oe.source_line;
	for (int i = 0; i < oe.callstack.size(); i++) {
		if (oe.callstack[i].file.begins_with("res://")) {
			res_file = oe.callstack[i].file;
			res_line = oe.callstack[i].line;
			break;
		}
	}
	if (res_file.is_empty() && oe.source_file.begins_with("res://")) {
		res_file = oe.source_file;
	}

	AIErrorMonitor::receive_debugger_error(msg, res_file, res_line, oe.warning);
}

AIAssistantPlugin::AIAssistantPlugin() {
	panel = memnew(AIAssistantPanel);

	// Build our own EditorDock wrapper so we can call focus_dock() on it.
	dock = memnew(EditorDock);
	dock->set_title("Godot AI");
	dock->set_default_slot(EditorDock::DOCK_SLOT_RIGHT_UL);
	dock->add_child(panel);
	add_dock(dock);

	// Defer focusing so the editor layout finishes loading first.
	call_deferred("_focus_dock");

	// Defer the scene-tree hook until the editor is fully initialised.
	call_deferred("_hook_scene_tree_menu");

	// Defer debugger signal connection — EditorDebuggerNode may not exist yet.
	call_deferred("_connect_debugger_signal");
}

AIAssistantPlugin::~AIAssistantPlugin() {
	if (dock) {
		remove_dock(dock);
		memdelete(dock);
		dock = nullptr;
	}
}

#endif // TOOLS_ENABLED
