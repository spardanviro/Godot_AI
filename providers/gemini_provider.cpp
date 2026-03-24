#include "gemini_provider.h"
#include "core/io/json.h"

void GeminiProvider::_bind_methods() {
}

String GeminiProvider::get_provider_name() const {
	return "gemini";
}

String GeminiProvider::get_default_endpoint() const {
	return "https://generativelanguage.googleapis.com/v1beta/models/";
}

String GeminiProvider::get_default_model() const {
	return "gemini-2.5-pro";
}

int GeminiProvider::get_model_context_length() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("2.5-pro") >= 0) return 1048576;
	if (m.find("2.5-flash") >= 0) return 1048576;
	if (m.find("2.0-flash") >= 0) return 1048576;
	if (m.find("1.5-pro") >= 0) return 2097152;
	if (m.find("1.5-flash") >= 0) return 1048576;
	return 1048576; // Default 1M for Gemini models.
}

int GeminiProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("2.5-pro") >= 0) return 65536;
	if (m.find("2.5-flash") >= 0) return 8192;
	if (m.find("2.0-flash") >= 0) return 8192;
	if (m.find("1.5-pro") >= 0) return 8192;
	if (m.find("1.5-flash") >= 0) return 8192;
	return 8192;
}

String GeminiProvider::get_request_url() const {
	String endpoint = api_endpoint.is_empty() ? get_default_endpoint() : api_endpoint;
	String m = model.is_empty() ? get_default_model() : model;
	return endpoint + m + ":generateContent?key=" + api_key;
}

String GeminiProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;

	// System instruction.
	Dictionary sys_instruction;
	Dictionary sys_part;
	sys_part["text"] = p_system_prompt;
	Array sys_parts;
	sys_parts.push_back(sys_part);
	sys_instruction["parts"] = sys_parts;
	body["systemInstruction"] = sys_instruction;

	// Contents (history + user message).
	Array contents;

	for (int i = 0; i < p_messages.size(); i++) {
		Dictionary msg = p_messages[i];
		Dictionary content;
		String role = msg.get("role", "user");
		content["role"] = (role == "assistant") ? "model" : "user";

		Dictionary part;
		part["text"] = msg.get("content", "");
		Array parts;
		parts.push_back(part);
		content["parts"] = parts;
		contents.push_back(content);
	}

	// User message.
	Dictionary user_content;
	user_content["role"] = "user";
	Dictionary user_part;
	user_part["text"] = p_user_message;
	Array user_parts;
	user_parts.push_back(user_part);
	user_content["parts"] = user_parts;
	contents.push_back(user_content);

	body["contents"] = contents;

	// Generation config.
	Dictionary gen_config;
	gen_config["maxOutputTokens"] = max_tokens;
	gen_config["temperature"] = temperature;
	body["generationConfig"] = gen_config;

	return JSON::stringify(body, "", false);
}

String GeminiProvider::parse_response(const String &p_response_body) const {
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

	if (!data.has("candidates")) {
		return "Error: No candidates in response.";
	}

	Array candidates = data["candidates"];
	if (candidates.is_empty()) {
		return "Error: Empty candidates.";
	}

	Dictionary candidate = candidates[0];
	Dictionary content = candidate["content"];
	Array parts = content["parts"];

	String result;
	for (int i = 0; i < parts.size(); i++) {
		Dictionary part = parts[i];
		if (part.has("text")) {
			result += String(part["text"]);
		}
	}

	return result;
}

Vector<String> GeminiProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	// Gemini uses API key in URL query param, not headers.
	return headers;
}

String GeminiProvider::get_models_list_url() const {
	return "https://generativelanguage.googleapis.com/v1beta/models?key=" + api_key;
}

Vector<String> GeminiProvider::get_models_list_headers() const {
	// Gemini uses API key in URL, no auth headers needed.
	Vector<String> headers;
	return headers;
}

PackedStringArray GeminiProvider::parse_models_list(const String &p_response_body) const {
	PackedStringArray models;

	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_response_body);
	if (err != OK) {
		return models;
	}

	Dictionary data = json->get_data();
	if (!data.has("models")) {
		return models;
	}

	// Skip deprecated/legacy model prefixes.
	Vector<String> skip_prefixes;
	skip_prefixes.push_back("gemini-1.0");
	skip_prefixes.push_back("gemini-pro");     // Old Gemini 1.0 Pro alias.
	skip_prefixes.push_back("text-");
	skip_prefixes.push_back("chat-");
	skip_prefixes.push_back("embedding-");
	skip_prefixes.push_back("aqa");

	Array model_list = data["models"];
	for (int i = 0; i < model_list.size(); i++) {
		Dictionary m = model_list[i];
		if (!m.has("name")) {
			continue;
		}

		String name = m["name"];
		// Gemini returns "models/gemini-xxx", strip prefix.
		if (name.begins_with("models/")) {
			name = name.substr(7);
		}

		// Only keep gemini-1.5 and gemini-2.x series.
		if (!name.begins_with("gemini-")) {
			continue;
		}

		// Skip deprecated.
		bool blocked = false;
		for (int j = 0; j < skip_prefixes.size(); j++) {
			if (name.begins_with(skip_prefixes[j])) {
				blocked = true;
				break;
			}
		}
		if (blocked) {
			continue;
		}

		// Must support generateContent.
		if (m.has("supportedGenerationMethods")) {
			Array methods = m["supportedGenerationMethods"];
			bool supports_generate = false;
			for (int j = 0; j < methods.size(); j++) {
				if (String(methods[j]) == "generateContent") {
					supports_generate = true;
					break;
				}
			}
			if (supports_generate) {
				models.push_back(name);
			}
		}
	}

	models.sort();
	return models;
}

