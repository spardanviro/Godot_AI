#pragma once

#include "../ai_provider.h"

// Shared implementation of the OpenAI chat-completions wire format. OpenAI,
// DeepSeek and GLM all speak it; each concrete provider only supplies what
// actually differs (endpoint, model tables, a few request details).
class OpenAICompatibleProvider : public AIProvider {
	GDCLASS(OpenAICompatibleProvider, AIProvider);

protected:
	static void _bind_methods() {}

	// Name of the request field that caps output tokens.
	virtual String _get_max_tokens_field() const { return "max_tokens"; }
	// Whether a model id returned by the /models endpoint should be offered.
	virtual bool _accepts_listed_model(const String &p_id) const { return true; }
	// Models to pick first when auto-selecting, best first.
	virtual Vector<String> _get_preferred_models() const { return Vector<String>(); }
	// When no preferred model is listed: take the first listed one (true) or
	// fall back to the default model (false).
	virtual bool _falls_back_to_first_listed() const { return true; }
	// Custom endpoints given as a bare host get this appended ("/v1" for OpenAI).
	virtual String _get_required_base_suffix() const { return String(); }

	String _get_effective_model() const { return model.is_empty() ? get_default_model() : model; }
	// Whether the endpoint is another service's (a relay, an aggregator, a
	// local server) and not the provider's own host. Such a service has its
	// own models, which the provider's tables say nothing about.
	bool _is_third_party_endpoint() const;
	// The endpoint without a trailing "/chat/completions" or "/models".
	String _get_base_url() const;
	String _build_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message, bool p_stream) const;
	// Returns the first choice's "message" (p_delta false) or "delta" (true).
	static bool _get_first_choice(const String &p_json, bool p_delta, Dictionary &r_out, String *r_error = nullptr);

public:
	virtual String build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const override;
	virtual String parse_response(const String &p_response_body) const override;
	virtual Vector<String> get_headers() const override;

	virtual String get_stream_url() const override;
	virtual String get_models_list_url() const override;
	virtual Vector<String> get_models_list_headers() const override;
	virtual PackedStringArray parse_models_list(const String &p_response_body) const override;
	virtual String select_best_model(const PackedStringArray &p_models) const override;

	virtual String build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const override;
	virtual String parse_stream_delta(const String &p_data) const override;
	virtual Dictionary parse_stream_delta_ex(const String &p_data) const override;
	virtual bool is_stream_done_marker(const String &p_data) const override;
};
