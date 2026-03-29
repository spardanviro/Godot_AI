#include "openai_provider.h"
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
	// OpenAI / compatible model context lengths.
	if (m.find("o3") >= 0 || m.find("o4") >= 0) return 200000;
	if (m.find("gpt-5") >= 0) return 128000;
	if (m.find("gpt-4.1") >= 0) return 1048576;
	if (m.find("gpt-4o") >= 0) return 128000;
	if (m.find("gpt-4-turbo") >= 0) return 128000;
	if (m.find("gpt-4") >= 0) return 8192;
	if (m.find("gpt-3.5") >= 0) return 16384;
	return 128000; // Default for unknown OpenAI-compatible models.
}

int OpenAIProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("o3") >= 0 || m.find("o4") >= 0) return 16384;
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

	// System message.
	Dictionary sys_msg;
	sys_msg["role"] = "system";
	sys_msg["content"] = p_system_prompt;
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
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

String OpenAIProvider::get_models_list_url() const {
	// Use custom endpoint base or default OpenAI.
	String endpoint = api_endpoint.is_empty() ? "https://api.openai.com/v1" : api_endpoint;
	// Remove trailing /chat/completions if present.
	if (endpoint.ends_with("/chat/completions")) {
		endpoint = endpoint.substr(0, endpoint.length() - String("/chat/completions").length());
	}
	return endpoint + "/models";
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

		// Only current generation: gpt-5.x flagship or o3/o4 reasoning series.
		bool current_gen = false;
		if (id.begins_with("gpt-5")) {
			current_gen = true;
		} else if (id.begins_with("o3") || id.begins_with("o4")) {
			current_gen = true;
		}
		if (!current_gen) {
			continue;
		}

		// Skip reduced-capability variants.
		if (id.contains("mini") || id.contains("nano") || id.contains("lite")) {
			continue;
		}
		// Skip non-chat capabilities.
		if (id.contains("audio") || id.contains("realtime") || id.contains("search") || id.contains("transcribe")) {
			continue;
		}
		// Skip dated snapshots (prefer canonical alias e.g. "gpt-5.4" over "gpt-5.4-2026-01-15").
		if (id.find("-20") > 2) {
			continue;
		}

		models.push_back(id);
	}

	models.sort();
	return models;
}

String OpenAIProvider::select_best_model(const PackedStringArray &p_models) const {
	// Prefer highest-version gpt-5.x; fall back to highest o-series.
	String best_gpt;
	float best_gpt_ver = 0.0f;
	String best_o;

	for (int i = 0; i < p_models.size(); i++) {
		const String &m = p_models[i];
		if (m.begins_with("gpt-5")) {
			String ver_str = m.substr(4); // "5.4"
			float ver = ver_str.to_float();
			if (ver > best_gpt_ver) {
				best_gpt_ver = ver;
				best_gpt = m;
			}
		} else if ((m.begins_with("o3") || m.begins_with("o4")) && best_o.is_empty()) {
			best_o = m;
		}
	}

	if (!best_gpt.is_empty()) {
		return best_gpt;
	}
	if (!best_o.is_empty()) {
		return best_o;
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
	sys_msg["content"] = p_system_prompt;
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
