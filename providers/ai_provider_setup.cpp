#ifdef TOOLS_ENABLED

#include "ai_provider_setup.h"
#include "../common/ai_settings.h"

#include "ai_credential_store.h"

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
			AISettings::get("provider", ""),
			AISettings::get("model", ""),
			AISettings::get("api_endpoint", ""),
			String());

	// The output cap follows the model unless the user fixed one.
	// One beyond the model's limit would fail every request.
	if (uses_custom_max_tokens()) {
		const int output_limit = provider->get_model_spec().output;
		const int saved = AISettings::get("max_tokens", provider->get_recommended_max_tokens());
		provider->set_max_tokens(output_limit > 0 ? MIN(saved, output_limit) : saved);
	}
	provider->set_temperature(AISettings::get("temperature", DEFAULT_TEMPERATURE));
	provider->set_send_temperature(uses_custom_temperature());
	provider->set_thinking(AISettings::get("thinking", ""));
	return provider;
}

bool AIProviderSetup::uses_custom_max_tokens() {
	// Absent in settings from before the switch: their value was chosen
	// against model tables that have since gone out of date.
	return AISettings::get("custom_max_tokens", false);
}

bool AIProviderSetup::uses_custom_temperature() {
	if (AISettings::has("custom_temperature")) {
		return AISettings::get("custom_temperature", false);
	}
	const double saved = AISettings::get("temperature", DEFAULT_TEMPERATURE);
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
			", context: " + itos(p_provider->get_model_context_length()) +
			", thinking: " + (p_provider->get_effective_thinking().is_empty() ? String("model default") : p_provider->get_effective_thinking());
}

#endif // TOOLS_ENABLED
