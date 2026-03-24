#ifdef TOOLS_ENABLED

#include "ai_ui_agent.h"

void AIUIAgent::_bind_methods() {
}

bool AIUIAgent::is_ui_request(const String &p_message) {
	String lower = p_message.to_lower();

	// Chinese keywords.
	if (lower.find(String::utf8("界面")) != -1 || lower.find(String::utf8("菜单")) != -1 ||
			lower.find(String::utf8("按钮")) != -1 || lower.find(String::utf8("标签")) != -1 ||
			lower.find(String::utf8("输入框")) != -1 || lower.find(String::utf8("文本")) != -1 ||
			lower.find(String::utf8("布局")) != -1 || lower.find(String::utf8("面板")) != -1 ||
			lower.find(String::utf8("对话框")) != -1 || lower.find(String::utf8("血条")) != -1 ||
			lower.find(String::utf8("背包")) != -1 || lower.find(String::utf8("物品栏")) != -1 ||
			lower.find(String::utf8("设置界面")) != -1) {
		return true;
	}

	// English keywords.
	static const char *ui_keywords[] = {
		"ui", "gui", "hud", "menu", "button", "label", "panel",
		"dialog", "inventory", "health bar", "progress bar",
		"text input", "dropdown", "slider", "checkbox",
		"tab", "container", "layout", "control", "widget",
		"main menu", "settings menu", "pause menu", "game over",
		"score", "minimap", "tooltip", "popup",
		nullptr
	};

	for (int i = 0; ui_keywords[i]; i++) {
		if (lower.find(ui_keywords[i]) != -1) {
			return true;
		}
	}

	return false;
}

String AIUIAgent::get_ui_system_prompt() {
	String p;
	p += "\n## UI/Control Node Specialization\n";
	p += "The user is requesting UI work. Use these Control node best practices:\n\n";

	p += "### Container Hierarchy\n";
	p += "- Use containers (VBoxContainer, HBoxContainer, GridContainer, MarginContainer) for automatic layout.\n";
	p += "- Nest containers: MarginContainer > VBoxContainer > HBoxContainer for proper spacing.\n";
	p += "- Use PanelContainer for backgrounds with StyleBoxFlat.\n";
	p += "- Use ScrollContainer for scrollable content.\n\n";

	p += "### Anchors and Sizing\n";
	p += "- Use anchors for responsive positioning: ANCHOR_BEGIN (0), ANCHOR_END (1), ANCHOR_CENTER (0.5).\n";
	p += "- set_anchors_preset(Control.PRESET_FULL_RECT) for fullscreen UI.\n";
	p += "- set_anchors_preset(Control.PRESET_CENTER_TOP) for centered top elements.\n";
	p += "- set_anchors_preset(Control.PRESET_BOTTOM_LEFT) for HUD elements.\n";
	p += "- size_flags_horizontal/vertical: SIZE_EXPAND_FILL, SIZE_SHRINK_CENTER, SIZE_FILL.\n\n";

	p += "### Common UI Patterns\n";
	p += "#### HUD (Head-Up Display):\n";
	p += "```gdscript\n";
	p += "var canvas = CanvasLayer.new()\n";
	p += "canvas.name = \"HUD\"\n";
	p += "canvas.layer = 10\n";
	p += "var margin = MarginContainer.new()\n";
	p += "margin.set_anchors_preset(Control.PRESET_FULL_RECT)\n";
	p += "margin.add_theme_constant_override(\"margin_left\", 20)\n";
	p += "margin.add_theme_constant_override(\"margin_top\", 20)\n";
	p += "margin.add_theme_constant_override(\"margin_right\", 20)\n";
	p += "margin.add_theme_constant_override(\"margin_bottom\", 20)\n";
	p += "canvas.add_child(margin)\n";
	p += "```\n\n";

	p += "#### Centered Dialog/Panel:\n";
	p += "```gdscript\n";
	p += "var panel = PanelContainer.new()\n";
	p += "panel.set_anchors_preset(Control.PRESET_CENTER)\n";
	p += "panel.custom_minimum_size = Vector2(400, 300)\n";
	p += "var vbox = VBoxContainer.new()\n";
	p += "panel.add_child(vbox)\n";
	p += "var title = Label.new()\n";
	p += "title.text = \"Title\"\n";
	p += "title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER\n";
	p += "vbox.add_child(title)\n";
	p += "```\n\n";

	p += "### Theme and Styling\n";
	p += "- Use add_theme_color_override(), add_theme_font_size_override(), add_theme_constant_override() for inline styling.\n";
	p += "- Create StyleBoxFlat for custom backgrounds: var style = StyleBoxFlat.new(); style.bg_color = Color(...)\n";
	p += "- Apply with add_theme_stylebox_override(\"panel\", style)\n\n";

	p += "### Input Handling for UI\n";
	p += "- Buttons: connect(\"pressed\", callable)\n";
	p += "- LineEdit: connect(\"text_submitted\", callable)\n";
	p += "- Slider/SpinBox: connect(\"value_changed\", callable)\n";
	p += "- For editor scripts, use print() to show what would happen on interaction.\n\n";

	return p;
}

AIUIAgent::AIUIAgent() {
}

#endif // TOOLS_ENABLED
