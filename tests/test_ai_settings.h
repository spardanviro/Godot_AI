#pragma once

#include "../agent/ai_agent_controller.h"
#include "../ui/ai_chat_view.h"
#include "../ui/ai_model_selector.h"
#include "../execution/ai_permission_manager.h"
#include "../providers/ai_provider.h"
#include "../providers/ai_provider_catalog.h"
#include "../network/ai_stream_client.h"

#include "core/templates/hash_set.h"
#include "tests/test_macros.h"

namespace TestAISettings {

// --- Provider catalog ---

TEST_CASE("[AIAssistant][Settings] the catalog lists the providers the factory creates, in the same order") {
	const Vector<String> ids = AIProvider::get_provider_ids();

	REQUIRE(AIProviderCatalog::get_count() == ids.size());
	for (int i = 0; i < ids.size(); i++) {
		CHECK_MESSAGE(AIProviderCatalog::get_id(i) == ids[i], ids[i]);
		CHECK_MESSAGE(AIProviderCatalog::find(ids[i]) == i, ids[i]);
		CHECK_FALSE(AIProviderCatalog::get_display_name(i).is_empty());
	}
}

TEST_CASE("[AIAssistant][Settings] the first model offered for a provider is the one it uses by default") {
	for (int i = 0; i < AIProviderCatalog::get_count(); i++) {
		const String id = AIProviderCatalog::get_id(i);

		const PackedStringArray models = AIProviderCatalog::get_known_models(i);

		REQUIRE_MESSAGE(!models.is_empty(), id);
		CHECK_MESSAGE(models[0] == AIProvider::create(id)->get_default_model(), id);
	}
}

TEST_CASE("[AIAssistant][Settings] an unknown provider falls back to the first one") {
	CHECK(AIProviderCatalog::find("no-such-provider") == 0);
	CHECK(AIProviderCatalog::find("") == 0);
	CHECK(AIProviderCatalog::get_id(-1).is_empty());
	CHECK(AIProviderCatalog::get_id(AIProviderCatalog::get_count()).is_empty());
	CHECK(AIProviderCatalog::get_known_models(99).is_empty());
}

// --- Permission defaults ---

TEST_CASE("[AIAssistant][Settings] by default deleting files is denied, scripts, notes and editor code ask, the rest is allowed") {
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		const AIPermissionManager::Category category = (AIPermissionManager::Category)i;
		AIPermissionManager::Level expected = AIPermissionManager::LEVEL_ALLOW;
		if (category == AIPermissionManager::PERM_FILE_DELETE) {
			expected = AIPermissionManager::LEVEL_DENY;
		} else if (category == AIPermissionManager::PERM_MODIFY_SCRIPTS || category == AIPermissionManager::PERM_REMEMBER || category == AIPermissionManager::PERM_RUN_SCRIPTS) {
			expected = AIPermissionManager::LEVEL_ASK;
		}

		CHECK_MESSAGE(AIPermissionManager::get_default_level(category) == expected, AIPermissionManager::get_setting_key(category));
	}
}

TEST_CASE("[AIAssistant][Settings] every permission has its own editor setting") {
	HashSet<String> keys;
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		const String key = AIPermissionManager::get_setting_key((AIPermissionManager::Category)i);

		CHECK_MESSAGE(key.begins_with("ai_assistant/permissions/"), key);
		keys.insert(key);
	}
	CHECK(keys.size() == AIPermissionManager::PERM_MAX);
}

// --- Model selector ---

