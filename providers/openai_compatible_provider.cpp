#include "openai_compatible_provider.h"

#include "ai_model_names.h"

#include "core/io/json.h"

static String _string_or_empty(const Dictionary &p_dict, const String &p_key) {
	const Variant value = p_dict.get(p_key, Variant());
	return value.get_type() == Variant::STRING ? String(value) : String();
}

// "https://api.example.com/v1/models" -> "api.example.com".
static String _host_of(const String &p_url) {
	const int scheme_end = p_url.find("://");
	const String rest = scheme_end < 0 ? p_url : p_url.substr(scheme_end + 3);
	return rest.get_slicec('/', 0).to_lower();
}

bool OpenAICompatibleProvider::_is_third_party_endpoint() const {
	return !api_endpoint.strip_edges().is_empty() && _host_of(api_endpoint.strip_edges()) != _host_of(get_default_endpoint());
}

String OpenAICompatibleProvider::_get_base_url() const {
	const bool is_custom = !api_endpoint.is_empty();
	String base = is_custom ? api_endpoint : get_default_endpoint();
	while (base.ends_with("/")) {
		base = base.left(base.length() - 1);
	}
	static const char *suffixes[] = { "/chat/completions", "/models", nullptr };
	for (int i = 0; suffixes[i]; i++) {
		if (base.ends_with(suffixes[i])) {
			base = base.left(base.length() - (int)strlen(suffixes[i]));
			break;
		}
	}
	// A user-supplied bare "http://host:port" still has to reach the versioned API.
	const String required = _get_required_base_suffix();
	if (is_custom && !required.is_empty() && !base.ends_with(required)) {
		base += required;
	}
	return base;
}

// A history message in the form the API takes. Ordinary messages already
// are; the two kinds of a tool round are rewritten, and reasoning sent back
// goes in "reasoning_content".
Dictionary OpenAICompatibleProvider::_to_wire_message(const Dictionary &p_message) {
	if (p_message.has("thinking")) {
		Dictionary message = p_message.duplicate();
		message.erase("thinking");
		message.erase("thinking_signature");
		message = _to_wire_message(message);
		message["reasoning_content"] = p_message["thinking"];
		return message;
	}
	if (p_message.has("tool_calls")) {
		Array calls;
		const Array stored = p_message["tool_calls"];
		for (int i = 0; i < stored.size(); i++) {
			const Dictionary call = stored[i];
			Dictionary function;
			function["name"] = call.get("name", "");
			function["arguments"] = call.get("arguments", "{}");
			Dictionary entry;
			entry["id"] = call.get("id", "");
			entry["type"] = "function";
			entry["function"] = function;
			calls.push_back(entry);
		}
		Dictionary message;
		message["role"] = "assistant";
		// Text beside the calls is optional; an empty string is refused by some servers.
		const String content = p_message.get("content", "");
		message["content"] = content.is_empty() ? Variant() : Variant(content);
		message["tool_calls"] = calls;
		return message;
	}
	if (String(p_message.get("role", "")) == "tool") {
		Dictionary message;
		message["role"] = "tool";
		message["tool_call_id"] = p_message.get("tool_call_id", "");
		message["content"] = p_message.get("content", "");
		return message;
	}
	return p_message;
}

void OpenAICompatibleProvider::_add_thinking(Dictionary &r_body) const {
	const String choice = get_effective_thinking();
	if (!choice.is_empty() && choice != THINKING_OFF) {
		r_body["reasoning_effort"] = choice;
	}
}

// DeepSeek and GLM: thinking is switched with "thinking", its depth set with
// "reasoning_effort".
void OpenAICompatibleProvider::_add_thinking_switch(Dictionary &r_body) const {
	const String choice = get_effective_thinking();
	if (choice.is_empty()) {
		return;
	}
	Dictionary thinking_mode;
	thinking_mode["type"] = choice == THINKING_OFF ? "disabled" : "enabled";
	r_body["thinking"] = thinking_mode;
	if (choice != THINKING_OFF) {
		r_body["reasoning_effort"] = choice;
	}
}

