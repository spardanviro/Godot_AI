#pragma once

#ifdef TOOLS_ENABLED

#include "../agent/ai_response_parser.h"

#include "scene/gui/dialogs.h"

class RichTextLabel;

// The window behind a "Details" or "Thinking" link in the chat: a whole
// reply, its text rendered as Markdown and its code blocks set apart, or the
// model's reasoning.
class AIDetailsDialog : public AcceptDialog {
	GDCLASS(AIDetailsDialog, AcceptDialog);

	RichTextLabel *text = nullptr;

protected:
	static void _bind_methods() {}

public:
	// Does nothing for an empty reply.
	void show_response(const String &p_response, const Ref<AIResponseParser> &p_parser);
	void show_thinking(const String &p_thinking);

	AIDetailsDialog();
};

#endif // TOOLS_ENABLED
