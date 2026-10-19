#pragma once

#include "../common/ai_settings.h"
#include "../execution/ai_code_fixes.h"
#include "../execution/ai_permission_manager.h"
#include "../execution/ai_safety_policy.h"
#include "../execution/ai_script_wrapper.h"
#include "../network/ai_proxy.h"
#include "../providers/ai_credential_store.h"
#include "../providers/ai_provider_catalog.h"
#include "../providers/ai_provider_settings.h"
#include "../providers/ai_provider_setup.h"

#include "core/io/dir_access.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestAISettingsModel {

// Settings given for the length of a test, and a credential store in a
// scratch file, so nothing touches the user's configuration.
struct Fixture {
	String store_path;

	explicit Fixture(const Dictionary &p_settings = Dictionary()) {
		AISettings::set_overrides_for_tests(p_settings);
		store_path = TestUtils::get_temp_path("ai_settings_model_credentials.json");
		DirAccess::remove_absolute(store_path);
		AICredentialStore::set_storage_path_override(store_path);
	}
	~Fixture() {
		AISettings::clear_overrides_for_tests();
		DirAccess::remove_absolute(store_path);
		AICredentialStore::set_storage_path_override(String());
	}
};

static Dictionary _settings(const String &p_provider, const String &p_endpoint = String(), int p_max_tokens = -1) {
	Dictionary settings;
	settings["provider"] = p_provider;
	settings["api_endpoint"] = p_endpoint;
	if (p_max_tokens > 0) {
		settings["max_tokens"] = p_max_tokens;
	}
	return settings;
}

// --- AISettings ---

TEST_CASE("[AIAssistant][SettingsModel] settings are read by short or full name, with a default when absent") {
	Dictionary given;
	given["autorun"] = false;
	given["ai_assistant/language"] = "zh";
	Fixture fixture(given);

	CHECK(bool(AISettings::get("autorun", true)) == false);
	CHECK(bool(AISettings::get("ai_assistant/autorun", true)) == false);
	CHECK(String(AISettings::get("language", "en")) == "zh");
	CHECK(int(AISettings::get("max_tokens", 123)) == 123);
	CHECK(AISettings::has("autorun"));
	CHECK_FALSE(AISettings::has("max_tokens"));
}

TEST_CASE("[AIAssistant][SettingsModel] a write is read back, and gone once the overrides are cleared") {
	{
		Fixture fixture;
		AISettings::set("image_model", "gpt-image-1");
		CHECK(String(AISettings::get("image_model", "")) == "gpt-image-1");
	}
	CHECK(String(AISettings::get("image_model", "none")) != "gpt-image-1");
}

TEST_CASE("[AIAssistant][SettingsModel] what reads settings in the lower layers follows them") {
	Dictionary given;
	given["proxy_mode"] = AIProxy::MODE_CUSTOM;
	given["proxy_address"] = "10.1.2.3:8080";
	given[AIPermissionManager::get_setting_key(AIPermissionManager::PERM_FILE_DELETE)] = (int)AIPermissionManager::LEVEL_ALLOW;
	given["provider"] = "deepseek";
	given["model"] = "deepseek-reasoner";
	given["temperature"] = 0.2;
	Fixture fixture(given);

	const AIProxyConfig proxy = AIProxy::get_configured();
	Ref<AIPermissionManager> permissions;
	permissions.instantiate();
	permissions->load_from_settings();
	Ref<AIProvider> provider = AIProviderSetup::from_editor_settings();

	CHECK(proxy.to_text() == "10.1.2.3:8080");
	CHECK(permissions->get_permission(AIPermissionManager::PERM_FILE_DELETE) == AIPermissionManager::LEVEL_ALLOW);
	CHECK(provider->get_provider_name() == "deepseek");
	CHECK(provider->get_model() == "deepseek-reasoner");
	// A temperature that was changed counts as asked for.
	CHECK(AIProviderSetup::uses_custom_temperature());
}

// --- AIProviderSettings ---

TEST_CASE("[AIAssistant][SettingsModel] each provider keeps the endpoint entered for it") {
	Fixture fixture(_settings("openai", "https://relay.example.com/v1"));
	AIProviderSettings settings;
	settings.load();
	const int glm = AIProviderCatalog::find("glm");
	const int openai = AIProviderCatalog::find("openai");

	settings.select_provider(glm);
	const String glm_endpoint_at_first = settings.get_endpoint();
	settings.set_endpoint("https://glm.example.com");
	settings.select_provider(openai);

	CHECK(glm_endpoint_at_first.is_empty());
	CHECK(settings.get_endpoint() == "https://relay.example.com/v1");
	CHECK(settings.make_provider("k")->get_models_list_url() == "https://relay.example.com/v1/models");
	settings.select_provider(glm);
	CHECK(settings.get_endpoint() == "https://glm.example.com");
}

