#include "ai_provider_settings.h"

#include "../common/ai_log.h"
#include "../common/ai_settings.h"
#include "ai_credential_store.h"
#include "ai_provider_catalog.h"

void AIProviderSettings::load() {
	forget_unsaved_keys();
	provider_index = AIProviderCatalog::find(AISettings::get("provider", ""));
	endpoints.clear();
	endpoints[get_provider_id()] = AISettings::get("api_endpoint", "");
}

String AIProviderSettings::get_provider_id() const {
	return AIProviderCatalog::get_id(provider_index);
}

void AIProviderSettings::select_provider(int p_index) {
	if (p_index >= 0 && p_index < AIProviderCatalog::get_count()) {
		provider_index = p_index;
	}
}

void AIProviderSettings::set_endpoint(const String &p_endpoint) {
	endpoints[get_provider_id()] = p_endpoint;
}

String AIProviderSettings::get_endpoint() const {
	const String *endpoint = endpoints.getptr(get_provider_id());
	return endpoint ? *endpoint : String();
}

String AIProviderSettings::get_default_endpoint() const {
	return AIProvider::create(get_provider_id())->get_default_endpoint();
}

PackedStringArray AIProviderSettings::get_known_models() const {
	return AIProviderCatalog::get_known_models(provider_index);
}

// =============================================================================
// API key
// =============================================================================

void AIProviderSettings::type_key(const String &p_text) {
	const String provider_id = get_provider_id();
	typed_keys[provider_id] = p_text;
	if (!p_text.strip_edges().is_empty()) {
		keys_to_remove.erase(provider_id); // A new key replaces the old one instead.
	}
}

String AIProviderSettings::get_typed_key() const {
	const String *key = typed_keys.getptr(get_provider_id());
	return key ? *key : String();
}

void AIProviderSettings::request_key_removal() {
	const String provider_id = get_provider_id();
	keys_to_remove.insert(provider_id);
	typed_keys.erase(provider_id);
}

void AIProviderSettings::forget_unsaved_keys() {
	typed_keys.clear();
	keys_to_remove.clear();
}

String AIProviderSettings::get_key_environment_variable() const {
	return AICredentialStore::get_environment_source(get_provider_id());
}

AIProviderSettings::KeyState AIProviderSettings::get_key_state() const {
	const String provider_id = get_provider_id();
	if (!AICredentialStore::get_environment_source(provider_id).is_empty()) {
		return KEY_FROM_ENVIRONMENT; // Takes precedence over the stored key.
	}
	if (keys_to_remove.has(provider_id)) {
		return KEY_REMOVAL_PENDING;
	}
	return AICredentialStore::get_stored_api_key(provider_id).is_empty() ? KEY_NONE : KEY_STORED;
}

bool AIProviderSettings::can_remove_key() const {
	const String provider_id = get_provider_id();
	return !keys_to_remove.has(provider_id) && !AICredentialStore::get_stored_api_key(provider_id).is_empty();
}

String AIProviderSettings::get_usable_key() const {
	const String typed = get_typed_key().strip_edges();
	return typed.is_empty() ? AICredentialStore::get_api_key(get_provider_id()) : typed;
}

// =============================================================================
// What follows
// =============================================================================

Ref<AIProvider> AIProviderSettings::make_provider(const String &p_api_key) const {
	Ref<AIProvider> provider = AIProvider::create(get_provider_id());
	provider->set_api_key(p_api_key);
	const String endpoint = get_endpoint().strip_edges();
	if (!endpoint.is_empty()) {
		provider->set_api_endpoint(endpoint);
	}
	return provider;
}

int AIProviderSettings::get_recommended_max_tokens(const String &p_model) const {
	Ref<AIProvider> provider = make_provider(String());
	provider->set_model(p_model);
	return provider->get_recommended_max_tokens();
}

int AIProviderSettings::get_max_tokens_for_listed_model(const String &p_model) const {
	const int saved = AISettings::get("max_tokens", -1);
	const bool saved_for_this_provider = String(AISettings::get("provider", "")) == get_provider_id();
	if (saved_for_this_provider && saved > 0 && saved != UNSET_MAX_TOKENS) {
		return saved;
	}
	return get_recommended_max_tokens(p_model);
}

void AIProviderSettings::save(const String &p_model, int p_max_tokens) {
	// API keys go to the credential store, never to the settings file. A
	// provider without a typed key keeps the key it has, unless its removal
	// was asked for.
	for (const String &provider_id : keys_to_remove) {
		if (!AICredentialStore::set_stored_api_key(provider_id, String())) {
			AI_ERR("Could not remove the API key for " + provider_id + ".");
		}
	}
	for (const KeyValue<String, String> &typed : typed_keys) {
		const String key = typed.value.strip_edges();
		if (!key.is_empty() && !AICredentialStore::set_stored_api_key(typed.key, key)) {
			AI_ERR("Could not store the API key for " + typed.key + ".");
		}
	}
	forget_unsaved_keys();

	AISettings::set("provider", get_provider_id());
	AISettings::set("model", p_model);
	AISettings::set("api_endpoint", get_endpoint().strip_edges());
	AISettings::set("max_tokens", p_max_tokens);
}
