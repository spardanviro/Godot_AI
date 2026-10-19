#pragma once

#include "../agent/ai_session.h"
#include "../common/ai_settings.h"
#include "../tools/ai_toolbox.h"
#include "../providers/ai_model_specs.h"
#include "../providers/ai_provider.h"
#include "../providers/ai_provider_catalog.h"
#include "../providers/ai_provider_setup.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestAIModelSpecs {

// Facts in the shape of data/model_specs.json, made up for the tests so they
// do not change when the real data is refreshed.
static const char *SPECS = R"({
"deepseek": {"models": {
	"deepseek-v9": {"limit": {"context": 1000000, "output": 400000}, "temperature": true, "tool_call": true, "release_date": "2026-09-01",
		"reasoning_options": [{"type": "toggle"}, {"type": "effort", "values": ["low", "high", "max"]}]},
	"deepseek-old": {"limit": {"context": 128000, "output": 8000}, "tool_call": true, "status": "deprecated", "release_date": "2025-01-01"}
}},
"zhipuai-coding-plan": {"models": {
	"glm-9": {"limit": {"context": 200000, "output": 131072}, "tool_call": true, "release_date": "2026-08-01",
		"reasoning_options": [{"type": "effort", "values": ["low", "high", "max"]}]}
}},
"zhipuai": {"models": {
	"glm-9": {"limit": {"context": 1, "output": 1}, "tool_call": true},
	"glm-8": {"limit": {"context": 200000, "output": 131072}, "tool_call": true, "release_date": "2026-03-01", "reasoning_options": [{"type": "toggle"}]}
}},
"anthropic": {"models": {
	"claude-test-9": {"limit": {"context": 1000000, "output": 128000}, "temperature": false, "tool_call": true, "release_date": "2026-09-22", "family": "claude-opus",
		"reasoning_options": [{"type": "effort", "values": ["low", "medium", "high", "xhigh", "max"]}]},
	"claude-test-9-20260922": {"limit": {"context": 1000000, "output": 128000}, "tool_call": true, "release_date": "2026-09-22"},
	"claude-budget-4": {"limit": {"context": 200000, "output": 64000}, "tool_call": true, "release_date": "2025-10-01", "family": "claude-opus",
		"reasoning_options": [{"type": "budget_tokens", "min": 1024}]}
}},
"openai": {"models": {
	"gpt-9": {"limit": {"context": 400000, "output": 128000}, "temperature": false, "tool_call": true, "release_date": "2026-10-01",
		"reasoning_options": [{"type": "effort", "values": ["none", "low", "medium", "high", "xhigh"]}]},
	"text-embedding-9": {"limit": {"context": 8000, "output": 0}, "release_date": "2026-10-02"}
}},
"google": {"models": {
	"gemini-9-flash": {"limit": {"context": 1048576, "output": 65536}, "temperature": true, "tool_call": true, "release_date": "2026-10-01",
		"reasoning_options": [{"type": "effort", "values": ["minimal", "low", "medium", "high"]}]},
	"gemini-8-flash": {"limit": {"context": 1048576, "output": 65536}, "tool_call": true, "release_date": "2025-06-01",
		"reasoning_options": [{"type": "toggle"}, {"type": "budget_tokens", "min": 0, "max": 24576}]}
}}
})";

// Uses SPECS for the length of a test, the built-in data after it.
struct SpecsFixture {
	SpecsFixture() { REQUIRE(AIModelSpecs::use(SPECS)); }
	~SpecsFixture() { AIModelSpecs::use_builtin(); }
};

static Dictionary _body(const Ref<AIProvider> &p_provider) {
	return JSON::parse_string(p_provider->build_stream_request_body("system", Array(), "hi"));
}

static Ref<AIProvider> _provider(const String &p_id, const String &p_model, const String &p_thinking = String()) {
	Ref<AIProvider> provider = AIProvider::create(p_id);
	provider->set_model(p_model);
	provider->set_thinking(p_thinking);
	return provider;
}

// --- Facts ---

