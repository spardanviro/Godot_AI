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
	LineEdit *model_edit = nullptr;
	LineEdit *endpoint_edit = nullptr;
	SpinBox *max_tokens_spin = nullptr;
	SpinBox *temperature_spin = nullptr;
	CheckBox *send_on_enter_check = nullptr;
	CheckBox *autorun_check = nullptr;
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
	Label *label_permissions = nullptr;
	Label *perm_labels[AIPermissionManager::PERM_MAX] = {};

	// N6: Permission settings.
	OptionButton *perm_options[AIPermissionManager::PERM_MAX] = {};

	// Auto-fetch.
	HTTPRequest *models_http_request = nullptr;
	Ref<AIProvider> fetch_provider;
	bool is_fetching = false;

	// Known defaults for auto-switch.
	String _get_default_model_for(int p_provider_index) const;
	bool _is_known_default(const String &p_model) const;

	void _on_provider_changed(int p_index);
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
