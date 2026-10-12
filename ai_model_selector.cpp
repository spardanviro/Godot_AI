#ifdef TOOLS_ENABLED

#include "ai_model_selector.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"

void AIModelSelector::_bind_methods() {
	ADD_SIGNAL(MethodInfo("model_picked"));
}

void AIModelSelector::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		list_button->set_button_icon(get_theme_icon(SNAME("arrow"), SNAME("OptionButton")));
	}
}

bool AIModelSelector::is_custom() const {
	return !models.has(get_model());
}

String AIModelSelector::get_model() const {
	return name_edit->get_text().strip_edges();
}

void AIModelSelector::_fill(const PackedStringArray &p_models) {
	models = p_models;
	PopupMenu *popup = list_button->get_popup();
	popup->clear();
	for (int i = 0; i < models.size(); i++) {
		popup->add_item(models[i], i);
	}
	list_button->set_disabled(models.is_empty());
}

void AIModelSelector::set_models(const PackedStringArray &p_models) {
	_fill(p_models);
	name_edit->set_text(models.is_empty() ? String() : models[0]);
}

void AIModelSelector::update_models(const PackedStringArray &p_models, const String &p_preferred) {
	const String previous = get_model();
	const bool was_typed = !previous.is_empty() && !models.has(previous);

	_fill(p_models);
	if (was_typed || models.has(previous)) {
		return;
	}
	if (models.has(p_preferred)) {
		name_edit->set_text(p_preferred);
	} else if (previous.is_empty() && !models.is_empty()) {
		name_edit->set_text(models[0]);
	}
}

void AIModelSelector::select_model(const String &p_name) {
	if (!p_name.is_empty()) {
		name_edit->set_text(p_name);
	}
}

void AIModelSelector::set_placeholder(const String &p_text) {
	name_edit->set_placeholder(p_text);
}

void AIModelSelector::set_list_tooltip(const String &p_text) {
	list_button->set_tooltip_text(p_text);
}

void AIModelSelector::_on_list_item_pressed(int p_index) {
	if (p_index < 0 || p_index >= models.size()) {
		return;
	}
	name_edit->set_text(models[p_index]);
	emit_signal(SNAME("model_picked"));
}

AIModelSelector::AIModelSelector() {
	name_edit = memnew(LineEdit);
	name_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	name_edit->set_placeholder("Model name");
	add_child(name_edit);

	list_button = memnew(MenuButton);
	list_button->set_flat(false);
	list_button->set_disabled(true);
	list_button->get_popup()->connect("id_pressed", callable_mp(this, &AIModelSelector::_on_list_item_pressed));
	add_child(list_button);
}

#endif // TOOLS_ENABLED