TEST_CASE("[AIAssistant][ModelSpecs] the built-in data knows the current models of every provider") {
	for (int i = 0; i < AIProviderCatalog::get_count(); i++) {
		CHECK_MESSAGE(!AIModelSpecs::get_models(AIProviderCatalog::get_id(i), 5).is_empty(), AIProviderCatalog::get_id(i));
	}
	const AIModelSpec spec = AIModelSpecs::find("deepseek", "deepseek-v4-pro");
	CHECK(spec.found);
	CHECK(spec.context >= 1000000);
}

TEST_CASE("[AIAssistant][ModelSpecs] a model is found by its provider's name for it, with or without a vendor prefix") {
	SpecsFixture fixture;

	const AIModelSpec plain = AIModelSpecs::find("deepseek", "deepseek-v9");
	const AIModelSpec prefixed = AIModelSpecs::find("deepseek", "deepseek-ai/DeepSeek-V9");
	const AIModelSpec through_relay = AIModelSpecs::find("openai", "deepseek-v9"); // Listed by another provider only.
	const AIModelSpec unknown = AIModelSpecs::find("deepseek", "no-such-model");

	CHECK(plain.found);
	CHECK(plain.context == 1000000);
	CHECK(plain.output == 400000);
	CHECK(plain.can_turn_off_thinking);
	CHECK(plain.efforts == PackedStringArray({ "low", "high", "max" }));
	CHECK(prefixed.output == 400000);
	CHECK(through_relay.output == 400000);
	CHECK_FALSE(unknown.found);
}

TEST_CASE("[AIAssistant][ModelSpecs] GLM reads the coding plan's entry before the general one") {
	SpecsFixture fixture;

	CHECK(AIModelSpecs::find("glm", "glm-9").context == 200000);
	CHECK(AIModelSpecs::find("glm", "glm-8").can_turn_off_thinking);
}

TEST_CASE("[AIAssistant][ModelSpecs] the models offered are current tool-calling ones, newest first") {
	SpecsFixture fixture;

	const PackedStringArray anthropic = AIModelSpecs::get_models("anthropic", 10);
	const PackedStringArray deepseek = AIModelSpecs::get_models("deepseek", 10);
	const PackedStringArray openai = AIModelSpecs::get_models("openai", 10);
	const PackedStringArray glm = AIModelSpecs::get_models("glm", 10);

	CHECK(anthropic == PackedStringArray({ "claude-test-9", "claude-budget-4" })); // No dated snapshot.
	CHECK(deepseek == PackedStringArray({ "deepseek-v9" })); // No deprecated model.
	CHECK(openai == PackedStringArray({ "gpt-9" })); // No embedding model.
	CHECK(glm == PackedStringArray({ "glm-9", "glm-8" })); // Each once.
	CHECK(AIModelSpecs::get_models("deepseek", 0).is_empty());
}

TEST_CASE("[AIAssistant][ModelSpecs] the download is cut to what the editor keeps, and junk is refused") {
	SpecsFixture fixture;
	const String download = R"({"deepseek": {"id": "deepseek", "env": ["KEY"], "models": {"deepseek-v9": {"id": "deepseek-v9", "cost": {"input": 1},
		"limit": {"context": 1000, "output": 500, "input": 900}, "tool_call": true, "reasoning_options": [{"type": "toggle"}]}}},
		"someone-else": {"models": {"x": {"limit": {"context": 1}}}}})";

	const String kept = AIModelSpecs::filter(download);
	const Dictionary parsed = JSON::parse_string(kept);
	const Dictionary model = Dictionary(Dictionary(Dictionary(parsed["deepseek"])["models"])["deepseek-v9"]);

	CHECK(parsed.size() == 1);
	CHECK_FALSE(model.has("cost"));
	CHECK_FALSE(model.has("id"));
	CHECK(Dictionary(model["limit"]).size() == 2);
	CHECK(AIModelSpecs::filter("not json").is_empty());
	CHECK(AIModelSpecs::filter("{\"other\": {}}").is_empty());
	CHECK_FALSE(AIModelSpecs::use("{\"other\": {}}"));
	CHECK(AIModelSpecs::find("deepseek", "deepseek-v9").output == 400000); // Unchanged by the refusal.
	REQUIRE(AIModelSpecs::use(kept));
	CHECK(AIModelSpecs::find("deepseek", "deepseek-v9").output == 500);
}

