#pragma once

#ifdef TOOLS_ENABLED

#include "editor/docks/editor_dock.h"
#include "editor/plugins/editor_plugin.h"

class AIAssistantPanel;

class AIAssistantPlugin : public EditorPlugin {
	GDCLASS(AIAssistantPlugin, EditorPlugin);

	AIAssistantPanel *panel = nullptr;
	EditorDock *dock = nullptr;

	void _focus_dock();

	// Scene-tree right-click hook — adds "Mention in AI Assistant".
	static const int AI_MENU_MENTION_ID = 9001;
	void _hook_scene_tree_menu();
	void _on_scene_menu_about_to_popup();
	void _on_scene_menu_id_pressed(int p_id);

	// Debugger integration — forward runtime errors to AIErrorMonitor via debug_data signal.
	void _connect_debugger_signal();
	void _on_debug_data(const String &p_msg, const Array &p_data);
	void _on_debugger_started();

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override { return "AIAssistant"; }

	AIAssistantPlugin();
	~AIAssistantPlugin();
};

#endif // TOOLS_ENABLED
