#pragma once

#ifdef TOOLS_ENABLED

#include "scene/gui/box_container.h"

class LineEdit;
class OptionButton;

// A model dropdown whose last entry, "Custom...", reveals a field for typing
// any model name. Emits model_picked when the user chooses an entry.
class AIModelSelector : public VBoxContainer {
	GDCLASS(AIModelSelector, VBoxContainer);

	OptionButton *option = nullptr;
	LineEdit *custom_edit = nullptr;

	int _custom_index() const;
	// Selects p_name if it is listed. Does not touch the custom field.
	bool _select_listed(const String &p_name);
	void _fill(const PackedStringArray &p_models);
	void _on_item_selected(int p_index);

protected:
	static void _bind_methods();

public:
	// Replaces the list and selects its first model.
	void set_models(const PackedStringArray &p_models);
	// Replaces the list but keeps the current choice — the selected model if
	// it is still listed, or the custom entry with what was typed. Otherwise
	// p_preferred, otherwise the first model.
	void update_models(const PackedStringArray &p_models, const String &p_preferred);
	// Selects a model; one that is not listed goes into the custom field.
	void select_model(const String &p_name);

	bool is_custom() const;
	// The chosen model: the listed one, or what was typed for "Custom...".
	String get_model() const;

	AIModelSelector();
};

#endif // TOOLS_ENABLED
