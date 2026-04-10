#include "openai_provider.h"
#include "core/object/class_db.h"
#include "core/io/json.h"

void OpenAIProvider::_bind_methods() {
}

String OpenAIProvider::get_provider_name() const {
	return "openai";
}

String OpenAIProvider::get_default_endpoint() const {
	return "https://api.openai.com/v1/chat/completions";
}

String OpenAIProvider::get_default_model() const {
	return "gpt-5.4";
}

int OpenAIProvider::get_model_context_length() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("o3") >= 0 || m.find("o4") >= 0) return 200000;
	if (m.find("gpt-5.4") >= 0) return 128000;
	// Codex series (gpt-5.3-codex, gpt-5.2-codex, gpt-5.1-codex, gpt-5-codex).
	if (m.find("codex") >= 0) return 128000;
	if (m.find("gpt-5.2") >= 0) return 128000;
	if (m.find("gpt-5.1") >= 0) return 128000;
	if (m.find("gpt-5") >= 0) return 128000;
	if (m.find("gpt-4.1") >= 0) return 1048576;
	if (m.find("gpt-4o") >= 0) return 128000;
	if (m.find("gpt-4-turbo") >= 0) return 128000;
	if (m.find("gpt-4") >= 0) return 8192;
	if (m.find("gpt-3.5") >= 0) return 16384;
	return 128000;
}

int OpenAIProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("o3-pro") >= 0) return 32768;
	if (m.find("o3") >= 0 || m.find("o4") >= 0) return 16384;
	if (m.find("gpt-5.4") >= 0) return 16384;
	// Codex reasoning models — allow more tokens for reasoning output.
	if (m.find("codex") >= 0) return 32768;
	if (m.find("gpt-5.2") >= 0) return 16384;
	if (m.find("gpt-5.1") >= 0) return 16384;
	if (m.find("gpt-5") >= 0) return 16384;
	if (m.find("gpt-4.1") >= 0) return 32768;
	if (m.find("gpt-4o") >= 0) return 16384;
	if (m.find("gpt-4-turbo") >= 0) return 4096;
	if (m.find("gpt-4") >= 0) return 4096;
	if (m.find("gpt-3.5") >= 0) return 4096;
	return 8192;
}

String OpenAIProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_completion_tokens"] = max_tokens;
	body["temperature"] = temperature;

	Array messages;

	// System message — strip the cache boundary marker (OpenAI caches automatically).
	Dictionary sys_msg;
	sys_msg["role"] = "system";
	sys_msg["content"] = strip_cache_boundary(p_system_prompt);
	messages.push_back(sys_msg);

	// History messages.
	for (int i = 0; i < p_messages.size(); i++) {
		messages.push_back(p_messages[i]);
	}

	// User message.
	Dictionary user_msg;
	user_msg["role"] = "user";
	user_msg["content"] = p_user_message;
	messages.push_back(user_msg);

	body["messages"] = messages;

	return JSON::stringify(body, "", false);
}

String OpenAIProvider::parse_response(const String &p_response_body) const {
	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_response_body);
	if (err != OK) {
		return "Error: Failed to parse API response.";
	}

	Dictionary data = json->get_data();
	if (data.has("error")) {
		Dictionary error = data["error"];
		return "API Error: " + String(error.get("message", "Unknown error"));
	}

	if (!data.has("choices")) {
		return "Error: No choices in response.";
	}

	Array choices = data["choices"];
	if (choices.is_empty()) {
		return "Error: Empty choices array.";
	}

	Dictionary choice = choices[0];
	Dictionary message = choice["message"];
	return message["content"];
}

Vector<String> OpenAIProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Accept: text/event-stream");
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

// Normalize a user-supplied endpoint to just the /v1 base.
// Accepts any of:   http://host:port
//                   http://host:port/v1
//                   http://host:port/v1/chat/completions
// Always returns:   http://host:port/v1   (no trailing slash)
static String _openai_base_url(const String &p_endpoint) {
	String ep = p_endpoint;
	// Strip trailing slash(es).
	while (ep.ends_with("/")) {
		ep = ep.substr(0, ep.length() - 1);
	}
	// Strip known suffixes so we always end up with the base.
	if (ep.ends_with("/chat/completions")) {
		ep = ep.substr(0, ep.length() - (int)String("/chat/completions").length());
	} else if (ep.ends_with("/models")) {
		ep = ep.substr(0, ep.length() - (int)String("/models").length());
	}
	// If the remaining URL has no /v1 suffix at all, the user supplied a bare
	// host:port — append /v1 so downstream path construction works correctly.
	if (!ep.ends_with("/v1")) {
		ep += "/v1";
	}
	return ep;
}

