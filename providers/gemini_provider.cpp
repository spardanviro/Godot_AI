#include "gemini_provider.h"

#include "ai_model_names.h"
#include "core/object/class_db.h"
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
	return "gemini-3.1-pro-preview";
}

int GeminiProvider::get_model_context_length() const {
	String m = model.is_empty() ? get_default_model() : model;
	// Gemini 3.x series — all 1M context.
	if (m.find("3.1-pro") >= 0) return 1048576;
	if (m.find("3-flash") >= 0 || m.find("3.1-flash") >= 0) return 1048576;
	// Older models kept for compatibility.
	if (m.find("2.5-pro") >= 0) return 1048576;
	if (m.find("2.5-flash") >= 0) return 1048576;
	if (m.find("2.0-flash") >= 0) return 1048576;
	return 1048576; // Default 1M for Gemini models.
}

int GeminiProvider::get_recommended_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	// Gemini 3.x series — all support 65K output.
	if (m.find("3.1-pro") >= 0) return 65536;
	if (m.find("3-flash") >= 0 || m.find("3.1-flash") >= 0) return 65536;
	// Older models.
	if (m.find("2.5-pro") >= 0) return 65536;
	if (m.find("2.5-flash") >= 0) return 65536; // Thinking tokens count against the limit: 8K left no room for the answer.
	if (m.find("gemini-3") >= 0) return 65536; // Any other 3.x model.
	if (m.find("2.0-flash") >= 0) return 8192;
	return 8192;
}

String GeminiProvider::get_request_url() const {
	String endpoint = api_endpoint.is_empty() ? get_default_endpoint() : api_endpoint;
	String m = model.is_empty() ? get_default_model() : model;
	return endpoint + m + ":generateContent";
}

static Dictionary _arguments_object(const String &p_arguments) {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_arguments) == OK && json->get_data().get_type() == Variant::DICTIONARY) {
		return json->get_data();
	}
	return Dictionary();
}

// History in Gemini's form: roles are "user" and "model", content is a list
// of parts. A tool round is a model turn with functionCall parts — each with
// the thought signature it arrived with, which the API insists on getting
// back — and one user turn with a functionResponse per call.
static Array _to_wire_contents(const Array &p_messages) {
	Array contents;
	for (int i = 0; i < p_messages.size(); i++) {
		const Dictionary message = p_messages[i];
		const String role = message.get("role", "user");
		Array parts;

		if (message.has("tool_calls")) {
			const String text = message.get("content", "");
			if (!text.strip_edges().is_empty()) {
				Dictionary text_part;
				text_part["text"] = text;
				parts.push_back(text_part);
			}
			const Array calls = message["tool_calls"];
			for (int c = 0; c < calls.size(); c++) {
				const Dictionary call = calls[c];
				Dictionary function_call;
				function_call["name"] = call.get("name", "");
				function_call["args"] = _arguments_object(call.get("arguments", "{}"));
				Dictionary part;
				part["functionCall"] = function_call;
				const String signature = call.get("provider_data", "");
				if (!signature.is_empty()) {
					part["thoughtSignature"] = signature;
				}
				parts.push_back(part);
			}
		} else if (role == "tool") {
			for (; i < p_messages.size() && String(Dictionary(p_messages[i]).get("role", "")) == "tool"; i++) {
				const Dictionary stored = p_messages[i];
				Dictionary response;
				response["result"] = stored.get("content", "");
				Dictionary function_response;
				function_response["name"] = stored.get("name", "");
				function_response["response"] = response;
				Dictionary part;
				part["functionResponse"] = function_response;
				parts.push_back(part);
			}
			i--;
		} else {
			Dictionary part;
			part["text"] = message.get("content", "");
			parts.push_back(part);
		}

		Dictionary content;
		content["role"] = role == "assistant" ? "model" : "user";
		content["parts"] = parts;
		contents.push_back(content);
	}
	return contents;
}

String GeminiProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;

	// System instruction.
	Dictionary sys_instruction;
	Dictionary sys_part;
	sys_part["text"] = strip_cache_boundary(p_system_prompt);
	Array sys_parts;
	sys_parts.push_back(sys_part);
	sys_instruction["parts"] = sys_parts;
	body["systemInstruction"] = sys_instruction;

	// Contents (history + user message).
	Array contents = _to_wire_contents(p_messages);

	if (!tools.is_empty()) {
		Array declarations;
		for (const AIToolSpec &tool : tools) {
			Dictionary declaration;
			declaration["name"] = tool.name;
			declaration["description"] = tool.description;
			// The full JSON Schema form; "parameters" takes a narrower dialect.
			declaration["parametersJsonSchema"] = tool.parameters;
			declarations.push_back(declaration);
		}
		Dictionary tool_group;
		tool_group["functionDeclarations"] = declarations;
		Array offered;
		offered.push_back(tool_group);
		body["tools"] = offered;
	}

	// User message. After a tool round there is none: the model carries on
	// from the results, and a screenshot a tool took joins them.
	Array user_parts;
	if (!pending_image_png_b64.is_empty()) {
		Dictionary inline_data;
		inline_data["mime_type"] = "image/png";
		inline_data["data"] = pending_image_png_b64;
		Dictionary image_part;
		image_part["inline_data"] = inline_data;
		user_parts.push_back(image_part);
	}
	const bool after_tool_round = !tools.is_empty() && !contents.is_empty() &&
			Dictionary(Array(Dictionary(contents[contents.size() - 1])["parts"])[0]).has("functionResponse");
	if (after_tool_round && (p_user_message.is_empty() || !pending_image_png_b64.is_empty())) {
		if (!p_user_message.is_empty()) {
			Dictionary text_part;
			text_part["text"] = p_user_message;
			user_parts.push_back(text_part);
		}
		if (!user_parts.is_empty()) {
			Dictionary last = contents[contents.size() - 1];
			Array parts = last["parts"];
			parts.append_array(user_parts);
			last["parts"] = parts;
			contents[contents.size() - 1] = last;
		}
	} else {
		Dictionary user_part;
		user_part["text"] = p_user_message;
		user_parts.push_back(user_part);
		Dictionary user_content;
		user_content["role"] = "user";
		user_content["parts"] = user_parts;
		contents.push_back(user_content);
	}

	body["contents"] = contents;

	// Generation config.
	Dictionary gen_config;
	gen_config["maxOutputTokens"] = max_tokens;
	if (send_temperature) {
		gen_config["temperature"] = temperature;
	}
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
	// The key goes in a header, not in the address: an address ends up in
	// proxy and server logs.
	headers.push_back("x-goog-api-key: " + api_key);
	return headers;
}

String GeminiProvider::get_models_list_url() const {
	return "https://generativelanguage.googleapis.com/v1beta/models?pageSize=1000";
}

Vector<String> GeminiProvider::get_models_list_headers() const {
	Vector<String> headers;
	headers.push_back("x-goog-api-key: " + api_key);
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

		// Gemini 2.5 and every later version. Skip older generations and
		// other families (gemma, imagen, veo).
		static const int OLDEST_VERSION = 2005;
		if (AIModelNames::version_after(name, "gemini-") < OLDEST_VERSION) {
			continue;
		}

		// Skip models that are not for chat.
		if (AIModelNames::contains_any(name, { "embedding", "aqa", "vision", "image", "tts", "audio", "live", "robotics", "computer-use" })) {
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
	// The pro model of the highest version. At the same version a released
	// model goes before a preview, and the plain name before its variants.
	String best;
	int best_rank = -1;

	for (int i = 0; i < p_models.size(); i++) {
		const String &m = p_models[i];
		if (!m.contains("pro") || m.contains("exp")) {
			continue;
		}
		const int version = AIModelNames::version_after(m, "gemini-");
		if (version < 0) {
			continue;
		}
		const int rank = version * 2 + (m.contains("preview") ? 0 : 1);
		if (rank > best_rank || (rank == best_rank && m.length() < best.length())) {
			best_rank = rank;
			best = m;
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

	if (json->get_data().get_type() != Variant::DICTIONARY) {
		return result;
	}
	Dictionary data = json->get_data();
	if (data.has("error")) {
		const Variant error = data["error"];
		result["error"] = error.get_type() == Variant::DICTIONARY ? String(Dictionary(error).get("message", "Unknown error")) : String("Unknown error");
		return result;
	}
	const Variant feedback = data.get("promptFeedback", Variant());
	if (feedback.get_type() == Variant::DICTIONARY && Dictionary(feedback).has("blockReason")) {
		result["finish"] = FINISH_BLOCKED;
	}
	if (!data.has("candidates")) {
		return result;
	}

	Array candidates = data["candidates"];
	if (candidates.is_empty()) {
		return result;
	}

	Dictionary candidate = candidates[0];
	const String finish_reason = candidate.get("finishReason", "");
	if (finish_reason == "MAX_TOKENS") {
		result["finish"] = FINISH_LENGTH;
	} else if (!finish_reason.is_empty() && finish_reason != "STOP" && finish_reason != "FINISH_REASON_UNSPECIFIED") {
		result["finish"] = FINISH_BLOCKED; // SAFETY, RECITATION, BLOCKLIST, ...
	}
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
	Array tool_calls;
	for (int i = 0; i < parts.size(); i++) {
		Dictionary part = parts[i];
		if (part.get("functionCall", Variant()).get_type() == Variant::DICTIONARY) {
			// A call arrives in one piece, with a signature that has to be sent back.
			const Dictionary function_call = part["functionCall"];
			Dictionary fragment;
			fragment["whole"] = true;
			fragment["name"] = function_call.get("name", "");
			fragment["arguments"] = JSON::stringify(function_call.get("args", Dictionary()), "", false);
			fragment["provider_data"] = part.get("thoughtSignature", "");
			tool_calls.push_back(fragment);
			continue;
		}
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
	if (!tool_calls.is_empty()) {
		result["tool_calls"] = tool_calls;
	}
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
	return endpoint + m + ":streamGenerateContent?alt=sse";
}

GeminiProvider::GeminiProvider() {
}
