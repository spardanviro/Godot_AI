#pragma once

#include "../ai_provider.h"

// DeepSeek API provider — OpenAI-compatible chat completions.
// Default: streaming enabled.
class DeepSeekProvider : public AIProvider {
	GDCLASS(DeepSeekProvider, AIProvider);

protected:
	static void _bind_methods();

public:
	virtual String get_provider_name() const override;
	virtual String get_default_endpoint() const override;
	virtual String get_default_model() const override;

	virtual String build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const override;
	virtual String parse_response(const String &p_response_body) const override;
	virtual Vector<String> get_headers() const override;

	virtual String get_stream_url() const override;
	virtual String get_models_list_url() const override;
	virtual Vector<String> get_models_list_headers() const override;
	virtual PackedStringArray parse_models_list(const String &p_response_body) const override;
	virtual String select_best_model(const PackedStringArray &p_models) const override;

	virtual int get_model_context_length() const override;
	virtual int get_recommended_max_tokens() const override;

	// Streaming.
	virtual String build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const override;
	virtual String parse_stream_delta(const String &p_data) const override;
	virtual Dictionary parse_stream_delta_ex(const String &p_data) const override;
	virtual bool is_stream_done_marker(const String &p_data) const override;

	DeepSeekProvider();
};
