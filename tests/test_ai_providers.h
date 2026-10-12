#pragma once

#include "../ai_credential_store.h"
#include "../ai_provider.h"
#include "../ai_system_prompt.h"
#include "../providers/deepseek_provider.h"
#include "../providers/glm_provider.h"
#include "../providers/openai_provider.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestAIProviders {

static Dictionary _parse(const String &p_json) {
	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse(p_json) == OK);
	return json->get_data();
}

static Array _history() {
	Dictionary user;
	user["role"] = "user";
	user["content"] = "earlier question";
	Dictionary assistant;
	assistant["role"] = "assistant";
	assistant["content"] = "earlier answer";
	Array history;
	history.push_back(user);
	history.push_back(assistant);
	return history;
}

// --- Request bodies ---

TEST_CASE("[AIAssistant][Providers] request body carries system prompt, history and the new message in order") {
	Ref<DeepSeekProvider> provider;
	provider.instantiate();

	const Dictionary body = _parse(provider->build_stream_request_body("SYSTEM", _history(), "new question"));

	const Array messages = body["messages"];
	REQUIRE(messages.size() == 4);
	CHECK(String(Dictionary(messages[0])["role"]) == "system");
	CHECK(String(Dictionary(messages[0])["content"]) == "SYSTEM");
	CHECK(String(Dictionary(messages[1])["content"]) == "earlier question");
	CHECK(String(Dictionary(messages[3])["role"]) == "user");
	CHECK(String(Dictionary(messages[3])["content"]) == "new question");
	CHECK(bool(body["stream"]));
	CHECK(String(body["model"]) == "deepseek-chat");
}

TEST_CASE("[AIAssistant][Providers] only the streaming body asks for a stream") {
	Ref<GLMProvider> provider;
	provider.instantiate();

	const Dictionary body = _parse(provider->build_request_body("SYSTEM", Array(), "question"));

	CHECK_FALSE(body.has("stream"));
}

TEST_CASE("[AIAssistant][Providers] each provider names the output limit the way its API expects") {
	Ref<OpenAIProvider> openai;
	openai.instantiate();
	openai->set_max_tokens(1234);
	Ref<DeepSeekProvider> deepseek;
	deepseek.instantiate();
	deepseek->set_max_tokens(1234);

	const Dictionary openai_body = _parse(openai->build_stream_request_body("S", Array(), "q"));
	const Dictionary deepseek_body = _parse(deepseek->build_stream_request_body("S", Array(), "q"));

	CHECK(int(openai_body["max_completion_tokens"]) == 1234);
	CHECK_FALSE(openai_body.has("max_tokens"));
	CHECK(int(deepseek_body["max_tokens"]) == 1234);
	CHECK_FALSE(deepseek_body.has("max_completion_tokens"));
}

TEST_CASE("[AIAssistant][Providers] temperature is omitted once a model has rejected it") {
	Ref<OpenAIProvider> provider;
	provider.instantiate();
	provider->set_temperature(0.3f);

	const Dictionary with_temperature = _parse(provider->build_stream_request_body("S", Array(), "q"));
	provider->set_send_temperature(false);
	const Dictionary without_temperature = _parse(provider->build_stream_request_body("S", Array(), "q"));

	CHECK(with_temperature.has("temperature"));
	CHECK_FALSE(without_temperature.has("temperature"));
}

TEST_CASE("[AIAssistant][Providers] the cache boundary marker never reaches the API") {
	Ref<GLMProvider> provider;
	provider.instantiate();
	const String prompt = String("static part\n") + AISystemPrompt::CACHE_BOUNDARY + "\ndynamic part";

	const Dictionary body = _parse(provider->build_stream_request_body(prompt, Array(), "q"));

	const String sent = Dictionary(Array(body["messages"])[0])["content"];
	CHECK(sent == "static part\ndynamic part");
}

