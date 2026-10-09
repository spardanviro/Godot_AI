#pragma once

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

	static void _bind_methods();

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

	// Model metadata — context window and recommended output tokens.
	virtual int get_model_context_length() const;
	virtual int get_recommended_max_tokens() const;

	// --- Streaming support ---
	// Build the HTTP request body with streaming enabled.
	virtual String build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const;
	// Parse a single SSE data payload and return the text delta (empty if no text).
	virtual String parse_stream_delta(const String &p_data) const;
	// Parse SSE data returning both content and thinking deltas.
	// Returns Dictionary with "content" (String) and "thinking" (String).
	virtual Dictionary parse_stream_delta_ex(const String &p_data) const;
	// Check if an SSE data payload signals end of stream.
	virtual bool is_stream_done_marker(const String &p_data) const;
	// Get the streaming URL (may differ from regular URL, e.g. Gemini).
	virtual String get_stream_url() const;

	// Models listing API.
	virtual String get_models_list_url() const;
	virtual Vector<String> get_models_list_headers() const;
	virtual PackedStringArray parse_models_list(const String &p_response_body) const;
	virtual String select_best_model(const PackedStringArray &p_models) const;

	// Strip the cache boundary marker from a system prompt.
	// Call this in every provider that doesn't implement native prompt caching
	// so the HTML comment never leaks into the API payload.
	static String strip_cache_boundary(const String &p_prompt);

	AIProvider();
	virtual ~AIProvider();
};
