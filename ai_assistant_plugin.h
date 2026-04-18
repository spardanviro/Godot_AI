#pragma once

#ifdef TOOLS_ENABLED

#include "editor/docks/editor_dock.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/inspector/editor_context_menu_plugin.h"

class AIAssistantPanel;

// Minimal EditorContextMenuPlugin subclass used to inject "在GodotAI中提及"
// into the scene-tree and filesystem right-click menus.
// One instance is registered per slot; each calls a different panel method.
class AIContextMenuPlugin : public EditorContextMenuPlugin {
	GDCLASS(AIContextMenuPlugin, EditorContextMenuPlugin);

	String label;
	Callable callback;
	Ref<Texture2D> icon;

protected:
	static void _bind_methods() {} // No GDScript exposure needed.

public:
	void setup(const String &p_label, const Callable &p_callback, const Ref<Texture2D> &p_icon = Ref<Texture2D>()) {
		label    = p_label;
		callback = p_callback;
		icon     = p_icon;
	}

	virtual void get_options(const Vector<String> & /*p_paths*/) override {
		if (!label.is_empty() && callback.is_valid()) {
			add_context_menu_item(label, callback, icon);
		}
	}
};

class AIAssistantPlugin : public EditorPlugin {
	GDCLASS(AIAssistantPlugin, EditorPlugin);

	AIAssistantPanel *panel = nullptr;
	EditorDock       *dock  = nullptr;

	// Context-menu plugins (kept alive by Ref<>).
	Ref<AIContextMenuPlugin> scene_ctx_plugin;
	Ref<AIContextMenuPlugin> fs_ctx_plugin;

	void _focus_dock();

	// Legacy scene-tree hook stubs (get_context_menu() removed in 4.7).
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