String OpenAICompatibleProvider::_build_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message, bool p_stream) const {
	Dictionary body;
	body["model"] = _get_effective_model();
	body[_get_max_tokens_field()] = max_tokens;
	if (send_temperature && supports_temperature()) {
		body["temperature"] = temperature;
	}
	// Only the conversation itself: the plain request serves short side requests
	// (the history summary), which keep the model's default.
	if (p_stream) {
		_add_thinking(body);
	}
	if (p_stream) {
		body["stream"] = true;
	}

	Array messages;

	// These APIs cache the prompt prefix on their own; the marker must not leak.
	Dictionary sys_msg;
	sys_msg["role"] = "system";
	sys_msg["content"] = strip_cache_boundary(p_system_prompt);
	messages.push_back(sys_msg);

	for (int i = 0; i < p_messages.size(); i++) {
		messages.push_back(_to_wire_message(p_messages[i]));
	}

	if (!tools.is_empty()) {
		Array offered;
		for (const AIToolSpec &tool : tools) {
			Dictionary function;
			function["name"] = tool.name;
			function["description"] = tool.description;
			function["parameters"] = tool.parameters;
			Dictionary entry;
			entry["type"] = "function";
			entry["function"] = function;
			offered.push_back(entry);
		}
		body["tools"] = offered;
	}

	// After a tool round there is nothing new from the user: the model
	// carries on from the results.
	if (p_user_message.is_empty() && pending_image_png_b64.is_empty() && !tools.is_empty()) {
		body["messages"] = messages;
		return JSON::stringify(body, "", false);
	}

	Dictionary user_msg;
	user_msg["role"] = "user";
	if (p_stream && supports_image_input() && !pending_image_png_b64.is_empty()) {
		Dictionary text_part;
		text_part["type"] = "text";
		text_part["text"] = p_user_message;
		Dictionary image_url;
		image_url["url"] = "data:image/png;base64," + pending_image_png_b64;
		Dictionary image_part;
		image_part["type"] = "image_url";
		image_part["image_url"] = image_url;
		Array content_parts;
		content_parts.push_back(text_part);
		content_parts.push_back(image_part);
		user_msg["content"] = content_parts;
	} else {
		user_msg["content"] = p_user_message;
	}
	messages.push_back(user_msg);

	body["messages"] = messages;
	return JSON::stringify(body, "", false);
}

bool OpenAICompatibleProvider::_get_first_choice(const String &p_json, bool p_delta, Dictionary &r_out, String *r_error) {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_json) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		if (r_error) {
			*r_error = "Error: Failed to parse API response.";
		}
		return false;
	}
	const Dictionary data = json->get_data();
	if (data.has("error")) {
		if (r_error) {
			const Variant error = data["error"];
			const String message = error.get_type() == Variant::DICTIONARY ? String(Dictionary(error).get("message", "Unknown error")) : String(error);
			*r_error = "API Error: " + message;
		}
		return false;
	}
	if (data.get("choices", Variant()).get_type() != Variant::ARRAY) {
		if (r_error) {
			*r_error = "Error: No choices in response.";
		}
		return false;
	}
	const Array choices = data["choices"];
	if (choices.is_empty() || choices[0].get_type() != Variant::DICTIONARY) {
		if (r_error) {
			*r_error = "Error: Empty choices array.";
		}
		return false;
	}
	const Variant part = Dictionary(choices[0]).get(p_delta ? "delta" : "message", Variant());
	if (part.get_type() != Variant::DICTIONARY) {
		if (r_error) {
			*r_error = "Error: Malformed choice in response.";
		}
		return false;
	}
	r_out = part;
	return true;
}

String OpenAICompatibleProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	return _build_body(p_system_prompt, p_messages, p_user_message, false);
}

String OpenAICompatibleProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	return _build_body(p_system_prompt, p_messages, p_user_message, true);
}

String OpenAICompatibleProvider::parse_response(const String &p_response_body) const {
	Dictionary message;
	String error;
	if (!_get_first_choice(p_response_body, false, message, &error)) {
		return error;
	}
	return _string_or_empty(message, "content");
}

Vector<String> OpenAICompatibleProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

String OpenAICompatibleProvider::get_stream_url() const {
	return _get_base_url() + "/chat/completions";
}

String OpenAICompatibleProvider::get_models_list_url() const {
	return _get_base_url() + "/models";
}

Vector<String> OpenAICompatibleProvider::get_models_list_headers() const {
	Vector<String> headers;
	headers.push_back("Authorization: Bearer " + api_key);
	return headers;
}

