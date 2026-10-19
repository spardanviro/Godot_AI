#pragma once

#include "../common/ai_tool_types.h"
#include "ai_model_specs.h"

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

class AIProvider : public RefCounted {
	GDCLASS(AIProvider, RefCounted);

protected:
	String api_key;
	String model;
	String api_endpoint;
	int max_tokens = 4096;
	float temperature = 0.7f;
	// Base64 PNG to attach to the next user message (empty = none). Set by the
	// panel right before building a request and cleared right after.
	String pending_image_png_b64;
	// Some reasoning models only accept their default sampling temperature and
	// answer 400 to any explicit value; the panel turns this off when that happens.
	bool send_temperature = true;
	// Tools offered to the model with each request. Empty: none.
	Vector<AIToolSpec> tools;
	String thinking; // See set_thinking().

	static void _bind_methods();

	String _model_or_default() const { return model.is_empty() ? get_default_model() : model; }
	// What the provider assumes about a model models.dev does not list.
	virtual int _table_context_length() const { return 8192; }
	virtual int _table_max_tokens() const { return 4096; }
	// The newest current model of these models.dev series is the default,
	// so the default moves on when the data does; p_fallback when none is listed.
	String _newest_model_or(const Vector<String> &p_families, const String &p_fallback) const {
		const String newest = AIModelSpecs::get_newest(get_provider_name(), p_families);
		return newest.is_empty() ? p_fallback : newest;
	}
	// The provider's flagship series (models.dev names), for choosing the default.
	virtual Vector<String> _get_flagship_families() const { return Vector<String>(); }
	// Choosing from a listed set goes: the default, if it is listed; else
	// what the names say (a generation newer than the model facts shows up
	// there first); else the newest listed model the facts know, the
	// flagship series first. Each is empty when it has nothing to offer.
	String _listed_default(const PackedStringArray &p_models) const {
		const String default_model = get_default_model();
		return p_models.has(default_model) ? default_model : String();
	}
	String _newest_known_of(const PackedStringArray &p_models) const {
		return AIModelSpecs::get_newest_listed(get_provider_name(), p_models, _get_flagship_families());
	}

public:
	void set_api_key(const String &p_key);
	String get_api_key() const;

	void set_model(const String &p_model);
	String get_model() const;

	void set_api_endpoint(const String &p_endpoint);
	String get_api_endpoint() const;

	void set_max_tokens(int p_max);
	int get_max_tokens() const;

	void set_temperature(float p_temp);
	float get_temperature() const;

	void set_pending_image(const String &p_png_base64) { pending_image_png_b64 = p_png_base64; }
	void set_send_temperature(bool p_send) { send_temperature = p_send; }
	bool is_sending_temperature() const { return send_temperature; }
	// Whether requests can offer tools and the stream can carry calls to
	// them. With tools set, the history may hold the two kinds of message
	// AISession writes for a tool round — an assistant message with
	// "tool_calls" ([{id, name, arguments}]) and "tool" messages with
	// "tool_call_id" — and an empty user message means "continue": no user
	// turn is added.
	virtual bool supports_tools() const { return false; }
	void set_tools(const Vector<AIToolSpec> &p_tools) { tools = p_tools; }
	bool has_tools() const { return !tools.is_empty(); }
	// Whether build_stream_request_body() can attach pending_image_png_b64.
	virtual bool supports_image_input() const { return false; }

	virtual String get_provider_name() const;
	virtual String get_default_endpoint() const;
	virtual String get_default_model() const;

	// Build the HTTP request body as JSON string.
	virtual String build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const;

	// Parse the response body and return the assistant message text.
	virtual String parse_response(const String &p_response_body) const;

	// Get required HTTP headers.
	virtual Vector<String> get_headers() const;

	// --- What is known about the model (AIModelSpecs, else the provider's table) ---
	AIModelSpec get_model_spec() const;
	int get_model_context_length() const;
	// The output cap to use: the model's own limit, unless that would leave
	// less than three quarters of the context window for the prompt and history.
	int get_recommended_max_tokens() const;
	// Whether the model accepts a sampling temperature at all.
	bool supports_temperature() const;

