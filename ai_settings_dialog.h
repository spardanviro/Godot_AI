#pragma once

#ifdef TOOLS_ENABLED

#include "ai_provider.h"
#include "ai_permission_manager.h"
#include "ai_localization.h"
#include "scene/gui/dialogs.h"

class CheckBox;
class LineEdit;
class OptionButton;
class SpinBox;
class Label;
class HTTPRequest;

class AISettingsDialog : public AcceptDialog {
	GDCLASS(AISettingsDialog, AcceptDialog);

	OptionButton *provider_option = nullptr;
	LineEdit *api_key_edit = nullptr;
	OptionButton *model_option = nullptr;
	LineEdit *custom_model_edit = nullptr; // Visible only when "Custom..." is selected.
	LineEdit *endpoint_edit = nullptr;
	SpinBox *max_tokens_spin = nullptr;
	SpinBox *temperature_spin = nullptr;
	CheckBox *send_on_enter_check = nullptr;
	CheckBox *autorun_check = nullptr;
	CheckBox *restore_last_chat_check = nullptr;
	OptionButton *language_option = nullptr;
	Label *fetch_status_label = nullptr;

	// Labels that need dynamic language update.
	Label *label_language = nullptr;
	Label *label_provider = nullptr;
	Label *label_api_key = nullptr;
	Label *label_model = nullptr;
	Label *label_endpoint = nullptr;
	Label *label_max_tokens = nullptr;
	Label *label_temperature = nullptr;
	Label *label_send_on_enter = nullptr;
	Label *label_auto_execute = nullptr;
	Label *label_restore_last_chat = nullptr;
	Label *label_permissions = nullptr;
	Label *perm_labels[AIPermissionManager::PERM_MAX] = {};

	// N6: Permission settings.
	OptionButton *perm_options[AIPermissionManager::PERM_MAX] = {};

	// Auto-fetch.
	HTTPRequest *models_http_request = nullptr;
	Ref<AIProvider> fetch_provider;
	bool is_fetching = false;

	// Per-provider API keys (0=Anthropic, 1=OpenAI, 2=Gemini, 3=GLM, 4=DeepSeek).
	String provider_api_keys[5];
	int current_provider_idx = 0;

	// Default model per provider (used to seed dropdown before fetch completes).
	String _get_default_model_for(int p_provider_index) const;
	// Known current models per provider — seeds dropdown immediately; live fetch updates it.
	PackedStringArray _get_fallback_models_for(int p_provider_index) const;

	void _on_provider_changed(int p_index);
	void _on_model_changed(int p_index);
	void _on_custom_model_changed(const String &p_text);
	// Ensures "Custom..." always sits at the end of model_option.
	void _append_custom_model_item();
	// Select "Custom..." entry and pre-fill the edit with p_name.
	void _select_custom_model(const String &p_name);
	void _on_api_key_changed(const String &p_text);
	void _on_language_changed(int p_index);
	void _on_confirmed();
	void _load_settings();
	void _update_ui_texts();
	void _auto_fetch_models();
	void _on_models_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	Ref<AIProvider> _create_provider_for_index(int p_index, const String &p_api_key, const String &p_endpoint) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void refresh();
	AISettingsDialog();

	// Signal emitted when language changes so the panel can refresh.
	// Declared in _bind_methods.
};

#endif // TOOLS_ENABLED
