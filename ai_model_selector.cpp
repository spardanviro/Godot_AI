#ifdef TOOLS_ENABLED

#include "ai_model_selector.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"

static const char *CUSTOM_ITEM = "Custom...";

void AIModelSelector::_bind_methods() {
	ADD_SIGNAL(MethodInfo("model_picked"));
}

// The custom entry is always the last one.
int AIModelSelector::_custom_index() const {
	return option->get_item_count() - 1;
}

bool AIModelSelector::is_custom() const {
	return option->get_selected() == _custom_index();
}

String AIModelSelector::get_model() const {
	if (is_custom()) {
		return custom_edit->get_text().strip_edges();
	}
	return option->get_item_text(option->get_selected()).strip_edges();
}

void AIModelSelector::_fill(const PackedStringArray &p_models) {
	option->clear();
	for (int i = 0; i < p_models.size(); i++) {
		option->add_item(p_models[i]);
	}
	option->add_item(CUSTOM_ITEM);
	option->select(0);
}

bool AIModelSelector::_select_listed(const String &p_name) {
	for (int i = 0; i < _custom_index(); i++) {
		if (option->get_item_text(i) == p_name) {
			option->select(i);
			custom_edit->hide();
			return true;
		}
	}
	return false;
}

void AIModelSelector::set_models(const PackedStringArray &p_models) {
	_fill(p_models);
	custom_edit->set_text(String());
	custom_edit->set_visible(is_custom()); // Only when the list is empty.
}

void AIModelSelector::update_models(const PackedStringArray &p_models, const String &p_preferred) {
	const bool was_custom = is_custom();
	const String previous = get_model();

	_fill(p_models);
	if (was_custom) {
		// The user chose to name a model, typed yet or not.
		option->select(_custom_index());
		custom_edit->show();
		select_model(previous); // A listed name moves to its entry.
	} else if (!_select_listed(previous) && !_select_listed(p_preferred)) {
		custom_edit->set_visible(is_custom());
	}
}

void AIModelSelector::select_model(const String &p_name) {
	if (p_name.is_empty() || _select_listed(p_name)) {
		return;
	}
	option->select(_custom_index());
	custom_edit->set_text(p_name);
	custom_edit->show();
}

void AIModelSelector::_on_item_selected(int /*p_index*/) {
	custom_edit->set_visible(is_custom());
	if (is_custom()) {
		custom_edit->grab_focus();
	}
	emit_signal(SNAME("model_picked"));
}

AIModelSelector::AIModelSelector() {
	option = memnew(OptionButton);
	option->add_item(CUSTOM_ITEM);
	option->connect("item_selected", callable_mp(this, &AIModelSelector::_on_item_selected));
	add_child(option);

	custom_edit = memnew(LineEdit);
	custom_edit->set_placeholder("Enter model name, e.g. gpt-4o-mini");
	custom_edit->hide();
	add_child(custom_edit);
}

#endif // TOOLS_ENABLED