	// --- Thinking ---
	// What can be chosen for the model, in the provider's words: THINKING_OFF
	// first when thinking can be switched off, then its levels — or, for a
	// model that takes a thinking budget instead, budgets in tokens ("16384").
	// Empty: nothing can be chosen; the model thinks (or not) as by default.
	PackedStringArray get_thinking_options() const;
	// "" (the model's default), THINKING_OFF or one of the options.
	void set_thinking(const String &p_choice) { thinking = p_choice; }
	String get_thinking() const { return thinking; }
	// The choice if the model offers it, else "": what requests are built with.
	String get_effective_thinking() const;
	static constexpr const char *THINKING_OFF = "off";
	// Whether a choice is a thinking budget, and how many tokens it is.
	static bool is_thinking_budget(const String &p_choice) { return p_choice.is_valid_int() && p_choice.to_int() > 0; }

	// Whether earlier replies have to be sent back with their reasoning.
	// DeepSeek's and GLM's thinking modes need it in a tool loop; Claude's
	// thinking goes back with its signature.
	virtual bool wants_reasoning_back() const { return false; }
	// After a refused request: if the last one sent earlier reasoning back in
	// a form the provider may refuse (Claude's signed blocks), stop doing that
	// and return true, so the request can be sent again without it.
	virtual bool stop_replaying_reasoning() { return false; }

	// --- Streaming support ---
	// Build the HTTP request body with streaming enabled.
	virtual String build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const;
	// Parse a single SSE data payload and return the text delta (empty if no text).
	virtual String parse_stream_delta(const String &p_data) const;
	// Parse SSE data returning both content and thinking deltas.
	// Returns Dictionary with "content" (String) and "thinking" (String).
	// Two optional keys say how the reply ended, when the payload does:
	//   "finish": FINISH_LENGTH (cut off at the output limit) or
	//             FINISH_BLOCKED (withheld by the provider's filter);
	//   "error":  the message of an error reported inside the stream.
	// And "tool_calls", when the payload carries fragments of tool calls:
	// an Array of {index, id, name, arguments}, for AIToolCallAccumulator —
	// or of {whole: true, name, arguments, provider_data} for a provider
	// that sends each call in one piece.
	virtual Dictionary parse_stream_delta_ex(const String &p_data) const;
	// Check if an SSE data payload signals end of stream.
	virtual bool is_stream_done_marker(const String &p_data) const;
	// Get the streaming URL (may differ from regular URL, e.g. Gemini).
	virtual String get_stream_url() const;
	// URL for a plain (non-streaming) request built with build_request_body().
	virtual String get_request_url() const { return get_stream_url(); }

	static constexpr const char *FINISH_LENGTH = "length";
	static constexpr const char *FINISH_BLOCKED = "blocked";

	// Models listing API.
	virtual String get_models_list_url() const;
	virtual Vector<String> get_models_list_headers() const;
	virtual PackedStringArray parse_models_list(const String &p_response_body) const;
	virtual String select_best_model(const PackedStringArray &p_models) const;

	// Marks, inside a system prompt, where the content that is the same on
	// every turn ends. A provider with prompt caching splits there.
	static constexpr const char *CACHE_BOUNDARY = "<!-- GODOTAI_CACHE_BOUNDARY -->";
	// Strip the cache boundary marker from a system prompt.
	// Call this in every provider that doesn't implement native prompt caching
	// so the HTML comment never leaks into the API payload.
	static String strip_cache_boundary(const String &p_prompt);

	// Provider ids in the order they are offered in the settings dialog.
	static Vector<String> get_provider_ids();
	// Creates the provider for an id from get_provider_ids(); unknown ids get
	// the default provider.
	static Ref<AIProvider> create(const String &p_provider_id);

	AIProvider();
	virtual ~AIProvider();
};