// --- What a provider makes of them ---

TEST_CASE("[AIAssistant][ModelSpecs] the output cap is the model's, unless it would crowd out the context") {
	SpecsFixture fixture;

	CHECK(_provider("deepseek", "deepseek-v9")->get_recommended_max_tokens() == 250000); // A quarter of 1M.
	CHECK(_provider("anthropic", "claude-test-9")->get_recommended_max_tokens() == 128000);
	CHECK(_provider("glm", "glm-9")->get_recommended_max_tokens() == 50000);
	CHECK(_provider("deepseek", "deepseek-v9")->get_model_context_length() == 1000000);
	// Not listed: the provider's own table.
	CHECK(_provider("deepseek", "deepseek-reasoner")->get_model_context_length() == 128000);
	CHECK(_provider("deepseek", "deepseek-reasoner")->get_recommended_max_tokens() == 32768);
}

TEST_CASE("[AIAssistant][ModelSpecs] the thinking options are the model's own") {
	SpecsFixture fixture;

	CHECK(_provider("deepseek", "deepseek-v9")->get_thinking_options() == PackedStringArray({ "off", "low", "high", "max" }));
	CHECK(_provider("anthropic", "claude-test-9")->get_thinking_options() == PackedStringArray({ "low", "medium", "high", "xhigh", "max" }));
	CHECK(_provider("openai", "gpt-9")->get_thinking_options() == PackedStringArray({ "none", "low", "medium", "high", "xhigh" }));
	// A model that takes a budget gets budgets, within its bounds.
	CHECK(_provider("anthropic", "claude-budget-4")->get_thinking_options() == PackedStringArray({ "4096", "16384", "32768" }));
	CHECK(_provider("gemini", "gemini-8-flash")->get_thinking_options() == PackedStringArray({ "off", "4096", "16384" }));
	CHECK(_provider("deepseek", "no-such-model")->get_thinking_options().is_empty());
	// A choice the model does not offer is not sent.
	CHECK(_provider("anthropic", "claude-test-9", "off")->get_effective_thinking().is_empty());
	CHECK(_provider("anthropic", "claude-test-9", "xhigh")->get_effective_thinking() == "xhigh");
}

TEST_CASE("[AIAssistant][ModelSpecs] each provider sends the choice in its own words") {
	SpecsFixture fixture;

	const Dictionary deepseek = _body(_provider("deepseek", "deepseek-v9", "max"));
	const Dictionary deepseek_off = _body(_provider("deepseek", "deepseek-v9", "off"));
	const Dictionary glm = _body(_provider("glm", "glm-9", "low"));
	const Dictionary openai = _body(_provider("openai", "gpt-9", "none"));
	const Dictionary anthropic = _body(_provider("anthropic", "claude-test-9", "xhigh"));
	const Dictionary gemini = _body(_provider("gemini", "gemini-9-flash", "minimal"));
	const Dictionary gemini_off = _body(_provider("gemini", "gemini-8-flash", "off"));
	const Dictionary by_default = _body(_provider("deepseek", "deepseek-v9"));

	CHECK(String(deepseek["reasoning_effort"]) == "max");
	CHECK(String(Dictionary(deepseek["thinking"])["type"]) == "enabled");
	CHECK(String(Dictionary(deepseek_off["thinking"])["type"]) == "disabled");
	CHECK_FALSE(deepseek_off.has("reasoning_effort"));
	CHECK(String(glm["reasoning_effort"]) == "low");
	CHECK(String(openai["reasoning_effort"]) == "none");
	CHECK_FALSE(openai.has("thinking"));
	CHECK(String(Dictionary(anthropic["output_config"])["effort"]) == "xhigh");
	CHECK_FALSE(anthropic.has("thinking"));
	const Dictionary gemini_config = Dictionary(gemini["generationConfig"])["thinkingConfig"];
	CHECK(String(gemini_config["thinkingLevel"]) == "minimal");
	CHECK(int(Dictionary(Dictionary(gemini_off["generationConfig"])["thinkingConfig"])["thinkingBudget"]) == 0);
	CHECK_FALSE(by_default.has("thinking"));
	CHECK_FALSE(by_default.has("reasoning_effort"));
}