TEST_CASE("[AIAssistant][Providers] a screenshot is attached only by providers that accept images") {
	Ref<OpenAIProvider> openai;
	openai.instantiate();
	openai->set_pending_image("QUJD");
	Ref<DeepSeekProvider> deepseek;
	deepseek.instantiate();
	deepseek->set_pending_image("QUJD");

	const Dictionary openai_body = _parse(openai->build_stream_request_body("S", Array(), "look"));
	const Dictionary deepseek_body = _parse(deepseek->build_stream_request_body("S", Array(), "look"));

	const Variant openai_content = Dictionary(Array(openai_body["messages"])[1])["content"];
	const Variant deepseek_content = Dictionary(Array(deepseek_body["messages"])[1])["content"];
	REQUIRE(openai_content.get_type() == Variant::ARRAY);
	const Dictionary image_part = Array(openai_content)[1];
	CHECK(String(Dictionary(image_part["image_url"])["url"]) == "data:image/png;base64,QUJD");
	CHECK(deepseek_content.get_type() == Variant::STRING);
}

// --- URLs ---

TEST_CASE("[AIAssistant][Providers] default endpoints resolve to the chat and models URLs") {
	Ref<OpenAIProvider> openai;
	openai.instantiate();
	Ref<DeepSeekProvider> deepseek;
	deepseek.instantiate();
	Ref<GLMProvider> glm;
	glm.instantiate();

	CHECK(openai->get_stream_url() == "https://api.openai.com/v1/chat/completions");
	CHECK(openai->get_models_list_url() == "https://api.openai.com/v1/models");
	CHECK(deepseek->get_stream_url() == "https://api.deepseek.com/chat/completions");
	CHECK(deepseek->get_models_list_url() == "https://api.deepseek.com/models");
	CHECK(glm->get_stream_url() == "https://open.bigmodel.cn/api/coding/paas/v4/chat/completions");
	CHECK(glm->get_models_list_url() == "https://open.bigmodel.cn/api/coding/paas/v4/models");
}

TEST_CASE("[AIAssistant][Providers] a custom OpenAI endpoint may be a host, a /v1 base or a full URL") {
	static const char *spellings[] = {
		"http://localhost:8317",
		"http://localhost:8317/",
		"http://localhost:8317/v1",
		"http://localhost:8317/v1/chat/completions",
		"http://localhost:8317/v1/models",
		nullptr
	};

	for (int i = 0; spellings[i]; i++) {
		Ref<OpenAIProvider> provider;
		provider.instantiate();
		provider->set_api_endpoint(spellings[i]);

		CHECK_MESSAGE(provider->get_stream_url() == "http://localhost:8317/v1/chat/completions", spellings[i]);
		CHECK_MESSAGE(provider->get_models_list_url() == "http://localhost:8317/v1/models", spellings[i]);
	}
}

TEST_CASE("[AIAssistant][Providers] a custom base URL gets the chat path appended") {
	Ref<DeepSeekProvider> provider;
	provider.instantiate();
	provider->set_api_endpoint("https://proxy.example.com/deepseek/");

	CHECK(provider->get_stream_url() == "https://proxy.example.com/deepseek/chat/completions");
	CHECK(provider->get_request_url() == provider->get_stream_url());
}

TEST_CASE("[AIAssistant][Providers] the API key is sent as a bearer token") {
	Ref<GLMProvider> provider;
	provider.instantiate();
	provider->set_api_key("secret-key");

	CHECK(provider->get_headers().has("Authorization: Bearer secret-key"));
	CHECK(provider->get_models_list_headers().has("Authorization: Bearer secret-key"));
}

// --- Responses ---

TEST_CASE("[AIAssistant][Providers] stream deltas separate answer and reasoning") {
	Ref<DeepSeekProvider> provider;
	provider.instantiate();

	const Dictionary content = provider->parse_stream_delta_ex("{\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}");
	const Dictionary reasoning = provider->parse_stream_delta_ex("{\"choices\":[{\"delta\":{\"reasoning_content\":\"hmm\",\"content\":null}}]}");

	CHECK(String(content["content"]) == "Hello");
	CHECK(String(content["thinking"]).is_empty());
	CHECK(String(reasoning["thinking"]) == "hmm");
	CHECK(String(reasoning["content"]).is_empty());
}

