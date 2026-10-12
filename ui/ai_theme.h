#pragma once

#ifdef TOOLS_ENABLED

#include "ai_markdown.h"

#include "core/math/color.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_theme_manager.h"
#include "scene/gui/rich_text_label.h"

// The assistant's text colours are chosen for the dark editor theme: light,
// low-saturation tints. On a light theme the same colours all but vanish, so
// they are turned into their dark counterparts — same hue, readable on white.
inline Color ai_themed(const Color &p_color) {
	// No editor settings (unit tests): nothing to adapt to.
	if (!EditorSettings::get_singleton() || EditorThemeManager::is_dark_theme()) {
		return p_color;
	}
	const float saturation = p_color.get_s();
	// Greys stay grey; tints get stronger, or they would all look alike when dark.
	const float themed_saturation = saturation < 0.08f ? saturation : MIN(1.0f, saturation * 1.6f + 0.25f);
	return Color::from_hsv(p_color.get_h(), themed_saturation, p_color.get_v() * 0.45f, p_color.a);
}

// How Markdown looks in the editor's theme. p_font_size is the size of the
// text it is written into. Backgrounds and borders are translucent greys that
// work on either theme as they are.
inline AIMarkdownStyle ai_markdown_style(int p_font_size) {
	AIMarkdownStyle style;
	style.font_size = p_font_size;
	style.heading = ai_themed(style.heading);
	style.code = ai_themed(style.code);
	style.quote = ai_themed(style.quote);
	style.link = ai_themed(style.link);
	return style;
}

// Code in a label is written in its "mono_font", which the editor theme
// leaves at the interface font. Points it at the script editor's font, at the
// size of the text around it. Call when the theme changes.
inline void ai_use_code_font(RichTextLabel *p_label) {
	p_label->add_theme_font_override("mono_font", p_label->get_theme_font(SNAME("source"), EditorStringName(EditorFonts)));
	p_label->add_theme_font_size_override("mono_font_size", p_label->get_theme_font_size(SNAME("normal_font_size")));
}

#endif // TOOLS_ENABLED