TEST_CASE("[AIAssistant][ModelSpecs] a temperature goes only to a model that takes one") {
	SpecsFixture fixture;
	Ref<AIProvider> takes = _provider("deepseek", "deepseek-v9");
	Ref<AIProvider> refuses = _provider("openai", "gpt-9");
	takes->set_send_temperature(true);
	refuses->set_send_temperature(true);

	CHECK(_body(takes).has("temperature"));
	CHECK_FALSE(_body(refuses).has("temperature"));
	CHECK_FALSE(refuses->supports_temperature());
	CHECK(_provider("openai", "no-such-model")->supports_temperature()); // Unknown: the user decides.
}

TEST_CASE("[AIAssistant][ModelSpecs] reasoning goes back only while thinking is on") {
	SpecsFixture fixture;

	CHECK(_provider("deepseek", "deepseek-v9")->wants_reasoning_back());
	CHECK_FALSE(_provider("deepseek", "deepseek-v9", "off")->wants_reasoning_back());
	CHECK(_provider("glm", "glm-9")->wants_reasoning_back());
	CHECK_FALSE(_provider("glm", "glm-8", "off")->wants_reasoning_back());
	CHECK_FALSE(_provider("deepseek", "deepseek-chat")->wants_reasoning_back());
	CHECK_FALSE(_provider("openai", "gpt-9")->wants_reasoning_back());
}

// --- Settings ---

TEST_CASE("[AIAssistant][ModelSpecs] the output cap follows the model unless a custom one is set") {
	SpecsFixture fixture;
	Dictionary automatic;
	automatic["provider"] = "deepseek";
	automatic["model"] = "deepseek-v9";
	automatic["max_tokens"] = 37888; // Saved before the switch existed.
	automatic["thinking"] = "high";
	Dictionary custom = automatic.duplicate();
	custom["custom_max_tokens"] = true;

	AISettings::set_overrides_for_tests(automatic);
	const Ref<AIProvider> from_automatic = AIProviderSetup::from_editor_settings();
	AISettings::set_overrides_for_tests(custom);
	const Ref<AIProvider> from_custom = AIProviderSetup::from_editor_settings();
	AISettings::clear_overrides_for_tests();

	CHECK(from_automatic->get_max_tokens() == 250000);
	CHECK(from_custom->get_max_tokens() == 37888);
	CHECK(from_automatic->get_effective_thinking() == "high");
}

TEST_CASE("[AIAssistant][ModelSpecs] a custom output cap never exceeds the model's limit") {
	SpecsFixture fixture;
	Dictionary given;
	given["provider"] = "anthropic";
	given["model"] = "claude-budget-4"; // Writes 64000 at most.
	given["custom_max_tokens"] = true;
	given["max_tokens"] = 200000;

	AISettings::set_overrides_for_tests(given);
	const Ref<AIProvider> provider = AIProviderSetup::from_editor_settings();
	AISettings::clear_overrides_for_tests();

	CHECK(provider->get_max_tokens() == 64000);
}

TEST_CASE("[AIAssistant][ModelSpecs] a side request without streaming keeps the model's default thinking") {
	SpecsFixture fixture;
	Ref<AIProvider> claude = _provider("anthropic", "claude-budget-4", "32768");
	claude->set_max_tokens(64000);

	const Dictionary deepseek = JSON::parse_string(_provider("deepseek", "deepseek-v9", "low")->build_request_body("system", Array(), "hi"));
	const Dictionary anthropic = JSON::parse_string(claude->build_request_body("system", Array(), "hi"));

	// The history summary caps its output far below a thinking budget.
	CHECK_FALSE(deepseek.has("reasoning_effort"));
	CHECK_FALSE(anthropic.has("thinking"));
	CHECK_FALSE(anthropic.has("output_config"));
}