String OpenAIProvider::get_stream_url() const {
	if (api_endpoint.is_empty()) {
		return get_default_endpoint(); // https://api.openai.com/v1/chat/completions
	}
	return _openai_base_url(api_endpoint) + "/chat/completions";
}

String OpenAIProvider::get_models_list_url() const {
	if (api_endpoint.is_empty()) {
		return "https://api.openai.com/v1/models";
	}
	return _openai_base_url(api_endpoint) + "/models";
}

Vector<String> OpenAIProvider::get_models_list_headers() const {
	Vector<String> headers;
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

PackedStringArray OpenAIProvider::parse_models_list(const String &p_response_body) const {
	PackedStringArray models;

	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_response_body);
	if (err != OK) {
		return models;
	}

	Dictionary data = json->get_data();
	if (!data.has("data")) {
		return models;
	}

	Array model_list = data["data"];
	for (int i = 0; i < model_list.size(); i++) {
		Dictionary m = model_list[i];
		if (!m.has("id")) {
			continue;
		}
		String id = m["id"];

		// Accept all gpt-5.x models (flagship + codex series).
		if (!id.begins_with("gpt-5")) {
			continue;
		}

		models.push_back(id);
	}

	models.sort();
	return models;
}

String OpenAIProvider::select_best_model(const PackedStringArray &p_models) const {
	// Priority: gpt-5.4 > gpt-5.3-codex > gpt-5.4-mini > gpt-5.4-pro > gpt-5.4-nano.
	static const char *preferred[] = {
		"gpt-5.4", "gpt-5.3-codex", "gpt-5.4-mini", "gpt-5.4-pro", "gpt-5.4-nano", nullptr
	};
	for (int p = 0; preferred[p] != nullptr; p++) {
		for (int i = 0; i < p_models.size(); i++) {
			if (p_models[i] == String(preferred[p])) {
				return p_models[i];
			}
		}
	}
	return get_default_model();
}

// --- Streaming ---

String OpenAIProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_completion_tokens"] = max_tokens;
	body["temperature"] = temperature;
	body["stream"] = true;

	Array messages;

	Dictionary sys_msg;
	sys_msg["role"] = "system";
	sys_msg["content"] = strip_cache_boundary(p_system_prompt);
	messages.push_back(sys_msg);

	for (int i = 0; i < p_messages.size(); i++) {
		messages.push_back(p_messages[i]);
	}

	Dictionary user_msg;
	user_msg["role"] = "user";
	user_msg["content"] = p_user_message;
	messages.push_back(user_msg);

	body["messages"] = messages;

	return JSON::stringify(body, "", false);
}

String OpenAIProvider::parse_stream_delta(const String &p_data) const {
	// p_data is the JSON payload after "data: "
	// Format: {"choices":[{"delta":{"content":"token"}}]}
	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_data);
	if (err != OK) {
		return "";
	}

	Dictionary data = json->get_data();
	if (!data.has("choices")) {
		return "";
	}

	Array choices = data["choices"];
	if (choices.is_empty()) {
		return "";
	}

	Dictionary choice = choices[0];
	if (!choice.has("delta")) {
		return "";
	}

	Dictionary delta = choice["delta"];
	if (delta.has("content")) {
		return delta["content"];
	}

	return "";
}

Dictionary OpenAIProvider::parse_stream_delta_ex(const String &p_data) const {
	Dictionary result;
	result["content"] = String();
	result["thinking"] = String();

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_data) != OK) {
		return result;
	}

	Dictionary data = json->get_data();
	if (!data.has("choices")) {
		return result;
	}

	Array choices = data["choices"];
	if (choices.is_empty()) {
		return result;
	}

	Dictionary choice = choices[0];
	if (!choice.has("delta")) {
		return result;
	}

	Dictionary delta = choice["delta"];

	// OpenAI o-series models use "reasoning_content" for thinking tokens.
	if (delta.has("reasoning_content") && String(delta["reasoning_content"]).length() > 0) {
		result["thinking"] = delta["reasoning_content"];
	}

	if (delta.has("content") && String(delta["content"]).length() > 0) {
		result["content"] = delta["content"];
	}

	return result;
}

bool OpenAIProvider::is_stream_done_marker(const String &p_data) const {
	return p_data.strip_edges() == "[DONE]";
}

OpenAIProvider::OpenAIProvider() {
}
