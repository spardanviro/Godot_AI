#pragma once

#ifdef TOOLS_ENABLED

#include "ai_provider.h"

// Builds a ready-to-use provider: which one, which model and endpoint, and
// where its API key comes from.
class AIProviderSetup {
public:
	// From the editor settings; the key from AICredentialStore.
	static Ref<AIProvider> from_editor_settings();
	// From explicit values (headless runs). Empty model/endpoint mean the
	// provider's defaults; an empty key is looked up in AICredentialStore.
	static Ref<AIProvider> from_values(const String &p_provider_id, const String &p_model, const String &p_endpoint, const String &p_api_key);

	// "provider/model", identifying what a request is sent to.
	static String get_model_key(const Ref<AIProvider> &p_provider);
	// One log line describing the provider; never includes the key itself.
	static String describe(const Ref<AIProvider> &p_provider);

	static Variant get_editor_setting(const String &p_name, const Variant &p_default);

	static constexpr double DEFAULT_TEMPERATURE = 0.7;
	// Whether requests carry the temperature from the settings. Off unless
	// the user asked for it: current reasoning models reject the parameter
	// or ignore it, and each provider's default is tuned for its models.
	// Settings from before the switch existed count as "asked for it" if
	// the temperature was ever changed.
	static bool uses_custom_temperature();
};

#endif // TOOLS_ENABLED
