#include "anthropic_provider.h"
#include "core/io/json.h"

void AnthropicProvider::_bind_methods() {
}

String AnthropicProvider::get_provider_name() const {
	return "anthropic";
}

String AnthropicProvider::get_default_endpoint() const {
	return "https://api.anthropic.com/v1/messages";
}

String AnthropicProvider::get_default_model() const {
	return "claude-opus-4-6";
}

int AnthropicProvider::get_model_context_length() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("opus") >= 0) return 200000;
	if (m.find("sonnet") >= 0) return 200000;
	if (m.find("haiku") >= 0) return 200000;
	return 200000; // All Claude 3+ models support 200K.
}

int AnthropicProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("opus") >= 0) return 32768;
	if (m.find("sonnet") >= 0) return 16384;
	if (m.find("haiku") >= 0) return 8192;
	return 16384;
}

String AnthropicProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
	body["system"] = p_system_prompt;

	Array messages;

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

String AnthropicProvider::parse_response(const String &p_response_body) const {
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

	if (!data.has("content")) {
		return "Error: No content in response.";
	}

	Array content = data["content"];
	if (content.is_empty()) {
		return "Error: Empty content array.";
	}

	// Concatenate all text blocks.
	String result;
	for (int i = 0; i < content.size(); i++) {
		Dictionary block = content[i];
		if (String(block.get("type", "")) == "text") {
			result += String(block["text"]);
		}
	}

	return result;
}

Vector<String> AnthropicProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("x-api-key: " + api_key);
	headers.push_back("anthropic-version: 2023-06-01");
	return headers;
}

String AnthropicProvider::get_models_list_url() const {
	return "https://api.anthropic.com/v1/models?limit=100";
}

Vector<String> AnthropicProvider::get_models_list_headers() const {
	Vector<String> headers;
	headers.push_back("x-api-key: " + api_key);
	headers.push_back("anthropic-version: 2023-06-01");
	return headers;
}

PackedStringArray AnthropicProvider::parse_models_list(const String &p_response_body) const {
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

		// Only keep claude-4.x and newer series. Skip legacy claude-2, claude-3, claude-instant.
		if (id.begins_with("claude-2") || id.begins_with("claude-3") || id.begins_with("claude-instant")) {
			continue;
		}

		models.push_back(id);
	}

	models.sort();
	return models;
}

String AnthropicProvider::select_best_model(const PackedStringArray &p_models) const {
	// Pick the highest-version claude-opus (strongest tier).
	String best;
	float best_ver = 0.0f;

	for (int i = 0; i < p_models.size(); i++) {
		const String &m = p_models[i];
		// Only consider opus models (strongest tier).
		if (!m.contains("opus")) {
			continue;
		}
		// Skip dated snapshot variants if alias exists (e.g. prefer "claude-opus-4-6" over "claude-opus-4-6-20260205").
		// Shorter name = alias = preferred.
		// Parse version: "claude-opus-4-6" → extract "4-6" → 4.6
		int opus_pos = m.find("opus-");
		if (opus_pos >= 0) {
			String ver_part = m.substr(opus_pos + 5); // "4-6" or "4-6-20260205"
			// Take first two segments: major-minor.
			Vector<String> segs = ver_part.split("-");
			if (segs.size() >= 2) {
				float ver = segs[0].to_float() + segs[1].to_float() * 0.1f;
				if (ver > best_ver || (ver == best_ver && (best.is_empty() || m.length() < best.length()))) {
					best_ver = ver;
					best = m;
				}
			}
		}
	}

	return best.is_empty() ? get_default_model() : best;
}

// --- Streaming ---

String AnthropicProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
	body["system"] = p_system_prompt;
	body["stream"] = true;

	Array messages;

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

String AnthropicProvider::parse_stream_delta(const String &p_data) const {
	// Anthropic SSE data payloads:
	// {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"token"}}
	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_data);
	if (err != OK) {
		return "";
	}

	Dictionary data = json->get_data();
	String type = data.get("type", "");

	if (type == "content_block_delta") {
		Dictionary delta = data.get("delta", Dictionary());
		if (String(delta.get("type", "")) == "text_delta") {
			return delta.get("text", "");
		}
	}

	return "";
}

Dictionary AnthropicProvider::parse_stream_delta_ex(const String &p_data) const {
	Dictionary result;
	result["content"] = String();
	result["thinking"] = String();

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_data) != OK) {
		return result;
	}

	Dictionary data = json->get_data();
	String type = data.get("type", "");

	if (type == "content_block_delta") {
		Dictionary delta = data.get("delta", Dictionary());
		String delta_type = delta.get("type", "");
		if (delta_type == "thinking_delta") {
			// Anthropic extended thinking: thinking content.
			result["thinking"] = delta.get("thinking", "");
		} else if (delta_type == "text_delta") {
			result["content"] = delta.get("text", "");
		}
	}

	return result;
}

bool AnthropicProvider::is_stream_done_marker(const String &p_data) const {
	// Anthropic signals end with {"type":"message_stop"}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_data) != OK) {
		return false;
	}
	Dictionary data = json->get_data();
	return String(data.get("type", "")) == "message_stop";
}

AnthropicProvider::AnthropicProvider() {
}
