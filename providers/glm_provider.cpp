#include "glm_provider.h"
#include "core/object/class_db.h"
#include "core/io/json.h"

void GLMProvider::_bind_methods() {
}

String GLMProvider::get_provider_name() const {
	return "glm";
}

String GLMProvider::get_default_endpoint() const {
	return "https://open.bigmodel.cn/api/coding/paas/v4/chat/completions";
}

String GLMProvider::get_default_model() const {
	return "glm-5.1";
}

int GLMProvider::get_model_context_length() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("glm-5") >= 0) return 200000; // GLM-5.x / GLM-5: 200K
	if (m.find("glm-4.7") >= 0) return 128000;
	if (m.find("glm-4.6") >= 0) return 128000;
	if (m.find("glm-4.5") >= 0) return 128000;
	return 128000;
}

int GLMProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("glm-5") >= 0) return 32768;
	if (m.find("glm-4.7") >= 0) return 16384;
	if (m.find("glm-4.6") >= 0) return 16384;
	if (m.find("glm-4.5") >= 0) return 16384;
	return 16384;
}

String GLMProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
	body["temperature"] = temperature;

	Array messages;

	// System message.
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

String GLMProvider::parse_response(const String &p_response_body) const {
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

Vector<String> GLMProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

String GLMProvider::get_models_list_url() const {
	// GLM API is OpenAI-compatible; use the /models endpoint under the coding base URL.
	String endpoint = api_endpoint.is_empty() ? get_default_endpoint() : api_endpoint;
	// Strip /chat/completions suffix to get base URL.
	if (endpoint.ends_with("/chat/completions")) {
		endpoint = endpoint.substr(0, endpoint.length() - String("/chat/completions").length());
	}
	return endpoint + "/models";
}

Vector<String> GLMProvider::get_models_list_headers() const {
	Vector<String> headers;
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

PackedStringArray GLMProvider::parse_models_list(const String &p_response_body) const {
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

		// Only GLM-5 series chat/text models.
		if (!id.begins_with("glm-5")) {
			continue;
		}
		// Skip image, audio, video, vision variants.
		if (id.contains("v") || id.contains("image") || id.contains("audio") ||
				id.contains("video") || id.contains("voice") || id.contains("ocr")) {
			continue;
		}
		// Skip dated snapshots.
		if (id.find("-20") > 3) {
			continue;
		}

		models.push_back(id);
	}

	models.sort();
	return models;
}

String GLMProvider::select_best_model(const PackedStringArray &p_models) const {
	// Prefer glm-5.1 (undocumented but live), then glm-5, then glm-5-turbo.
	for (int i = 0; i < p_models.size(); i++) {
		if (p_models[i] == "glm-5.1") {
			return p_models[i];
		}
	}
	for (int i = 0; i < p_models.size(); i++) {
		if (p_models[i] == "glm-5") {
			return p_models[i];
		}
	}
	for (int i = 0; i < p_models.size(); i++) {
		if (p_models[i] == "glm-5-turbo") {
			return p_models[i];
		}
	}
	if (!p_models.is_empty()) {
		return p_models[0];
	}
	return get_default_model();
}

// --- Streaming ---

String GLMProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
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

String GLMProvider::parse_stream_delta(const String &p_data) const {
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

Dictionary GLMProvider::parse_stream_delta_ex(const String &p_data) const {
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

	// GLM-5 uses "reasoning_content" for thinking tokens (same as OpenAI o-series).
	if (delta.has("reasoning_content") && String(delta["reasoning_content"]).length() > 0) {
		result["thinking"] = delta["reasoning_content"];
	}

	if (delta.has("content") && String(delta["content"]).length() > 0) {
		result["content"] = delta["content"];
	}

	return result;
}

bool GLMProvider::is_stream_done_marker(const String &p_data) const {
	return p_data.strip_edges() == "[DONE]";
}

GLMProvider::GLMProvider() {
}
