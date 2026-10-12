#pragma once

#ifdef TOOLS_ENABLED

#include "../execution/ai_permission_manager.h"
#include "../providers/ai_provider_settings.h"

#include "scene/gui/dialogs.h"

class AIModelListFetcher;
class AIModelSelector;
class Button;
class CheckBox;
class Label;
class LineEdit;
class OptionButton;
class SpinBox;
class TabContainer;
class VBoxContainer;

// The "Godot AI Settings" window: shows the assistant's settings on four tabs
// (model, general, permissions, advanced) and writes them back on OK. What is
// being edited about the provider — its endpoint, its API key — is held by
// AIProviderSettings; the dialog shows it. Cancel discards everything,
// including a language switch tried out in the dialog. Emits language_changed
// when the interface language (or anything else the panel renders text from)
// may have changed.
class AISettingsDialog : public ConfirmationDialog {
	GDCLASS(AISettingsDialog, ConfirmationDialog);

	TabContainer *tabs = nullptr;

	// --- Model tab ---
	OptionButton *provider_option = nullptr;
	LineEdit *api_key_edit = nullptr;
	Button *show_key_button = nullptr;
	Button *clear_key_button = nullptr;
	AIModelSelector *model_selector = nullptr;
	Label *fetch_status_label = nullptr;
	Button *refresh_models_button = nullptr;
	LineEdit *endpoint_edit = nullptr;
	SpinBox *max_tokens_spin = nullptr;
	CheckBox *custom_temperature_check = nullptr;
	SpinBox *temperature_spin = nullptr;

	// --- General tab ---
	OptionButton *language_option = nullptr;
	CheckBox *send_on_enter_check = nullptr;
	CheckBox *autorun_check = nullptr;
	CheckBox *restore_last_chat_check = nullptr;
	CheckBox *runtime_fix_check = nullptr;
	CheckBox *use_tools_check = nullptr;
	SpinBox *compression_keep_recent_spin = nullptr;
	SpinBox *compression_threshold_spin = nullptr;
	CheckBox *compression_ai_summary_check = nullptr;

	// --- Permissions tab ---
	Label *permissions_hint_label = nullptr;
	Button *allow_all_button = nullptr;
	Button *ask_all_button = nullptr;
	Button *default_permissions_button = nullptr;
	OptionButton *permission_options[AIPermissionManager::PERM_MAX] = {};

	// --- Advanced tab ---
	OptionButton *proxy_mode_option = nullptr;
	LineEdit *proxy_address_edit = nullptr;
	Label *proxy_status_label = nullptr;
	LineEdit *image_model_edit = nullptr;
	LineEdit *speech_model_edit = nullptr;
	LineEdit *knowledge_dir_edit = nullptr;
	Button *open_knowledge_dir_button = nullptr;

	// Row and section captions; their text follows the interface language.
	Label *label_provider = nullptr;
	Label *label_api_key = nullptr;
	Label *label_model = nullptr;
	Label *label_endpoint = nullptr;
	Label *label_max_tokens = nullptr;
	Label *label_temperature = nullptr;
	Label *label_language = nullptr;
	Label *label_send_on_enter = nullptr;
	Label *label_auto_execute = nullptr;
	Label *label_restore_last_chat = nullptr;
	Label *label_runtime_fix = nullptr;
	Label *label_use_tools = nullptr;
	Label *label_compression = nullptr;
	Label *label_compression_keep_recent = nullptr;
	Label *label_compression_threshold = nullptr;
	Label *label_compression_ai_summary = nullptr;
	Label *permission_labels[AIPermissionManager::PERM_MAX] = {};
	Label *label_proxy = nullptr;
	Label *label_proxy_mode = nullptr;
	Label *label_proxy_address = nullptr;
	Label *label_assets = nullptr;
	Label *label_image_model = nullptr;
	Label *label_speech_model = nullptr;
	Label *label_knowledge = nullptr;
	Label *label_knowledge_dir = nullptr;

	AIModelListFetcher *model_list_fetcher = nullptr;

	AIProviderSettings provider_settings;
	// The user changed max tokens by hand since the dialog was loaded; the
	// model list arriving later must not replace what they entered.
	bool max_tokens_edited = false;
	bool setting_max_tokens = false; // The dialog itself is changing the value.
	String language_at_open; // To undo a language switch on Cancel.

	// --- Layout (ai_settings_dialog_layout.cpp) ---
	VBoxContainer *_add_tab(const String &p_name);
	void _build_model_tab(VBoxContainer *p_parent);
	void _build_general_tab(VBoxContainer *p_parent);
	void _build_permissions_tab(VBoxContainer *p_parent);
	void _build_advanced_tab(VBoxContainer *p_parent);
	void _update_ui_texts();

	// --- Settings (ai_settings_dialog.cpp) ---
	void _load_settings();
	void _on_confirmed();
	void _on_canceled();
	// Drops what was typed but not saved; keys must not linger in hidden widgets.
	void _forget_unsaved_keys();
	void _on_language_changed(int p_index);
	void _set_all_permissions(int p_level);
	void _reset_permissions();
	void _on_custom_temperature_toggled(bool p_enabled);
	void _on_proxy_changed();
	void _on_proxy_mode_selected(int p_index);
	void _on_proxy_address_changed(const String &p_text);
	String _get_proxy_mode() const;
	String _get_knowledge_dir() const;
	void _on_open_knowledge_dir_pressed();

	// --- Provider, key and model (ai_settings_dialog_model.cpp) ---
	// Shows the selected provider's key field, endpoint and known models.
	void _show_provider();
	void _on_endpoint_changed(const String &p_text);
	void _on_provider_changed(int p_index);
	void _on_api_key_changed(const String &p_text);
	void _on_show_key_toggled(bool p_shown);
	void _on_clear_key_pressed();
	void _update_api_key_state();
	void _set_max_tokens(int p_value);
	void _on_max_tokens_changed(double p_value);
	void _on_model_picked();
	void _apply_recommended_max_tokens();
	void _set_fetch_status(const String &p_text, const Color &p_color);
	void _fetch_models();
	void _on_models_fetched(const PackedStringArray &p_models, const String &p_best);
	void _on_models_fetch_failed(const String &p_message);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Registers the ai_assistant/* settings this dialog edits, with their
	// defaults. Safe to call more than once.
	static void register_default_settings();

	// Reloads the settings and the model list; call before showing the dialog.
	void refresh();
	// Shows the dialog, never larger than the screen it is on.
	void open();

	AISettingsDialog();
};

#endif // TOOLS_ENABLED