TEST_CASE("[AIAssistant][SettingsModel] a typed key is used at once but stored only on save") {
	Fixture fixture(_settings("openai"));
	AIProviderSettings settings;
	settings.load();

	settings.type_key("  sk-typed  ");

	CHECK(settings.get_usable_key() == "sk-typed");
	CHECK(AICredentialStore::get_stored_api_key("openai").is_empty());
	settings.save("gpt-5.4", 16384);
	CHECK(AICredentialStore::get_stored_api_key("openai") == "sk-typed");
	// Nothing typed is left behind to be read back.
	CHECK(settings.get_typed_key().is_empty());
	CHECK(String(AISettings::get("model", "")) == "gpt-5.4");
	CHECK(int(AISettings::get("max_tokens", 0)) == 16384);
}

TEST_CASE("[AIAssistant][SettingsModel] removing a key waits for save, and a new key cancels the removal") {
	Fixture fixture(_settings("glm"));
	AICredentialStore::set_stored_api_key("glm", "stored-key");
	AIProviderSettings settings;
	settings.load();
	const AIProviderSettings::KeyState at_first = settings.get_key_state();

	settings.request_key_removal();
	const AIProviderSettings::KeyState pending = settings.get_key_state();
	const bool removable_while_pending = settings.can_remove_key();
	const String still_stored = AICredentialStore::get_stored_api_key("glm");
	settings.type_key("replacement");
	const AIProviderSettings::KeyState after_typing = settings.get_key_state();
	settings.save("glm-5", 8192);

	CHECK(at_first == AIProviderSettings::KEY_STORED);
	CHECK(pending == AIProviderSettings::KEY_REMOVAL_PENDING);
	CHECK_FALSE(removable_while_pending);
	CHECK(still_stored == "stored-key");
	CHECK(after_typing == AIProviderSettings::KEY_STORED);
	CHECK(AICredentialStore::get_stored_api_key("glm") == "replacement");
}

TEST_CASE("[AIAssistant][SettingsModel] a removal that is saved empties the store; cancelling leaves it") {
	Fixture fixture(_settings("glm"));
	AICredentialStore::set_stored_api_key("glm", "stored-key");
	AIProviderSettings cancelled;
	cancelled.load();
	cancelled.request_key_removal();
	cancelled.forget_unsaved_keys();
	const String after_cancel = AICredentialStore::get_stored_api_key("glm");

	AIProviderSettings saved;
	saved.load();
	saved.request_key_removal();
	saved.save("glm-5", 8192);

	CHECK(after_cancel == "stored-key");
	CHECK(AICredentialStore::get_stored_api_key("glm").is_empty());
	CHECK(saved.get_key_state() == AIProviderSettings::KEY_NONE);
}

// --- The executor's parts ---

TEST_CASE("[AIAssistant][SettingsModel] the wrapper's header is as long as it says") {
	const PackedStringArray lines = AIScriptWrapper::wrap("var a = 1\nif a:\n    print(a)").split("\n");

	REQUIRE(lines.size() > AIScriptWrapper::HEADER_LINES + 2);
	CHECK(lines[AIScriptWrapper::HEADER_LINES - 1] == "func _run():");
	CHECK(lines[AIScriptWrapper::HEADER_LINES] == "\tvar a = 1");
	// Space indentation becomes tabs, under the wrapper's own.
	CHECK(lines[AIScriptWrapper::HEADER_LINES + 2] == "\t\tprint(a)");
}

TEST_CASE("[AIAssistant][SettingsModel] the policy and the fixes work without an executor") {
	CHECK(AISafetyPolicy::check("var n = Node.new()\nn.name = \"A\"").is_empty());
	CHECK_FALSE(AISafetyPolicy::check("OS.execute(\"cmd\", [])").is_empty());
	CHECK_FALSE(AISafetyPolicy::check("var s = \"ai_assistant/api_key\"").is_empty());

	const String fixed = AICodeFixes::apply("var root = get_scene()");
	CHECK(fixed.contains("EditorInterface.get_edited_scene_root()"));
	CHECK(AICodeFixes::apply("var a = 1") == "var a = 1");
}

} // namespace TestAISettingsModel