TEST_CASE("[AIAssistant][Providers] malformed stream payloads yield empty deltas instead of errors") {
	Ref<OpenAIProvider> provider;
	provider.instantiate();
	static const char *payloads[] = {
		"not json",
		"[]",
		"{}",
		"{\"choices\":[]}",
		"{\"choices\":[{}]}",
		"{\"choices\":[{\"delta\":\"text\"}]}",
		"{\"choices\":[{\"delta\":{\"content\":42}}]}",
		nullptr
	};

	for (int i = 0; payloads[i]; i++) {
		const Dictionary delta = provider->parse_stream_delta_ex(payloads[i]);
		CHECK_MESSAGE(String(delta["content"]).is_empty(), payloads[i]);
		CHECK_MESSAGE(String(delta["thinking"]).is_empty(), payloads[i]);
	}
}

TEST_CASE("[AIAssistant][Providers] the end of a stream is marked by [DONE]") {
	Ref<GLMProvider> provider;
	provider.instantiate();

	CHECK(provider->is_stream_done_marker(" [DONE]\n"));
	CHECK_FALSE(provider->is_stream_done_marker("{\"choices\":[]}"));
}

TEST_CASE("[AIAssistant][Providers] parse_response returns the message or a recognisable error string") {
	Ref<DeepSeekProvider> provider;
	provider.instantiate();

	const String ok = provider->parse_response("{\"choices\":[{\"message\":{\"content\":\"summary text\"}}]}");
	const String api_error = provider->parse_response("{\"error\":{\"message\":\"rate limited\"}}");
	const String garbage = provider->parse_response("<html>502</html>");
	const String null_content = provider->parse_response("{\"choices\":[{\"message\":{\"content\":null}}]}");

	CHECK(ok == "summary text");
	CHECK(api_error == "API Error: rate limited");
	CHECK(garbage.begins_with("Error: "));
	CHECK(null_content.is_empty());
}

// --- Model lists ---

TEST_CASE("[AIAssistant][Providers] model lists are filtered to chat models and the preferred one is picked") {
	Ref<GLMProvider> glm;
	glm.instantiate();
	Ref<OpenAIProvider> openai;
	openai.instantiate();

	const PackedStringArray glm_models = glm->parse_models_list(
			"{\"data\":[{\"id\":\"glm-5-turbo\"},{\"id\":\"glm-5\"},{\"id\":\"glm-5v\"},{\"id\":\"glm-4.6\"},{\"id\":\"glm-5-20260101\"},{\"name\":\"no id\"}]}");
	const PackedStringArray openai_models = openai->parse_models_list(
			"{\"data\":[{\"id\":\"gpt-5.4-mini\"},{\"id\":\"gpt-4o\"},{\"id\":\"text-embedding-3\"}]}");

	REQUIRE(glm_models.size() == 2);
	CHECK(glm->select_best_model(glm_models) == "glm-5");
	REQUIRE(openai_models.size() == 1);
	CHECK(openai->select_best_model(openai_models) == "gpt-5.4-mini");
}

TEST_CASE("[AIAssistant][Providers] with no preferred model listed the fallback differs by provider") {
	Ref<GLMProvider> glm;
	glm.instantiate();
	Ref<OpenAIProvider> openai;
	openai.instantiate();
	PackedStringArray unknown;
	unknown.push_back("some-new-model");

	CHECK(glm->select_best_model(unknown) == "some-new-model");
	CHECK(openai->select_best_model(unknown) == openai->get_default_model());
	CHECK(glm->select_best_model(PackedStringArray()) == glm->get_default_model());
}

TEST_CASE("[AIAssistant][Providers] the factory creates every listed provider under its own id") {
	const Vector<String> ids = AIProvider::get_provider_ids();

	CHECK(ids.size() == 5);
	for (int i = 0; i < ids.size(); i++) {
		const Ref<AIProvider> provider = AIProvider::create(ids[i]);
		REQUIRE(provider.is_valid());
		CHECK_MESSAGE(provider->get_provider_name() == ids[i], ids[i]);
		CHECK_FALSE(provider->get_default_model().is_empty());
		CHECK(provider->get_stream_url().begins_with("https://"));
	}
	CHECK(AIProvider::create("no-such-provider")->get_provider_name() == "anthropic");
}

