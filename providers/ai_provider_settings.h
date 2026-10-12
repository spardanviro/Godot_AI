#pragma once

#include "ai_provider.h"

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

// The provider part of the settings while it is being edited: which provider
// is selected, the endpoint entered for each, the API keys typed or marked
// for removal, and what follows from them (the provider to ask for a model
// list, the output cap to suggest). It has no widgets; the settings dialog
// shows what this holds and tells it what the user did.
//
// Nothing is written until save(). Stored keys are never handed out for
// display: only what was typed in this session can be read back.
class AIProviderSettings {
public:
	enum KeyState {
		KEY_NONE, // No key anywhere.
		KEY_STORED, // One is saved; typing replaces it, empty keeps it.
		KEY_REMOVAL_PENDING, // The saved one goes when the settings are saved.
		KEY_FROM_ENVIRONMENT, // An environment variable supplies it, whatever is saved.
	};

	// The value "max tokens" has before the user (or we) ever changed it.
	static constexpr int UNSET_MAX_TOKENS = 4096;

	// Starts over from what is saved. Unsaved keys are dropped.
	void load();

	// --- Provider and endpoint ---
	int get_provider_index() const { return provider_index; } // Into AIProviderCatalog.
	String get_provider_id() const;
	// Switches to another provider. Each keeps the endpoint entered for it.
	void select_provider(int p_index);
	void set_endpoint(const String &p_endpoint);
	String get_endpoint() const;
	String get_default_endpoint() const;
	PackedStringArray get_known_models() const;

	// --- API key of the selected provider ---
	void type_key(const String &p_text);
	String get_typed_key() const;
	void request_key_removal();
	void forget_unsaved_keys();
	KeyState get_key_state() const;
	// The variable that supplies the key in KEY_FROM_ENVIRONMENT.
	String get_key_environment_variable() const;
	bool can_remove_key() const;
	// The key a request would be sent with now: the typed one, else the saved
	// one or the one from the environment. Empty if there is none.
	String get_usable_key() const;

	// --- What follows ---
	// The selected provider with the entered endpoint, for asking it something.
	Ref<AIProvider> make_provider(const String &p_api_key) const;
	// The output cap the provider recommends for p_model.
	int get_recommended_max_tokens(const String &p_model) const;
	// The output cap to show once a model list has moved the selection to
	// p_model: the saved value if it was saved for this very provider (one
	// saved for another can exceed this provider's limit and fail every
	// request), else the recommendation.
	int get_max_tokens_for_listed_model(const String &p_model) const;

	// Stores typed keys, removes the ones marked for it, and writes the
	// provider, its endpoint, the model and the output cap. The caller saves
	// the settings file.
	void save(const String &p_model, int p_max_tokens);

private:
	int provider_index = 0;
	HashMap<String, String> typed_keys; // By provider id.
	HashMap<String, String> endpoints; // By provider id.
	HashSet<String> keys_to_remove;
};
