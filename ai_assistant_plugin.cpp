#ifdef TOOLS_ENABLED

#include "ai_assistant_plugin.h"
#include "ai_assistant_panel.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/scene_tree_dock.h"
#include "scene/gui/popup_menu.h"

void AIAssistantPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_hook_scene_tree_menu"), &AIAssistantPlugin::_hook_scene_tree_menu);
	ClassDB::bind_method(D_METHOD("_focus_dock"), &AIAssistantPlugin::_focus_dock);
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
}

AIAssistantPlugin::~AIAssistantPlugin() {
	if (dock) {
		remove_dock(dock);
		memdelete(dock);
		dock = nullptr;
	}
}

#endif // TOOLS_ENABLED
