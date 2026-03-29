#ifdef TOOLS_ENABLED

#include "ai_assistant_plugin.h"
#include "ai_assistant_panel.h"

#include "editor/docks/scene_tree_dock.h"
#include "scene/gui/popup_menu.h"

void AIAssistantPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_hook_scene_tree_menu"), &AIAssistantPlugin::_hook_scene_tree_menu);
}

// Called deferred once the editor is fully set up so SceneTreeDock::singleton
// is guaranteed to be valid.
void AIAssistantPlugin::_hook_scene_tree_menu() {
	SceneTreeDock *dock = SceneTreeDock::get_singleton();
	if (!dock) {
		return;
	}
	PopupMenu *ctx_menu = dock->get_context_menu();
	if (!ctx_menu) {
		return;
	}
	// about_to_popup fires every time the menu is shown, AFTER SceneTreeDock
	// has already filled it.  We append our item there so it always appears last.
	ctx_menu->connect("about_to_popup",
			callable_mp(this, &AIAssistantPlugin::_on_scene_menu_about_to_popup));
	ctx_menu->connect("id_pressed",
			callable_mp(this, &AIAssistantPlugin::_on_scene_menu_id_pressed));
}

void AIAssistantPlugin::_on_scene_menu_about_to_popup() {
	SceneTreeDock *dock = SceneTreeDock::get_singleton();
	if (!dock) {
		return;
	}
	PopupMenu *menu = dock->get_context_menu();
	if (!menu) {
		return;
	}
	// SceneTreeDock calls menu->clear() at the start of _tree_rmb, so our
	// previously-added item is already gone – just append fresh.
	menu->add_separator();
	menu->add_item(TTR("Mention in AI Assistant"), AI_MENU_MENTION_ID);
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
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, panel);

	// Defer the scene-tree hook until the editor is fully initialised.
	call_deferred("_hook_scene_tree_menu");
}

AIAssistantPlugin::~AIAssistantPlugin() {}

#endif // TOOLS_ENABLED
