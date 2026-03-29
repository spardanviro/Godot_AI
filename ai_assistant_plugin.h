#pragma once

#ifdef TOOLS_ENABLED

#include "editor/plugins/editor_plugin.h"

class AIAssistantPanel;

class AIAssistantPlugin : public EditorPlugin {
	GDCLASS(AIAssistantPlugin, EditorPlugin);

	AIAssistantPanel *panel = nullptr;

	// Scene-tree right-click hook — adds "Mention in AI Assistant".
	static const int AI_MENU_MENTION_ID = 9001;
	void _hook_scene_tree_menu();
	void _on_scene_menu_about_to_popup();
	void _on_scene_menu_id_pressed(int p_id);

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override { return "AIAssistant"; }

	AIAssistantPlugin();
	~AIAssistantPlugin();
};

#endif // TOOLS_ENABLED
