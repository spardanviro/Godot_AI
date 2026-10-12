#ifdef TOOLS_ENABLED

// AISettingsDialog: the provider, its API key and its models.

#include "ai_settings_dialog.h"

#include "ai_credential_store.h"
#include "ai_log.h"
#include "ai_model_list_fetcher.h"
#include "ai_model_selector.h"
#include "ai_provider_catalog.h"
#include "ai_provider_setup.h"

#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/spin_box.h"

// The value "max tokens" has before the user (or we) ever changed it.
static const int UNSET_MAX_TOKENS = 4096;

static const Color COLOR_STATUS_MUTED(0.65, 0.65, 0.65);
static const Color COLOR_STATUS_OK(0.5, 1.0, 0.5);
static const Color COLOR_STATUS_WARNING(1.0, 0.6, 0.3);

// The selected provider with the endpoint from the dialog, for asking it
// something (its model list, what it recommends for a model).
Ref<AIProvider> AISettingsDialog::_make_provider(const String &p_api_key) const {
	Ref<AIProvider> provider = AIProvider::create(AIProviderCatalog::get_id(current_provider));
	provider->set_api_key(p_api_key);
	const String endpoint = endpoint_edit->get_text().strip_edges();
	if (!endpoint.is_empty()) {
		provider->set_api_endpoint(endpoint);
	}
	return provider;
}

void AISettingsDialog::_show_provider(int p_index) {
	current_provider = p_index;
	const String provider_id = AIProviderCatalog::get_id(p_index);

	api_key_edit->set_text(typed_api_keys.has(provider_id) ? typed_api_keys[provider_id] : String());
	_update_api_key_state();
	endpoint_edit->set_text(endpoints.has(provider_id) ? endpoints[provider_id] : String());
	endpoint_edit->set_placeholder(AIProvider::create(provider_id)->get_default_endpoint());

	// The models known at build time, until (or unless) the live list arrives.
	model_selector->set_models(AIProviderCatalog::get_known_models(p_index));
	_apply_recommended_max_tokens();
}

void AISettingsDialog::_on_provider_changed(int p_index) {
	max_tokens_edited = false; // The value was for the other provider.
	// So is the endpoint: each provider keeps its own (see _show_provider()).
	endpoints[AIProviderCatalog::get_id(current_provider)] = endpoint_edit->get_text();
	_show_provider(p_index);
	_fetch_models();
}

// =============================================================================
// API key
// =============================================================================

void AISettingsDialog::_on_api_key_changed(const String &p_text) {
	const String provider_id = AIProviderCatalog::get_id(current_provider);
	typed_api_keys[provider_id] = p_text;
	if (!p_text.strip_edges().is_empty()) {
		keys_to_clear.erase(provider_id); // A new key replaces the old one instead.
		_update_api_key_state();
	}
}

// Only what is being typed can be shown; a stored key never enters the field.
void AISettingsDialog::_on_show_key_toggled(bool p_shown) {
	api_key_edit->set_secret(!p_shown);
	show_key_button->set_text(p_shown ? AI_L("Hide", "隐藏") : AI_L("Show", "显示"));
}

void AISettingsDialog::_on_clear_key_pressed() {
	const String provider_id = AIProviderCatalog::get_id(current_provider);
	keys_to_clear.insert(provider_id);
	typed_api_keys.erase(provider_id);
	api_key_edit->set_text(String());
	_update_api_key_state();
}

// An empty key field can mean several things; the placeholder says which.
void AISettingsDialog::_update_api_key_state() {
	const String provider_id = AIProviderCatalog::get_id(current_provider);
	const String variable = AICredentialStore::get_environment_source(provider_id);
	const bool has_stored_key = !AICredentialStore::get_stored_api_key(provider_id).is_empty();
	const bool removal_pending = keys_to_clear.has(provider_id);

	if (!variable.is_empty()) {
		// A key from the environment takes precedence over the stored one.
		api_key_edit->set_placeholder(AI_L("Using environment variable ", "正在使用环境变量 ") + variable);
	} else if (removal_pending) {
		api_key_edit->set_placeholder(AI_L("The saved key will be removed when you press OK", "点击“确定”后将删除已保存的密钥"));
	} else if (has_stored_key) {
		api_key_edit->set_placeholder(AI_L("Saved (leave empty to keep it; type a new key to replace it)", "已保存（留空则保持不变，输入新密钥可替换）"));
	} else {
		api_key_edit->set_placeholder(TR(STR_SETTINGS_API_KEY_PLACEHOLDER));
	}
	clear_key_button->set_disabled(!has_stored_key || removal_pending);
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
	Ref<AIProvider> provider = _make_provider(String());
	provider->set_model(model);
	_set_max_tokens(provider->get_recommended_max_tokens());
}

// =============================================================================
// Model list
// =============================================================================

void AISettingsDialog::_set_fetch_status(const String &p_text, const Color &p_color) {
	fetch_status_label->set_text(p_text);
	fetch_status_label->set_tooltip_text(p_text);
	fetch_status_label->add_theme_color_override("font_color", p_color);
}

void AISettingsDialog::_fetch_models() {
	String api_key = api_key_edit->get_text().strip_edges();
	if (api_key.is_empty()) {
		// Nothing typed: use the stored key or the one from the environment.
		api_key = AICredentialStore::get_api_key(AIProviderCatalog::get_id(current_provider));
	}
	if (api_key.is_empty()) {
		model_list_fetcher->cancel();
		_set_fetch_status(AI_L("Enter an API key to fetch the model list.", "输入 API 密钥后可获取模型列表。"), COLOR_STATUS_MUTED);
		return;
	}
	if (model_list_fetcher->fetch(_make_provider(api_key))) {
		_set_fetch_status(TR(STR_FETCH_FETCHING), COLOR_STATUS_MUTED);
	} else {
		_set_fetch_status(TR(STR_FETCH_FAILED), COLOR_STATUS_WARNING);
	}
}

void AISettingsDialog::_on_models_fetched(const PackedStringArray &p_models, const String &p_best) {
	model_selector->update_models(p_models, p_best);
	_set_fetch_status(AI_L("Connected. Latest: ", "连接正常。最新：") + p_best + " (" + itos(p_models.size()) + AI_L(" models)", " 个模型)"), COLOR_STATUS_OK);

	// The list may have moved the selection to another model, so follow its
	// recommendation — unless the user saved a max-tokens value for this very
	// provider. A value saved for another provider is not carried over: it can
	// exceed this one's output cap and make every request fail.
	if (max_tokens_edited) {
		return; // What the user just entered stands.
	}
	const int saved_max_tokens = AIProviderSetup::get_editor_setting("ai_assistant/max_tokens", -1);
	const bool saved_for_this_provider = String(AIProviderSetup::get_editor_setting("ai_assistant/provider", "")) == AIProviderCatalog::get_id(current_provider);
	_apply_recommended_max_tokens();
	if (saved_for_this_provider && saved_max_tokens > 0 && saved_max_tokens != UNSET_MAX_TOKENS) {
		_set_max_tokens(saved_max_tokens);
	}
}

void AISettingsDialog::_on_models_fetch_failed(const String &p_message) {
	_set_fetch_status(p_message, COLOR_STATUS_WARNING);
}

#endif // TOOLS_ENABLED
