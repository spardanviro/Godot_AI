#include "anthropic_provider.h"
#include "core/object/class_db.h"
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
	return _newest_model_or(_get_flagship_families(), "claude-opus-5-5");
}

int AnthropicProvider::_table_context_length() const {
	String m = model.is_empty() ? get_default_model() : model;
	// 200K: Haiku 4.5 and everything before the 4.6 generation. Listed first so
	// the 1M default below only applies to current models (4.6 and later, plus
	// the Fable family), which all have a 1M window.
	if (m.find("haiku-4-5") >= 0 || m.find("claude-3") >= 0) return 200000;
	if (m.find("opus-4-5") >= 0 || m.find("opus-4-1") >= 0 || m.find("opus-4-0") >= 0 || m.find("opus-4-2") >= 0) return 200000;
	if (m.find("sonnet-4-5") >= 0 || m.find("sonnet-4-0") >= 0 || m.find("sonnet-4-2") >= 0) return 200000;
	// Current Claude models have a 1M window. A name this table does not
	// know at all (a proxy's own model id) gets the safe assumption:
	// compressing early costs little, overflowing fails the request.
	return m.begins_with("claude") ? 1000000 : 200000;
}

int AnthropicProvider::_table_max_tokens() const {
	String m = model.is_empty() ? get_default_model() : model;
	if (m.find("opus") >= 0) return 32768;
	if (m.find("sonnet") >= 0) return 16384;
	if (m.find("haiku") >= 0) return 8192;
	return 16384;
}

// The arguments of a call as the object the API wants; {} if they do not parse.
static Dictionary _arguments_object(const String &p_arguments) {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_arguments) == OK && json->get_data().get_type() == Variant::DICTIONARY) {
		return json->get_data();
	}
	return Dictionary();
}

// History in Anthropic's form. Ordinary messages pass through. A tool round
// becomes an assistant turn of content blocks (text, then one tool_use per
// call) followed by a single user turn holding every tool_result: the API
// wants all results of a turn together.
//
// In a continuation of a tool loop (p_in_loop: no new user message), the
// thinking of the loop's replies — those after the last user message — goes
// back as signed thinking blocks when p_replay_thinking: the model then goes
// on from its own reasoning. Earlier ones are left out, which the API allows
// for a leading run of blocks. r_replayed says whether any went; r_missing
// whether a reply of the loop went without one.
static Array _to_wire_messages(const Array &p_messages, bool p_in_loop, bool p_replay_thinking, bool *r_replayed = nullptr, bool *r_missing = nullptr) {
	int last_user = p_messages.size(); // Outside a loop no reply belongs to one.
	if (p_in_loop) {
		last_user = -1;
		for (int i = 0; i < p_messages.size(); i++) {
			if (String(Dictionary(p_messages[i]).get("role", "")) == "user") {
				last_user = i;
			}
		}
	}
	if (r_replayed) {
		*r_replayed = false;
	}
	if (r_missing) {
		*r_missing = false;
	}
	Array messages;
	for (int i = 0; i < p_messages.size(); i++) {
		Dictionary message = p_messages[i];
		const String signature = message.get("thinking_signature", "");
		const String thinking = message.get("thinking", "");
		if (message.has("thinking") || message.has("thinking_signature")) {
			message = message.duplicate();
			message.erase("thinking");
			message.erase("thinking_signature");
		}
		if (message.has("tool_calls")) {
			Array blocks;
			if (i > last_user && p_replay_thinking && !signature.is_empty()) {
				Dictionary thinking_block;
				thinking_block["type"] = "thinking";
				thinking_block["thinking"] = thinking;
				thinking_block["signature"] = signature;
				blocks.push_back(thinking_block);
				if (r_replayed) {
					*r_replayed = true;
				}
			} else if (i > last_user && r_missing) {
				*r_missing = true;
			}
			const String text = message.get("content", "");
			if (!text.strip_edges().is_empty()) {
				Dictionary text_block;
				text_block["type"] = "text";
				text_block["text"] = text;
				blocks.push_back(text_block);
			}
			const Array calls = message["tool_calls"];
			for (int c = 0; c < calls.size(); c++) {
				const Dictionary call = calls[c];
				Dictionary use;
				use["type"] = "tool_use";
				use["id"] = call.get("id", "");
				use["name"] = call.get("name", "");
				use["input"] = _arguments_object(call.get("arguments", "{}"));
				blocks.push_back(use);
			}
			Dictionary assistant;
			assistant["role"] = "assistant";
			assistant["content"] = blocks;
			messages.push_back(assistant);
			continue;
		}
		if (String(message.get("role", "")) == "tool") {
			Array results;
			for (; i < p_messages.size() && String(Dictionary(p_messages[i]).get("role", "")) == "tool"; i++) {
				const Dictionary stored = p_messages[i];
				Dictionary result;
				result["type"] = "tool_result";
				result["tool_use_id"] = stored.get("tool_call_id", "");
				result["content"] = stored.get("content", "");
				results.push_back(result);
			}
			i--;
			Dictionary user;
			user["role"] = "user";
			user["content"] = results;
			messages.push_back(user);
			continue;
		}
		messages.push_back(message);
	}
	return messages;
}