TEST_CASE("[AIAssistant][ModelSpecs] a budget goes to Claude and Gemini as a budget") {
	SpecsFixture fixture;
	Ref<AIProvider> claude = _provider("anthropic", "claude-budget-4", "16384");
	claude->set_max_tokens(64000);
	Ref<AIProvider> small = _provider("anthropic", "claude-budget-4", "32768");
	small->set_max_tokens(8192);

	const Dictionary thinking = _body(claude)["thinking"];
	const Dictionary squeezed = _body(small)["thinking"];
	const Dictionary gemini = Dictionary(Dictionary(_body(_provider("gemini", "gemini-8-flash", "4096"))["generationConfig"])["thinkingConfig"]);

	CHECK(String(thinking["type"]) == "enabled");
	CHECK(int(thinking["budget_tokens"]) == 16384);
	CHECK(int(squeezed["budget_tokens"]) == 8192 - 1024); // Under the output cap.
	CHECK(int(gemini["thinkingBudget"]) == 4096);
}

TEST_CASE("[AIAssistant][ModelSpecs] the default model is the newest of the provider's flagship series") {
	SpecsFixture fixture;

	CHECK(AIModelSpecs::get_newest("anthropic", { "claude-opus" }) == "claude-test-9"); // Not its dated snapshot.
	CHECK(AIProvider::create("anthropic")->get_default_model() == "claude-test-9");
	CHECK(AIModelSpecs::get_newest("anthropic", { "no-such-series" }).is_empty());
	CHECK(AIProvider::create("openai")->get_default_model() == "gpt-6.1-sol"); // Nothing in its series: the fallback.
}

TEST_CASE("[AIAssistant][ModelSpecs] the built-in data gives every provider a current default model") {
	for (int i = 0; i < AIProviderCatalog::get_count(); i++) {
		const Ref<AIProvider> provider = AIProvider::create(AIProviderCatalog::get_id(i));
		const AIModelSpec spec = AIModelSpecs::find(provider->get_provider_name(), provider->get_default_model());
		CHECK_MESSAGE(spec.found, provider->get_default_model());
		CHECK_MESSAGE(!spec.deprecated, provider->get_default_model());
	}
}

// --- Claude's thinking in a tool loop ---

static Array _tool_loop_history(bool p_with_reasoning) {
	AISession session;
	session.push_user("earlier");
	Vector<AIToolCall> earlier_calls;
	AIToolCall earlier;
	earlier.id = "call_0";
	earlier.name = "get_scene_tree";
	earlier.arguments = "{}";
	earlier_calls.push_back(earlier);
	session.push_assistant_tool_calls("", "old thought", earlier_calls, "sig-old");
	session.push_tool_result(earlier, "tree");
	session.push_assistant("done");
	session.push_user("now");
	Vector<AIToolCall> calls;
	AIToolCall call;
	call.id = "call_1";
	call.name = "get_scene_tree";
	call.arguments = "{}";
	calls.push_back(call);
	session.push_assistant_tool_calls("", "new thought", calls, "sig-new");
	session.push_tool_result(call, "tree");
	return session.build_api_messages(true, p_with_reasoning);
}