String GeminiProvider::select_best_model(const PackedStringArray &p_models) const {
	// Pick the highest-version gemini-X.Y-pro that is NOT a preview.
	// Stable > preview. Pro > flash > lite.
	String best;
	float best_ver = 0.0f;

	for (int i = 0; i < p_models.size(); i++) {
		const String &m = p_models[i];
		// Skip preview/experimental models — only stable.
		if (m.contains("preview") || m.contains("exp") || m.contains("thinking")) {
			continue;
		}
		// Only consider pro models (strongest tier).
		if (!m.contains("pro")) {
			continue;
		}
		// Skip lite/flash variants.
		if (m.contains("lite") || m.contains("flash")) {
			continue;
		}
		// Parse version from "gemini-X.Y-pro"
		if (m.begins_with("gemini-")) {
			String after = m.substr(7); // "2.5-pro" or "3.0-pro"
			int dash = after.find("-");
			if (dash > 0) {
				float ver = after.substr(0, dash).to_float();
				if (ver > best_ver) {
					best_ver = ver;
					best = m;
				}
			}
		}
	}

	return best.is_empty() ? get_default_model() : best;
}

// --- Streaming ---

String GeminiProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	// Same body as non-streaming.
	return build_request_body(p_system_prompt, p_messages, p_user_message);
}

String GeminiProvider::parse_stream_delta(const String &p_data) const {
	// Gemini SSE streaming with alt=sse returns:
	// {"candidates":[{"content":{"parts":[{"text":"token"}]}}]}
	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_data);
	if (err != OK) {
		return "";
	}

	Dictionary data = json->get_data();
	if (!data.has("candidates")) {
		return "";
	}

	Array candidates = data["candidates"];
	if (candidates.is_empty()) {
		return "";
	}

	Dictionary candidate = candidates[0];
	if (!candidate.has("content")) {
		return "";
	}

	Dictionary content = candidate["content"];
	if (!content.has("parts")) {
		return "";
	}

	Array parts = content["parts"];
	String result;
	for (int i = 0; i < parts.size(); i++) {
		Dictionary part = parts[i];
		if (part.has("text")) {
			result += String(part["text"]);
		}
	}

	return result;
}

Dictionary GeminiProvider::parse_stream_delta_ex(const String &p_data) const {
	Dictionary result;
	result["content"] = String();
	result["thinking"] = String();

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_data) != OK) {
		return result;
	}

	Dictionary data = json->get_data();
	if (!data.has("candidates")) {
		return result;
	}

	Array candidates = data["candidates"];
	if (candidates.is_empty()) {
		return result;
	}

	Dictionary candidate = candidates[0];
	if (!candidate.has("content")) {
		return result;
	}

	Dictionary content = candidate["content"];
	if (!content.has("parts")) {
		return result;
	}

	Array parts = content["parts"];
	String content_text;
	String thinking_text;
	for (int i = 0; i < parts.size(); i++) {
		Dictionary part = parts[i];
		if (part.has("text")) {
			// Gemini uses "thought": true for thinking parts.
			if (part.has("thought") && bool(part["thought"])) {
				thinking_text += String(part["text"]);
			} else {
				content_text += String(part["text"]);
			}
		}
	}

	result["content"] = content_text;
	result["thinking"] = thinking_text;
	return result;
}

bool GeminiProvider::is_stream_done_marker(const String &p_data) const {
	// Gemini doesn't use [DONE]. Stream ends when connection closes.
	return false;
}

String GeminiProvider::get_stream_url() const {
	String endpoint = api_endpoint.is_empty() ? get_default_endpoint() : api_endpoint;
	String m = model.is_empty() ? get_default_model() : model;
	// Use streamGenerateContent instead of generateContent, with alt=sse for SSE format.
	return endpoint + m + ":streamGenerateContent?alt=sse&key=" + api_key;
}

GeminiProvider::GeminiProvider() {
}
