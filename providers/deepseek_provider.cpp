#include "deepseek_provider.h"
#include "core/object/class_db.h"
#include "core/io/json.h"

void DeepSeekProvider::_bind_methods() {
}

String DeepSeekProvider::get_provider_name() const {
	return "deepseek";
}

String DeepSeekProvider::get_default_endpoint() const {
	return "https://api.deepseek.com/chat/completions";
}

String DeepSeekProvider::get_default_model() const {
	return "deepseek-chat";
}

int DeepSeekProvider::get_model_context_length() const {
	// Both current DeepSeek models use 128K context.
	return 128000;
}

int DeepSeekProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	// deepseek-reasoner supports up to 64K output; deepseek-chat up to 8K.
	if (m == "deepseek-reasoner") {
		return 32768;
	}
	return 8192;
}

String DeepSeekProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
	body["temperature"] = temperature;

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

String DeepSeekProvider::parse_response(const String &p_response_body) const {
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

Vector<String> DeepSeekProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

String DeepSeekProvider::get_stream_url() const {
	String endpoint = api_endpoint.is_empty() ? get_default_endpoint() : api_endpoint;
	// If the user supplied a base URL without the path, append it.
	if (!endpoint.ends_with("/chat/completions")) {
		// Strip any trailing slash before appending.
		if (endpoint.ends_with("/")) {
			endpoint = endpoint.substr(0, endpoint.length() - 1);
		}
		endpoint += "/chat/completions";
	}
	return endpoint;
}

String DeepSeekProvider::get_models_list_url() const {
	// Use standard OpenAI-compatible /models endpoint.
	String endpoint = api_endpoint.is_empty() ? "https://api.deepseek.com" : api_endpoint;
	if (endpoint.ends_with("/chat/completions")) {
		endpoint = endpoint.substr(0, endpoint.length() - String("/chat/completions").length());
	}
	return endpoint + "/models";
}

Vector<String> DeepSeekProvider::get_models_list_headers() const {
	Vector<String> headers;
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

PackedStringArray DeepSeekProvider::parse_models_list(const String &p_response_body) const {
	PackedStringArray models;

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_response_body) != OK) {
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
		// Only chat/reasoner models.
		if (id != "deepseek-chat" && id != "deepseek-reasoner") {
			continue;
		}
		models.push_back(id);
	}

	models.sort();
	return models;
}

String DeepSeekProvider::select_best_model(const PackedStringArray &p_models) const {
	for (int i = 0; i < p_models.size(); i++) {
		if (p_models[i] == "deepseek-chat") {
			return p_models[i];
		}
	}
	if (!p_models.is_empty()) {
		return p_models[0];
	}
	return get_default_model();
}

// --- Streaming ---

String DeepSeekProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
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

String DeepSeekProvider::parse_stream_delta(const String &p_data) const {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_data) != OK) {
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

Dictionary DeepSeekProvider::parse_stream_delta_ex(const String &p_data) const {
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

	// deepseek-reasoner streams thinking tokens via "reasoning_content".
	if (delta.has("reasoning_content") && String(delta["reasoning_content"]).length() > 0) {
		result["thinking"] = delta["reasoning_content"];
	}

	if (delta.has("content") && String(delta["content"]).length() > 0) {
		result["content"] = delta["content"];
	}

	return result;
}

bool DeepSeekProvider::is_stream_done_marker(const String &p_data) const {
	return p_data.strip_edges() == "[DONE]";
}

DeepSeekProvider::DeepSeekProvider() {
}