TEST_CASE("[AIAssistant][ModelSpecs] Claude gets the thinking of the current tool loop back, signed") {
	SpecsFixture fixture;
	Ref<AIProvider> claude = _provider("anthropic", "claude-test-9");
	claude->set_tools(AIToolbox().get_specs(true));
	REQUIRE(claude->wants_reasoning_back());

	const Dictionary body = JSON::parse_string(claude->build_stream_request_body("system", _tool_loop_history(true), ""));
	const Array messages = body["messages"];
	const String headers = String("\n").join(claude->get_headers());

	// earlier user, assistant (calls), tool results, assistant, now, assistant (calls), tool results.
	REQUIRE(messages.size() == 7);
	const Array old_blocks = Dictionary(messages[1])["content"];
	const Array new_blocks = Dictionary(messages[5])["content"];
	CHECK(String(Dictionary(old_blocks[0])["type"]) == "tool_use"); // Before the last user message: left out.
	CHECK(String(Dictionary(new_blocks[0])["type"]) == "thinking");
	CHECK(String(Dictionary(new_blocks[0])["signature"]) == "sig-new");
	CHECK(String(Dictionary(new_blocks[0])["thinking"]) == "new thought");
	CHECK(String(Dictionary(new_blocks[1])["type"]) == "tool_use");
	CHECK(String(Dictionary(Dictionary(body["thinking"])["block_binding"])["prefix_mismatch_behavior"]) == "drop_block");
	CHECK(headers.contains("thinking-binding-controls-2026-08-01"));
	for (int i = 0; i < messages.size(); i++) {
		CHECK_FALSE(Dictionary(messages[i]).has("thinking"));
		CHECK_FALSE(Dictionary(messages[i]).has("thinking_signature"));
	}
}

TEST_CASE("[AIAssistant][ModelSpecs] a new user message starts without the earlier loop's thinking") {
	SpecsFixture fixture;
	Ref<AIProvider> claude = _provider("anthropic", "claude-test-9");
	claude->set_tools(AIToolbox().get_specs(true));

	const Dictionary body = JSON::parse_string(claude->build_stream_request_body("system", _tool_loop_history(true), "next question"));
	const Array new_blocks = Dictionary(Array(body["messages"])[5])["content"];

	CHECK(String(Dictionary(new_blocks[0])["type"]) == "tool_use");
	CHECK_FALSE(body.has("thinking"));
	CHECK_FALSE(String("\n").join(claude->get_headers()).contains("anthropic-beta"));
}

TEST_CASE("[AIAssistant][ModelSpecs] with a budget, a loop whose replies lack thinking goes without it") {
	SpecsFixture fixture;
	Ref<AIProvider> claude = _provider("anthropic", "claude-budget-4", "4096");
	claude->set_max_tokens(64000);
	claude->set_tools(AIToolbox().get_specs(true));
	AISession session;
	session.push_user("now");
	Vector<AIToolCall> calls;
	AIToolCall call;
	call.id = "call_1";
	call.name = "get_scene_tree";
	call.arguments = "{}";
	calls.push_back(call);
	session.push_assistant_tool_calls("", "", calls); // No signature came with it.
	session.push_tool_result(call, "tree");

	const Dictionary body = JSON::parse_string(claude->build_stream_request_body("system", session.build_api_messages(true, true), ""));

	CHECK_FALSE(body.has("thinking")); // A budget would demand a thinking block it does not have.
}

TEST_CASE("[AIAssistant][ModelSpecs] once a request with Claude's thinking is refused, it is not sent again") {
	SpecsFixture fixture;
	Ref<AIProvider> claude = _provider("anthropic", "claude-test-9");
	claude->set_tools(AIToolbox().get_specs(true));
	const bool before_any = claude->stop_replaying_reasoning(); // Nothing was sent yet.
	claude->build_stream_request_body("system", _tool_loop_history(true), "");

	const bool stopped = claude->stop_replaying_reasoning();
	const Dictionary body = JSON::parse_string(claude->build_stream_request_body("system", _tool_loop_history(claude->wants_reasoning_back()), ""));
	const Array new_blocks = Dictionary(Array(body["messages"])[5])["content"];

	CHECK_FALSE(before_any);
	CHECK(stopped);
	CHECK_FALSE(claude->wants_reasoning_back());
	CHECK(String(Dictionary(new_blocks[0])["type"]) == "tool_use");
	CHECK_FALSE(body.has("thinking"));
	CHECK_FALSE(String("\n").join(claude->get_headers()).contains("anthropic-beta"));
}

