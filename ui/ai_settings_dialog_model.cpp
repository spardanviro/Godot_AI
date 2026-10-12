#ifdef TOOLS_ENABLED

// AISettingsDialog: showing the provider, its API key and its models. What is
// being edited lives in AIProviderSettings; this file puts it on screen and
// reports what the user does.

#include "ai_settings_dialog.h"

#include "../common/ai_log.h"
#include "../network/ai_model_list_fetcher.h"
#include "ai_model_selector.h"
#include "ai_theme.h"

#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/spin_box.h"

static const Color COLOR_STATUS_MUTED(0.65, 0.65, 0.65);
static const Color COLOR_STATUS_OK(0.5, 1.0, 0.5);
static const Color COLOR_STATUS_WARNING(1.0, 0.6, 0.3);

void AISettingsDialog::_show_provider() {
	api_key_edit->set_text(provider_settings.get_typed_key());
	_update_api_key_state();
	endpoint_edit->set_text(provider_settings.get_endpoint());
	endpoint_edit->set_placeholder(provider_settings.get_default_endpoint());

	// The models known at build time, until (or unless) the live list arrives.
	model_selector->set_models(provider_settings.get_known_models());
	_apply_recommended_max_tokens();
}

void AISettingsDialog::_on_provider_changed(int p_index) {
	max_tokens_edited = false; // The value was for the other provider.
	provider_settings.select_provider(p_index);
	_show_provider();
	_fetch_models();
}

void AISettingsDialog::_on_endpoint_changed(const String &p_text) {
	provider_settings.set_endpoint(p_text);
}

// =============================================================================
// API key
// =============================================================================

void AISettingsDialog::_on_api_key_changed(const String &p_text) {
	provider_settings.type_key(p_text);
	_update_api_key_state();
}

// Only what is being typed can be shown; a stored key never enters the field.
void AISettingsDialog::_on_show_key_toggled(bool p_shown) {
	api_key_edit->set_secret(!p_shown);
	show_key_button->set_text(p_shown ? AI_L("Hide", "隐藏") : AI_L("Show", "显示"));
}

void AISettingsDialog::_on_clear_key_pressed() {
	provider_settings.request_key_removal();
	api_key_edit->set_text(String());
	_update_api_key_state();
}

// An empty key field can mean several things; the placeholder says which.
void AISettingsDialog::_update_api_key_state() {
	switch (provider_settings.get_key_state()) {
		case AIProviderSettings::KEY_FROM_ENVIRONMENT:
			api_key_edit->set_placeholder(AI_L("Using environment variable ", "正在使用环境变量 ") + provider_settings.get_key_environment_variable());
			break;
		case AIProviderSettings::KEY_REMOVAL_PENDING:
			api_key_edit->set_placeholder(AI_L("The saved key will be removed when you press OK", "点击“确定”后将删除已保存的密钥"));
			break;
		case AIProviderSettings::KEY_STORED:
			api_key_edit->set_placeholder(AI_L("Saved (leave empty to keep it; type a new key to replace it)", "已保存（留空则保持不变，输入新密钥可替换）"));
			break;
		case AIProviderSettings::KEY_NONE:
			api_key_edit->set_placeholder(TR(STR_SETTINGS_API_KEY_PLACEHOLDER));
			break;
	}
	clear_key_button->set_disabled(!provider_settings.can_remove_key());
}

// =============================================================================
// Max tokens
// =============================================================================

void AISettingsDialog::_set_max_tokens(int p_value) {
	setting_max_tokens = true;
	// Some models go beyond the spin box's default ceiling.
	max_tokens_spin->set_max(MAX(p_value, (int)max_tokens_spin->get_max()));
	max_tokens_spin->set_value(p_value);
	setting_max_tokens = false;
}

void AISettingsDialog::_on_max_tokens_changed(double /*p_value*/) {
	if (!setting_max_tokens) {
		max_tokens_edited = true;
	}
}

// Choosing a model asks for that model's recommendation.
void AISettingsDialog::_on_model_picked() {
	max_tokens_edited = false;
	_apply_recommended_max_tokens();
}

void AISettingsDialog::_apply_recommended_max_tokens() {
	const String model = model_selector->get_model();
	if (model_selector->is_custom() || model.is_empty()) {
		return; // Nothing is known about a model the user names.
	}
	_set_max_tokens(provider_settings.get_recommended_max_tokens(model));
}

// =============================================================================
// Model list
// =============================================================================

void AISettingsDialog::_set_fetch_status(const String &p_text, const Color &p_color) {
	fetch_status_label->set_text(p_text);
	fetch_status_label->set_tooltip_text(p_text);
	fetch_status_label->add_theme_color_override("font_color", ai_themed(p_color));
}

void AISettingsDialog::_fetch_models() {
	const String api_key = provider_settings.get_usable_key();
	if (api_key.is_empty()) {
		model_list_fetcher->cancel();
		_set_fetch_status(AI_L("Enter an API key to fetch the model list.", "输入 API 密钥后可获取模型列表。"), COLOR_STATUS_MUTED);
		return;
	}
	if (model_list_fetcher->fetch(provider_settings.make_provider(api_key))) {
		_set_fetch_status(TR(STR_FETCH_FETCHING), COLOR_STATUS_MUTED);
	} else {
		_set_fetch_status(TR(STR_FETCH_FAILED), COLOR_STATUS_WARNING);
	}
}

void AISettingsDialog::_on_models_fetched(const PackedStringArray &p_models, const String &p_best) {
	model_selector->update_models(p_models, p_best);
	const String count = itos(p_models.size()) + AI_L(" models", " 个模型");
	if (p_models.has(p_best)) {
		_set_fetch_status(AI_L("Connected. Latest: ", "连接正常。最新：") + p_best + " (" + count + ")", COLOR_STATUS_OK);
	} else {
		// Another service's models: nothing to recommend, pick or type one.
		_set_fetch_status(AI_L("Connected: ", "连接正常：") + count + AI_L(". Pick one from the list or type its name.", "。可从列表选择，或直接填写模型名。"), COLOR_STATUS_OK);
	}

	// The list may have moved the selection to another model, so its output
	// cap is shown — unless the user just entered one, or named a model
	// nothing is known about.
	const String model = model_selector->get_model();
	if (max_tokens_edited || model_selector->is_custom() || model.is_empty()) {
		return;
	}
	_set_max_tokens(provider_settings.get_max_tokens_for_listed_model(model));
}

void AISettingsDialog::_on_models_fetch_failed(const String &p_message) {
	_set_fetch_status(p_message, COLOR_STATUS_WARNING);
}

#endif // TOOLS_ENABLED
