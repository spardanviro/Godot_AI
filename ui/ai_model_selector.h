#pragma once

#ifdef TOOLS_ENABLED

#include "scene/gui/box_container.h"

class LineEdit;
class MenuButton;

// The model to use: a field that takes any model name, with a button beside
// it that lists the models known for the provider and fills the field with
// the one chosen. Emits model_picked when a model is chosen from the list, or a typed name is
// entered (Enter, or leaving the field).
class AIModelSelector : public HBoxContainer {
	GDCLASS(AIModelSelector, HBoxContainer);

	LineEdit *name_edit = nullptr;
	MenuButton *list_button = nullptr;
	PackedStringArray models;

	void _fill(const PackedStringArray &p_models);
	void _on_list_item_pressed(int p_index);
	void _on_name_entered();
	void _on_name_submitted(const String &p_text);
	String last_announced; // The name model_picked was last emitted for.

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Replaces the list and selects its first model.
	void set_models(const PackedStringArray &p_models);
	// Replaces the list but keeps the current choice: a name the user typed,
	// or the selected model if it is still listed. Otherwise p_preferred if
	// it is listed. With no such recommendation the name stays as it is, and
	// an empty field takes the first model.
	void update_models(const PackedStringArray &p_models, const String &p_preferred);
	// Puts a model name in the field, listed or not.
	void select_model(const String &p_name);
	void set_placeholder(const String &p_text);
	void set_list_tooltip(const String &p_text);

	// Whether the name in the field is not one of the listed models.
	bool is_custom() const;
	String get_model() const;
	PackedStringArray get_models() const { return models; }

	AIModelSelector();
};

#endif // TOOLS_ENABLED
