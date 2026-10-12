#ifdef TOOLS_ENABLED

#include "ai_history_list.h"

#include "../agent/ai_chat_store.h"
#include "../common/ai_log.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"

static const int DELETE_BUTTON_WIDTH = 28;

void AIHistoryList::_bind_methods() {
	ADD_SIGNAL(MethodInfo("chat_selected", PropertyInfo(Variant::STRING, "path")));
	ADD_SIGNAL(MethodInfo("delete_requested", PropertyInfo(Variant::STRING, "path")));
}

void AIHistoryList::_on_chat_pressed(const String &p_path) {
	emit_signal(SNAME("chat_selected"), p_path);
}

void AIHistoryList::_on_delete_pressed(const String &p_path) {
	emit_signal(SNAME("delete_requested"), p_path);
}

void AIHistoryList::refresh() {
	while (rows->get_child_count() > 0) {
		Node *child = rows->get_child(0);
		rows->remove_child(child);
		memdelete(child);
	}

	const Vector<AIChatStore::Entry> chats = AIChatStore::list();
	if (chats.is_empty()) {
		Label *empty_label = memnew(Label);
		empty_label->set_text(TR(STR_SYS_NO_HISTORY));
		empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		rows->add_child(empty_label);
		return;
	}

	for (int i = 0; i < chats.size(); i++) {
		HBoxContainer *row = memnew(HBoxContainer);
		rows->add_child(row);

		Button *title_button = memnew(Button);
		title_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		title_button->set_text(chats[i].title + "  (" + itos(chats[i].message_count) + " msgs)");
		title_button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		title_button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		title_button->set_flat(true);
		title_button->connect("pressed", callable_mp(this, &AIHistoryList::_on_chat_pressed).bind(chats[i].path));
		row->add_child(title_button);

		Button *delete_button = memnew(Button);
		delete_button->set_text("X");
		delete_button->set_tooltip_text(AI_L("Delete this chat", "删除此对话"));
		delete_button->set_custom_minimum_size(Size2(DELETE_BUTTON_WIDTH, 0) * EDSCALE);
		delete_button->connect("pressed", callable_mp(this, &AIHistoryList::_on_delete_pressed).bind(chats[i].path));
		row->add_child(delete_button);
	}
}

AIHistoryList::AIHistoryList() {
	set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);

	rows = memnew(VBoxContainer);
	rows->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	add_child(rows);
}

#endif // TOOLS_ENABLED