// The chosen effort goes in output_config, a budget (older models) in
// thinking; switching thinking off is only offered for the models that accept
// it. With earlier thinking replayed, a block the API no longer accepts (the
// system prompt carries the editor's state and changes between requests) is
// dropped instead of failing the request.
//
// A budget turns thinking on, and then every reply of a tool loop must start
// with its thinking block; when one of them has none (p_loop_lacks_thinking),
// this request goes without thinking rather than be refused.
void AnthropicProvider::_add_thinking(Dictionary &r_body, bool p_replays_thinking, bool p_loop_lacks_thinking) const {
	// Below this the API refuses a budget; it must also stay under max_tokens.
	static const int MIN_BUDGET_TOKENS = 1024;
	const String choice = get_effective_thinking();
	Dictionary thinking_mode;
	if (choice == THINKING_OFF) {
		thinking_mode["type"] = "disabled";
	} else if (is_thinking_budget(choice)) {
		if (p_loop_lacks_thinking) {
			return;
		}
		const int budget = MIN(choice.to_int(), max_tokens - MIN_BUDGET_TOKENS);
		if (budget >= MIN_BUDGET_TOKENS) {
			thinking_mode["type"] = "enabled";
			thinking_mode["budget_tokens"] = budget;
		}
	} else if (!choice.is_empty()) {
		Dictionary output_config;
		output_config["effort"] = choice;
		r_body["output_config"] = output_config;
	}
	if (p_replays_thinking) {
		if (!thinking_mode.has("type")) {
			thinking_mode["type"] = "adaptive"; // What the replayed blocks came from.
		}
		Dictionary binding;
		binding["prefix_mismatch_behavior"] = "drop_block";
		thinking_mode["block_binding"] = binding;
	}
	if (thinking_mode.has("type")) {
		r_body["thinking"] = thinking_mode;
	}
}

bool AnthropicProvider::wants_reasoning_back() const {
	return replay_thinking && get_model_spec().thinks() && get_effective_thinking() != THINKING_OFF;
}

bool AnthropicProvider::stop_replaying_reasoning() {
	if (!last_body_replayed_thinking) {
		return false;
	}
	replay_thinking = false;
	last_body_replayed_thinking = false;
	return true;
}

String AnthropicProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
	// No thinking setting: this path serves short side requests (the history
	// summary), whose small output cap a thinking budget would exceed.
	body["system"] = p_system_prompt;

	Array messages = _to_wire_messages(p_messages, false, false);

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
	if (last_body_replayed_thinking) {
		headers.push_back("anthropic-beta: thinking-binding-controls-2026-08-01"); // For block_binding.
	}
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
	if (!_listed_default(p_models).is_empty()) {
		return _listed_default(p_models);
	}
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
				// The second segment is the minor version only when it is short:
				// in "claude-opus-4-20250514" it is a date, not minor 20250514.
				const float minor = segs[1].length() <= 2 ? segs[1].to_float() : 0.0f;
				float ver = segs[0].to_float() + minor * 0.1f;
				if (ver > best_ver || (ver == best_ver && (best.is_empty() || m.length() < best.length()))) {
					best_ver = ver;
					best = m;
				}
			}
		}
	}

	if (best.is_empty()) {
		best = _newest_known_of(p_models);
	}
	return best.is_empty() ? get_default_model() : best;
}

// --- Streaming ---

String AnthropicProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	Dictionary body;
	body["model"] = model.is_empty() ? get_default_model() : model;
	body["max_tokens"] = max_tokens;
	body["stream"] = true;

	// Prompt caching: split system prompt at the CACHE_BOUNDARY marker.
	// Content before the boundary is static (same every turn) — mark it with
	// cache_control so Anthropic can cache it across requests, reducing both
	// latency and input token cost.
	// Content after the boundary is dynamic (editor context, performance data).
	const String boundary = CACHE_BOUNDARY;
	int boundary_pos = p_system_prompt.find(boundary);
	if (boundary_pos > 0) {
		String static_part  = p_system_prompt.left(boundary_pos);
		String dynamic_part = p_system_prompt.substr(boundary_pos + boundary.length());

		Dictionary static_block;
		static_block["type"] = "text";
		static_block["text"] = static_part;
		Dictionary cc;
		cc["type"] = "ephemeral"; // Anthropic's cache_control type for prefix caching
		static_block["cache_control"] = cc;

		Array system_blocks;
		system_blocks.push_back(static_block);

		if (!dynamic_part.strip_edges().is_empty()) {
			Dictionary dynamic_block;
			dynamic_block["type"] = "text";
			dynamic_block["text"] = dynamic_part;
			system_blocks.push_back(dynamic_block);
		}

		body["system"] = system_blocks;
	} else {
		// No boundary found — send as plain string (backwards compatibility).
		body["system"] = p_system_prompt;
	}

	// Thinking goes back only where it came from a known way of thinking:
	// adaptive (a model with effort levels) or a chosen budget.
	const bool budget_chosen = is_thinking_budget(get_effective_thinking());
	const bool in_loop = p_user_message.is_empty();
	const bool may_replay = replay_thinking && (budget_chosen || !get_model_spec().efforts.is_empty());
	bool replays_thinking = false;
	bool loop_lacks_thinking = false;
	Array messages = _to_wire_messages(p_messages, in_loop, may_replay, &replays_thinking, &loop_lacks_thinking);
	last_body_replayed_thinking = replays_thinking;
	_add_thinking(body, replays_thinking, loop_lacks_thinking);

	if (!tools.is_empty()) {
		Array offered;
		for (const AIToolSpec &tool : tools) {
			Dictionary entry;
			entry["name"] = tool.name;
			entry["description"] = tool.description;
			entry["input_schema"] = tool.parameters;
			offered.push_back(entry);
		}
		body["tools"] = offered;
	}

	// After a tool round there is nothing new from the user: the model
	// carries on from the results. A screenshot a tool took joins them, in
	// the same turn.
	if (p_user_message.is_empty() && !tools.is_empty() && pending_image_png_b64.is_empty()) {
		body["messages"] = messages;
		return JSON::stringify(body, "", false);
	}
	const bool continues_tool_round = !tools.is_empty() && !messages.is_empty() &&
			Dictionary(messages[messages.size() - 1]).get("content", Variant()).get_type() == Variant::ARRAY &&
			String(Dictionary(messages[messages.size() - 1]).get("role", "")) == "user";

	Dictionary user_msg;
	user_msg["role"] = "user";
	if (pending_image_png_b64.is_empty()) {
		user_msg["content"] = p_user_message;
	} else {
		Dictionary source;
		source["type"] = "base64";
		source["media_type"] = "image/png";
		source["data"] = pending_image_png_b64;
		Dictionary image_block;
		image_block["type"] = "image";
		image_block["source"] = source;
		Dictionary text_block;
		text_block["type"] = "text";
		text_block["text"] = p_user_message;
		Array content_blocks;
		content_blocks.push_back(image_block);
		content_blocks.push_back(text_block);
		user_msg["content"] = content_blocks;
		if (continues_tool_round) {
			Dictionary results = messages[messages.size() - 1];
			Array blocks = results["content"];
			blocks.append_array(content_blocks);
			results["content"] = blocks;
			messages[messages.size() - 1] = results;
			body["messages"] = messages;
			return JSON::stringify(body, "", false);
		}
	}
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
		} else if (delta_type == "signature_delta") {
			result["signature"] = delta.get("signature", "");
		} else if (delta_type == "text_delta") {
			result["content"] = delta.get("text", "");
		} else if (delta_type == "input_json_delta") {
			// The arguments of the tool call in this content block, a piece at a time.
			Dictionary fragment;
			fragment["index"] = data.get("index", 0);
			fragment["arguments"] = delta.get("partial_json", "");
			Array fragments;
			fragments.push_back(fragment);
			result["tool_calls"] = fragments;
		}
	} else if (type == "content_block_start") {
		const Dictionary block = data.get("content_block", Dictionary());
		if (String(block.get("type", "")) == "redacted_thinking") {
			result["redacted_thinking"] = true;
		} else if (String(block.get("type", "")) == "tool_use") {
			Dictionary fragment;
			fragment["index"] = data.get("index", 0);
			fragment["id"] = block.get("id", "");
			fragment["name"] = block.get("name", "");
			fragment["arguments"] = String();
			Array fragments;
			fragments.push_back(fragment);
			result["tool_calls"] = fragments;
		}
	} else if (type == "message_delta") {
		const Dictionary delta = data.get("delta", Dictionary());
		const Variant reason = delta.get("stop_reason", Variant());
		if (reason.get_type() == Variant::STRING && String(reason) == "max_tokens") {
			result["finish"] = FINISH_LENGTH;
		} else if (reason.get_type() == Variant::STRING && String(reason) == "refusal") {
			result["finish"] = FINISH_BLOCKED;
		}
	} else if (type == "error") {
		const Variant error = data.get("error", Variant());
		result["error"] = error.get_type() == Variant::DICTIONARY ? String(Dictionary(error).get("message", "Unknown error")) : String("Unknown error");
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