PackedStringArray OpenAICompatibleProvider::parse_models_list(const String &p_response_body) const {
	PackedStringArray models;

	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_response_body) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return models;
	}
	const Dictionary data = json->get_data();
	if (data.get("data", Variant()).get_type() != Variant::ARRAY) {
		return models;
	}
	// The provider's own list is cut down to its current chat models; another
	// service's list is offered whole.
	const bool offer_all = _is_third_party_endpoint();
	const Array model_list = data["data"];
	for (int i = 0; i < model_list.size(); i++) {
		if (model_list[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const String id = _string_or_empty(model_list[i], "id");
		if (!id.is_empty() && (offer_all || _accepts_listed_model(id))) {
			models.push_back(id);
		}
	}
	models.sort();
	return models;
}

String OpenAICompatibleProvider::select_best_model(const PackedStringArray &p_models) const {
	if (!_is_third_party_endpoint() && !_listed_default(p_models).is_empty()) {
		return _listed_default(p_models);
	}
	const String family = _get_family_prefix();
	if (!family.is_empty() && !_is_third_party_endpoint()) {
		const String newest = AIModelNames::newest_plain(p_models, family);
		if (!newest.is_empty()) {
			return newest;
		}
	}
	const Vector<String> preferred = _get_preferred_models();
	for (int p = 0; p < preferred.size(); p++) {
		if (p_models.has(preferred[p])) {
			return preferred[p];
		}
	}
	if (_is_third_party_endpoint()) {
		return String(); // Nothing is known about these models: no recommendation.
	}
	if (!_newest_known_of(p_models).is_empty()) {
		return _newest_known_of(p_models);
	}
	if (_falls_back_to_first_listed() && !p_models.is_empty()) {
		return p_models[0];
	}
	return get_default_model();
}

String OpenAICompatibleProvider::parse_stream_delta(const String &p_data) const {
	Dictionary delta;
	if (!_get_first_choice(p_data, true, delta)) {
		return String();
	}
	return _string_or_empty(delta, "content");
}

Dictionary OpenAICompatibleProvider::parse_stream_delta_ex(const String &p_data) const {
	Dictionary result;
	result["content"] = String();
	result["thinking"] = String();

	// How the reply ends is said outside the delta: an error object instead
	// of choices, or a finish_reason next to the last delta.
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_data) == OK && json->get_data().get_type() == Variant::DICTIONARY) {
		const Dictionary data = json->get_data();
		if (data.has("error")) {
			const Variant error = data["error"];
			result["error"] = error.get_type() == Variant::DICTIONARY ? String(Dictionary(error).get("message", "Unknown error")) : String(error);
			return result;
		}
		const Variant choices = data.get("choices", Variant());
		if (choices.get_type() == Variant::ARRAY && !Array(choices).is_empty() && Array(choices)[0].get_type() == Variant::DICTIONARY) {
			const Variant reason = Dictionary(Array(choices)[0]).get("finish_reason", Variant());
			if (reason.get_type() == Variant::STRING && String(reason) == "length") {
				result["finish"] = FINISH_LENGTH;
			} else if (reason.get_type() == Variant::STRING && String(reason) == "content_filter") {
				result["finish"] = FINISH_BLOCKED;
			}
		}
	}

	Dictionary delta;
	if (!_get_first_choice(p_data, true, delta)) {
		return result;
	}
	// Reasoning models (o-series, deepseek-reasoner, GLM-5) stream their
	// thinking tokens in "reasoning_content".
	result["thinking"] = _string_or_empty(delta, "reasoning_content");
	result["content"] = _string_or_empty(delta, "content");

	// Tool calls arrive in fragments: the id and the name once, the
	// arguments a few characters at a time.
	const Variant calls = delta.get("tool_calls", Variant());
	if (calls.get_type() == Variant::ARRAY) {
		Array fragments;
		const Array listed = calls;
		for (int i = 0; i < listed.size(); i++) {
			if (listed[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary call = listed[i];
			const Variant function = call.get("function", Variant());
			Dictionary fragment;
			fragment["index"] = call.get("index", i);
			fragment["id"] = _string_or_empty(call, "id");
			fragment["name"] = function.get_type() == Variant::DICTIONARY ? _string_or_empty(function, "name") : String();
			fragment["arguments"] = function.get_type() == Variant::DICTIONARY ? _string_or_empty(function, "arguments") : String();
			fragments.push_back(fragment);
		}
		if (!fragments.is_empty()) {
			result["tool_calls"] = fragments;
		}
	}
	return result;
}

bool OpenAICompatibleProvider::is_stream_done_marker(const String &p_data) const {
	return p_data.strip_edges() == "[DONE]";
}
