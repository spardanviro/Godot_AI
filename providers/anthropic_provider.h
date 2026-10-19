#pragma once

#include "ai_provider.h"

class AnthropicProvider : public AIProvider {
	GDCLASS(AnthropicProvider, AIProvider);

protected:
	static void _bind_methods();

	// For a model models.dev does not list.
	virtual int _table_context_length() const override;
	virtual int _table_max_tokens() const override;
	void _add_thinking(Dictionary &r_body, bool p_replays_thinking, bool p_loop_lacks_thinking) const;
	virtual Vector<String> _get_flagship_families() const override { return { "claude-opus" }; }

	bool replay_thinking = true; // Off for good once a request with it was refused.
	// Whether the body last built sent thinking back; the headers follow it.
	mutable bool last_body_replayed_thinking = false;

public:
	virtual String get_provider_name() const override;
	virtual bool supports_image_input() const override { return true; }
	virtual bool supports_tools() const override { return true; }
	virtual String get_default_endpoint() const override;
	virtual String get_default_model() const override;

	virtual bool wants_reasoning_back() const override;
	virtual bool stop_replaying_reasoning() override;

	virtual String build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const override;
	virtual String parse_response(const String &p_response_body) const override;
	virtual Vector<String> get_headers() const override;

	virtual String get_models_list_url() const override;
	virtual Vector<String> get_models_list_headers() const override;
	virtual PackedStringArray parse_models_list(const String &p_response_body) const override;
	virtual String select_best_model(const PackedStringArray &p_models) const override;

	// Streaming.
	virtual String build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const override;
	virtual String parse_stream_delta(const String &p_data) const override;
	virtual Dictionary parse_stream_delta_ex(const String &p_data) const override;
	virtual bool is_stream_done_marker(const String &p_data) const override;

	AnthropicProvider();
};
