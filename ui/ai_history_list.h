#pragma once

#ifdef TOOLS_ENABLED

#include "scene/gui/scroll_container.h"

class VBoxContainer;

// The saved chats, newest first, each with a button to open it and one to
// delete it. It only lists and reports: chat_selected(path) and
// delete_requested(path) say what the user asked for.
class AIHistoryList : public ScrollContainer {
	GDCLASS(AIHistoryList, ScrollContainer);

	VBoxContainer *rows = nullptr;

	void _on_chat_pressed(const String &p_path);
	void _on_delete_pressed(const String &p_path);

protected:
	static void _bind_methods();

public:
	// Rebuilds the list from the saved chats.
	void refresh();

	AIHistoryList();
};

#endif // TOOLS_ENABLED