// --- Credential store ---

TEST_CASE("[AIAssistant][Credentials] a sealed secret round-trips and does not contain the plain text") {
	const String secret = String::utf8("sk-test-1234567890-密钥");

	const PackedByteArray sealed = AICredentialStore::protect(secret);
	bool ok = false;
	const String unsealed = AICredentialStore::unprotect(sealed, &ok);

	REQUIRE_FALSE(sealed.is_empty());
	CHECK(ok);
	CHECK(unsealed == secret);
	const String sealed_as_text = String::utf8((const char *)sealed.ptr(), sealed.size());
	CHECK(sealed_as_text.find("sk-test-1234567890") < 0);
}

TEST_CASE("[AIAssistant][Credentials] a damaged blob is rejected rather than decoded to garbage") {
	PackedByteArray sealed = AICredentialStore::protect("sk-test-key");
	sealed.set(sealed.size() / 2, sealed[sealed.size() / 2] ^ 0xFF);

	bool ok = true;
	ERR_PRINT_OFF;
	const String unsealed = AICredentialStore::unprotect(sealed, &ok);
	const String empty_blob = AICredentialStore::unprotect(PackedByteArray(), &ok);
	ERR_PRINT_ON;

	CHECK_FALSE(ok);
	CHECK(unsealed != "sk-test-key");
	CHECK(empty_blob.is_empty());
}

TEST_CASE("[AIAssistant][Credentials] keys are stored per provider, off the settings file, and can be removed") {
	const String path = TestUtils::get_temp_path("ai_credentials_test/credentials.json");
	DirAccess::remove_absolute(path);
	AICredentialStore::set_storage_path_override(path);

	const bool stored = AICredentialStore::set_stored_api_key("openai", "sk-openai-key");
	AICredentialStore::set_stored_api_key("glm", "glm-key");
	const String file_text = FileAccess::get_file_as_string(path);
	const String openai_key = AICredentialStore::get_stored_api_key("openai");
	const String missing_key = AICredentialStore::get_stored_api_key("gemini");
	AICredentialStore::set_stored_api_key("openai", "");
	const String removed_key = AICredentialStore::get_stored_api_key("openai");
	const String other_key = AICredentialStore::get_stored_api_key("glm");

	AICredentialStore::set_storage_path_override(String());
	DirAccess::remove_absolute(path);

	CHECK(stored);
	CHECK(openai_key == "sk-openai-key");
	CHECK(file_text.find("sk-openai-key") < 0); // Never written in the clear.
	CHECK(missing_key.is_empty());
	CHECK(removed_key.is_empty());
	CHECK(other_key == "glm-key");
}

TEST_CASE("[AIAssistant][Credentials] an environment variable takes precedence over the stored key") {
	const String path = TestUtils::get_temp_path("ai_credentials_test/credentials_env.json");
	DirAccess::remove_absolute(path);
	AICredentialStore::set_storage_path_override(path);
	AICredentialStore::set_stored_api_key("deepseek", "stored-key");
	const String variable = "GODOT_AI_API_KEY_DEEPSEEK";
	const String previous = OS::get_singleton()->get_environment(variable);

	OS::get_singleton()->set_environment(variable, "env-key");
	const String with_env = AICredentialStore::get_api_key("deepseek");
	const String source = AICredentialStore::get_environment_source("deepseek");
	if (previous.is_empty()) {
		OS::get_singleton()->unset_environment(variable);
	} else {
		OS::get_singleton()->set_environment(variable, previous);
	}
	const String without_env = AICredentialStore::get_api_key("deepseek");

	AICredentialStore::set_storage_path_override(String());
	DirAccess::remove_absolute(path);

	CHECK(with_env == "env-key");
	CHECK(source == variable);
	// Without the variable the vendor's own one may still be set on this
	// machine; only assert when the lookup fell through to the store.
	if (AICredentialStore::get_environment_source("deepseek").is_empty()) {
		CHECK(without_env == "stored-key");
	}
}

// --- How a stream ended ---

