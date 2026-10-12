#pragma once

#ifdef TOOLS_ENABLED

#include "core/math/color.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_theme_manager.h"

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

#endif // TOOLS_ENABLED