TEST_CASE("[AIAssistant][SceneTree][Settings] a new list selects its first model") {
	AIModelSelector *selector = memnew(AIModelSelector);

	selector->set_models({ "alpha", "beta" });

	CHECK(selector->get_model() == "alpha");
	CHECK_FALSE(selector->is_custom());
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a model that is not listed is kept as typed") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models({ "alpha", "beta" });

	selector->select_model("deepseek-ai/DeepSeek-V3");

	CHECK(selector->is_custom());
	CHECK(selector->get_model() == "deepseek-ai/DeepSeek-V3");
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a fetched list keeps the model that was selected") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models({ "alpha", "beta" });
	selector->select_model("beta");

	selector->update_models({ "gamma", "beta", "alpha" }, "gamma");

	CHECK(selector->get_model() == "beta");
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a fetched list without the selected model selects the preferred one") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models({ "retired-model" });

	selector->update_models({ "gamma", "delta" }, "delta");

	CHECK(selector->get_model() == "delta");
	CHECK_FALSE(selector->is_custom());
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a fetched list does not discard a model name the user typed") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models({ "alpha" });
	selector->select_model("my-finetune");

	selector->update_models({ "gamma", "delta" }, "gamma");

	CHECK(selector->is_custom());
	CHECK(selector->get_model() == "my-finetune");
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a fetched list with no recommendation leaves the model alone") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models({ "gpt-5.4" });

	selector->update_models({ "Qwen/Qwen3-Coder", "deepseek-ai/DeepSeek-V3" }, String());

	CHECK(selector->get_model() == "gpt-5.4");
	CHECK(selector->is_custom());
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a fetched list fills a field that was left empty") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models(PackedStringArray());

	selector->update_models({ "gamma", "delta" }, "delta");

	CHECK(selector->get_model() == "delta");
	CHECK(selector->get_models().size() == 2);
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] a typed name that is listed is not custom") {
	AIModelSelector *selector = memnew(AIModelSelector);
	selector->set_models({ "alpha", "beta" });

	selector->select_model("  beta ");

	CHECK(selector->get_model() == "beta");
	CHECK_FALSE(selector->is_custom());
	memdelete(selector);
}

TEST_CASE("[AIAssistant][SceneTree][Settings] an empty list leaves the field empty for a name to be typed") {
	AIModelSelector *selector = memnew(AIModelSelector);

	selector->set_models(PackedStringArray());

	CHECK(selector->is_custom());
	CHECK(selector->get_model().is_empty());
	memdelete(selector);
}

// --- Chat view: messages that arrive while a reply streams in ---

TEST_CASE("[AIAssistant][SceneTree][ChatView] a notice that arrives during the placeholder survives its removal") {
	AIChatView *view = memnew(AIChatView);
	view->begin_stream(false);

	view->append_notice(AIAgentController::NOTICE_INFO, "asset ready");
	view->add_stream_delta("the answer", false); // Removes the placeholder.
	view->end_stream(AIStreamClient::OUTCOME_COMPLETED);

	CHECK(view->get_plain_text().contains("asset ready"));
	memdelete(view);
}

TEST_CASE("[AIAssistant][SceneTree][ChatView] a notice that arrives during the reasoning survives its folding") {
	AIChatView *view = memnew(AIChatView);
	view->begin_stream(true);
	view->add_stream_delta("let me think", true);

	view->append_notice(AIAgentController::NOTICE_WARNING, "the game reported an error");
	view->add_stream_delta("the answer", false); // Folds the reasoning into a link.
	view->end_stream(AIStreamClient::OUTCOME_COMPLETED);

	const String shown = view->get_plain_text();
	CHECK(shown.contains("the game reported an error"));
	CHECK(shown.contains("the answer"));
	CHECK_FALSE(shown.contains("let me think"));
	memdelete(view);
}

TEST_CASE("[AIAssistant][SceneTree][ChatView] a notice during a streamed answer is written after it, not into it") {
	AIChatView *view = memnew(AIChatView);
	view->begin_stream(true);
	view->add_stream_delta("first half, ", false);

	view->append_notice(AIAgentController::NOTICE_INFO, "asset ready");
	view->add_stream_delta("second half", false);
	view->end_stream(AIStreamClient::OUTCOME_COMPLETED);

	const String shown = view->get_plain_text();
	CHECK(shown.contains("first half, second half"));
	CHECK(shown.find("asset ready") > shown.find("second half"));
	memdelete(view);
}

TEST_CASE("[AIAssistant][SceneTree][ChatView] outside a stream a notice is written immediately") {
	AIChatView *view = memnew(AIChatView);

	view->append_notice(AIAgentController::NOTICE_INFO, "ready");

	CHECK(view->get_plain_text().contains("ready"));
	memdelete(view);
}

} // namespace TestAISettings
