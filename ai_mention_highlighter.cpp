#ifdef TOOLS_ENABLED

#include "ai_mention_highlighter.h"

Dictionary AIMentionHighlighter::_get_line_syntax_highlighting_impl(int p_line) {
	Dictionary result;
	const TextEdit *te = get_text_edit();
	if (!te) {
		return result;
	}

	const String line = te->get_line(p_line);
	const int len = line.length();
	const Color normal_color = te->get_theme_color(SNAME("font_color"), SNAME("TextEdit"));
	const Color transparent(0.0f, 0.0f, 0.0f, 0.0f);

	for (int i = 0; i < len; i++) {
		const char32_t c = line[i];
		if (c >= 0xE000 && c <= 0xF8FF) {
			// Hide the PUA placeholder character by making it transparent.
			Dictionary hide_col;
			hide_col["color"] = transparent;
			result[i] = hide_col;

			// Restore normal colour for the character after the PUA char.
			if (i + 1 < len) {
				Dictionary restore_col;
				restore_col["color"] = normal_color;
				result[i + 1] = restore_col;
			}
		}
	}
	return result;
}

#endif // TOOLS_ENABLED
