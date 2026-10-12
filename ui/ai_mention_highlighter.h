#pragma once

#ifdef TOOLS_ENABLED

#include "scene/gui/text_edit.h"

// Minimal SyntaxHighlighter that hides PUA characters (U+E000–U+F8FF)
// used as mention chip placeholders. Sets their foreground colour to
// fully transparent so only the inline-object chip drawing is visible.
class AIMentionHighlighter : public SyntaxHighlighter {
	GDCLASS(AIMentionHighlighter, SyntaxHighlighter);

protected:
	static void _bind_methods() {}

public:
	virtual Dictionary _get_line_syntax_highlighting_impl(int p_line) override;

	AIMentionHighlighter() {}
};

#endif // TOOLS_ENABLED