TEST_CASE("[AIAssistant][Providers] a reply cut off at the output limit is reported as such") {
	const Dictionary openai = AIProvider::create("openai")->parse_stream_delta_ex("{\"choices\":[{\"delta\":{},\"finish_reason\":\"length\"}]}");
	const Dictionary anthropic = AIProvider::create("anthropic")->parse_stream_delta_ex("{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"max_tokens\"}}");
	const Dictionary gemini = AIProvider::create("gemini")->parse_stream_delta_ex("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"abc\"}]},\"finishReason\":\"MAX_TOKENS\"}]}");

	CHECK(String(openai.get("finish", "")) == AIProvider::FINISH_LENGTH);
	CHECK(String(anthropic.get("finish", "")) == AIProvider::FINISH_LENGTH);
	CHECK(String(gemini.get("finish", "")) == AIProvider::FINISH_LENGTH);
	CHECK(String(gemini["content"]) == "abc"); // The text that did arrive is kept.
}

TEST_CASE("[AIAssistant][Providers] a normal end is not flagged") {
	const Dictionary openai = AIProvider::create("openai")->parse_stream_delta_ex("{\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}");
	const Dictionary anthropic = AIProvider::create("anthropic")->parse_stream_delta_ex("{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}");
	const Dictionary gemini = AIProvider::create("gemini")->parse_stream_delta_ex("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"abc\"}]},\"finishReason\":\"STOP\"}]}");

	CHECK_FALSE(openai.has("finish"));
	CHECK_FALSE(anthropic.has("finish"));
	CHECK_FALSE(gemini.has("finish"));
	CHECK_FALSE(openai.has("error"));
}

TEST_CASE("[AIAssistant][Providers] a withheld reply is reported as blocked") {
	const Dictionary openai = AIProvider::create("openai")->parse_stream_delta_ex("{\"choices\":[{\"delta\":{},\"finish_reason\":\"content_filter\"}]}");
	const Dictionary anthropic = AIProvider::create("anthropic")->parse_stream_delta_ex("{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"refusal\"}}");
	const Dictionary gemini = AIProvider::create("gemini")->parse_stream_delta_ex("{\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}");

	CHECK(String(openai.get("finish", "")) == AIProvider::FINISH_BLOCKED);
	CHECK(String(anthropic.get("finish", "")) == AIProvider::FINISH_BLOCKED);
	CHECK(String(gemini.get("finish", "")) == AIProvider::FINISH_BLOCKED);
}

TEST_CASE("[AIAssistant][Providers] an error inside a stream is surfaced, not swallowed") {
	const Dictionary openai = AIProvider::create("deepseek")->parse_stream_delta_ex("{\"error\":{\"message\":\"server overloaded\"}}");
	const Dictionary anthropic = AIProvider::create("anthropic")->parse_stream_delta_ex("{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"Overloaded\"}}");
	const Dictionary gemini = AIProvider::create("gemini")->parse_stream_delta_ex("{\"error\":{\"code\":503,\"message\":\"The model is overloaded.\"}}");

	CHECK(String(openai.get("error", "")) == "server overloaded");
	CHECK(String(anthropic.get("error", "")) == "Overloaded");
	CHECK(String(gemini.get("error", "")) == "The model is overloaded.");
}

// --- Model tables ---

TEST_CASE("[AIAssistant][Providers] a model the table does not know gets a cautious context window") {
	Ref<AIProvider> anthropic = AIProvider::create("anthropic");

	anthropic->set_model("some-proxy-model");
	const int unknown = anthropic->get_model_context_length();
	anthropic->set_model("claude-opus-5-5");
	const int current = anthropic->get_model_context_length();

	CHECK(unknown == 200000);
	CHECK(current == 1000000);
}

TEST_CASE("[AIAssistant][Providers] thinking models get an output limit that leaves room for the answer") {
	Ref<AIProvider> gemini = AIProvider::create("gemini");

	gemini->set_model("gemini-2.5-flash");
	const int flash = gemini->get_recommended_max_tokens();
	gemini->set_model("gemini-3-pro-preview");
	const int newer = gemini->get_recommended_max_tokens();

	CHECK(flash == 65536);
	CHECK(newer == 65536);
}

} // namespace TestAIProviders