TEST_CASE("[AIAssistant][ModelSpecs] a thinking signature in Claude's stream is read") {
	const Dictionary delta = AIProvider::create("anthropic")->parse_stream_delta_ex(
			"{\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"signature_delta\",\"signature\":\"abc\"}}");
	const Dictionary redacted = AIProvider::create("anthropic")->parse_stream_delta_ex(
			"{\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"redacted_thinking\",\"data\":\"x\"}}");

	CHECK(String(delta["signature"]) == "abc");
	CHECK(bool(redacted["redacted_thinking"]));
}

// --- The downloaded copy ---

// A scratch file in place of the editor's copy, for the length of a test.
struct CacheFixture {
	String path;
	CacheFixture() {
		path = TestUtils::get_temp_path("ai_model_specs_cache.json");
		DirAccess::remove_absolute(path);
		AIModelSpecs::set_cache_path_override(path);
	}
	~CacheFixture() {
		DirAccess::remove_absolute(path);
		AIModelSpecs::set_cache_path_override(String());
		AIModelSpecs::use_builtin();
	}
};

TEST_CASE("[AIAssistant][ModelSpecs] a stored copy is used, also after a restart, and is fresh") {
	CacheFixture cache;
	const bool stale_before = AIModelSpecs::is_cache_stale();

	REQUIRE(AIModelSpecs::store_cache(SPECS));
	AIModelSpecs::use_builtin();
	const bool known_without_copy = AIModelSpecs::find("deepseek", "deepseek-v9").found;
	AIModelSpecs::reload();

	CHECK(stale_before);
	CHECK_FALSE(AIModelSpecs::is_cache_stale());
	CHECK_FALSE(known_without_copy);
	CHECK(AIModelSpecs::find("deepseek", "deepseek-v9").output == 400000);
	CHECK_FALSE(FileAccess::exists(cache.path + ".partial"));
}

TEST_CASE("[AIAssistant][ModelSpecs] a damaged copy is removed, so the next start downloads a new one") {
	CacheFixture cache;
	{
		Ref<FileAccess> file = FileAccess::open(cache.path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("{\"deepseek\": {\"models\": "); // Cut off.
	}

	AIModelSpecs::reload();

	CHECK_FALSE(FileAccess::exists(cache.path));
	CHECK(AIModelSpecs::is_cache_stale());
	CHECK(AIModelSpecs::find("deepseek", "deepseek-v4-pro").found); // The built-in copy.
}

TEST_CASE("[AIAssistant][ModelSpecs] entries that are not plain names or cannot be real are dropped") {
	SpecsFixture fixture;
	REQUIRE(AIModelSpecs::use(R"({"google": {"models": {
		"gemini-9-pro": {"limit": {"context": 1000, "output": 500}, "tool_call": true, "release_date": "next week", "family": "a/b",
			"reasoning_options": [{"type": "effort", "values": ["high", "x?y"]}]},
		"evil/../models:generate?key=1": {"limit": {"context": 1000, "output": 500}, "tool_call": true},
		"too-long-output": {"limit": {"context": 1000, "output": 5000}, "tool_call": true}}}})"));

	const AIModelSpec kept = AIModelSpecs::find("gemini", "gemini-9-pro");

	CHECK(kept.found);
	CHECK(kept.released.is_empty());
	CHECK(kept.family.is_empty());
	CHECK(kept.efforts == PackedStringArray({ "high" }));
	CHECK(AIModelSpecs::get_models("gemini", 10) == PackedStringArray({ "gemini-9-pro" }));
	CHECK_FALSE(AIModelSpecs::find("gemini", "too-long-output").found);
}

TEST_CASE("[AIAssistant][ModelSpecs] numbers that cannot be true are not believed") {
	SpecsFixture fixture;
	REQUIRE(AIModelSpecs::use(R"({"deepseek": {"models": {"deepseek-v9": {"limit": {"context": 1e30, "output": -5}}}}})"));

	const AIModelSpec spec = AIModelSpecs::find("deepseek", "deepseek-v9");

	CHECK(spec.found);
	CHECK(spec.context == 0);
	CHECK(spec.output == 0);
	CHECK(_provider("deepseek", "deepseek-v9")->get_model_context_length() == 1000000); // The table's.
}

} // namespace TestAIModelSpecs
