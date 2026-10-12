#pragma once

#include "openai_compatible_provider.h"

class OpenAIProvider : public OpenAICompatibleProvider {
	GDCLASS(OpenAIProvider, OpenAICompatibleProvider);

protected:
	static void _bind_methods() {}

	virtual String _get_max_tokens_field() const override { return "max_completion_tokens"; }
	virtual bool _accepts_listed_model(const String &p_id) const override;
	virtual Vector<String> _get_preferred_models() const override;
	virtual bool _falls_back_to_first_listed() const override { return false; }
	virtual String _get_required_base_suffix() const override { return "/v1"; }

public:
	virtual String get_provider_name() const override { return "openai"; }
	virtual bool supports_image_input() const override { return true; }
	virtual String get_default_endpoint() const override { return "https://api.openai.com/v1/chat/completions"; }
	virtual String get_default_model() const override { return "gpt-5.4"; }
	virtual Vector<String> get_headers() const override;

	virtual int get_model_context_length() const override;
	virtual int get_recommended_max_tokens() const override;
};
