#ifdef TOOLS_ENABLED

#include "ai_provider_setup.h"

#include "ai_credential_store.h"

#include "editor/settings/editor_settings.h"

// The value "max tokens" has before the user (or we) ever changed it.
static const int UNSET_MAX_TOKENS = 4096;

Variant AIProviderSetup::get_editor_setting(const String &p_name, const Variant &p_default) {
	EditorSettings *es = EditorSettings::get_singleton();
	return es && es->has_setting(p_name) ? es->get_setting(p_name) : p_default;
}

Ref<AIProvider> AIProviderSetup::from_values(const String &p_provider_id, const String &p_model, const String &p_endpoint, const String &p_api_key) {
	Ref<AIProvider> provider = AIProvider::create(p_provider_id);
	provider->set_api_key(p_api_key.is_empty() ? AICredentialStore::get_api_key(provider->get_provider_name()) : p_api_key);
	if (!p_model.is_empty()) {
		provider->set_model(p_model);
	}
	if (!p_endpoint.is_empty()) {
		provider->set_api_endpoint(p_endpoint);
	}
	provider->set_max_tokens(provider->get_recommended_max_tokens());
	return provider;
}

Ref<AIProvider> AIProviderSetup::from_editor_settings() {
	Ref<AIProvider> provider = from_values(
			get_editor_setting("ai_assistant/provider", ""),
			get_editor_setting("ai_assistant/model", ""),
			get_editor_setting("ai_assistant/api_endpoint", ""),
			String());

	// An untouched max_tokens follows the model's recommendation, and the
	// settings are updated so the dialog shows what is actually used.
	int max_tokens = get_editor_setting("ai_assistant/max_tokens", UNSET_MAX_TOKENS);
	const int recommended = provider->get_recommended_max_tokens();
	if (max_tokens == UNSET_MAX_TOKENS && recommended != UNSET_MAX_TOKENS) {
		max_tokens = recommended;
		if (EditorSettings::get_singleton()) {
			EditorSettings::get_singleton()->set_setting("ai_assistant/max_tokens", max_tokens);
		}
	}
	provider->set_max_tokens(max_tokens);
	provider->set_temperature(get_editor_setting("ai_assistant/temperature", DEFAULT_TEMPERATURE));
	provider->set_send_temperature(uses_custom_temperature());
	return provider;
}

bool AIProviderSetup::uses_custom_temperature() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es && es->has_setting("ai_assistant/custom_temperature")) {
		return es->get_setting("ai_assistant/custom_temperature");
	}
	const double saved = get_editor_setting("ai_assistant/temperature", DEFAULT_TEMPERATURE);
	return !Math::is_equal_approx(saved, DEFAULT_TEMPERATURE);
}

String AIProviderSetup::get_model_key(const Ref<AIProvider> &p_provider) {
	if (p_provider.is_null()) {
		return String();
	}
	const String model = p_provider->get_model().is_empty() ? p_provider->get_default_model() : p_provider->get_model();
	return p_provider->get_provider_name() + "/" + model;
}

String AIProviderSetup::describe(const Ref<AIProvider> &p_provider) {
	String key_origin = "(not set)";
	if (!p_provider->get_api_key().is_empty()) {
		const String variable = AICredentialStore::get_environment_source(p_provider->get_provider_name());
		key_origin = variable.is_empty() ? String("configured") : "environment (" + variable + ")";
	}
	return get_model_key(p_provider) + ", key: " + key_origin + ", max tokens: " + itos(p_provider->get_max_tokens()) +
			", context: " + itos(p_provider->get_model_context_length());
}

#endif // TOOLS_ENABLED
