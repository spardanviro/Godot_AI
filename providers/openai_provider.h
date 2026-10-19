#pragma once

#include "openai_compatible_provider.h"

class OpenAIProvider : public OpenAICompatibleProvider {
	GDCLASS(OpenAIProvider, OpenAICompatibleProvider);

protected:
	static void _bind_methods() {}

	// For a model models.dev does not list.
	virtual int _table_context_length() const override;
	virtual int _table_max_tokens() const override;

	virtual Vector<String> _get_flagship_families() const override { return { "gpt-sol" }; }
	virtual String _get_max_tokens_field() const override { return "max_completion_tokens"; }
	virtual bool _accepts_listed_model(const String &p_id) const override;
	virtual String _get_family_prefix() const override { return "gpt-"; }
	virtual Vector<String> _get_preferred_models() const override;
	virtual bool _falls_back_to_first_listed() const override { return false; }
	virtual String _get_required_base_suffix() const override { return "/v1"; }

public:
	virtual String get_provider_name() const override { return "openai"; }
	virtual bool supports_image_input() const override { return true; }
	virtual String get_default_endpoint() const override { return "https://api.openai.com/v1/chat/completions"; }
	virtual String get_default_model() const override { return _newest_model_or(_get_flagship_families(), "gpt-6.1-sol"); }
	virtual Vector<String> get_headers() const override;

};
