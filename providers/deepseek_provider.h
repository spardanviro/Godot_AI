#pragma once

#include "openai_compatible_provider.h"

class DeepSeekProvider : public OpenAICompatibleProvider {
	GDCLASS(DeepSeekProvider, OpenAICompatibleProvider);

	// deepseek-chat and deepseek-reasoner are the V3 names. The V4 models
	// (deepseek-v4-pro, deepseek-flash) think by default.
	bool _is_v3_model() const {
		const String model_name = _get_effective_model();
		return model_name == "deepseek-chat" || model_name == "deepseek-reasoner";
	}

protected:
	static void _bind_methods() {}

	virtual bool _accepts_listed_model(const String &p_id) const override { return p_id.begins_with("deepseek-"); }
	virtual Vector<String> _get_preferred_models() const override { return { "deepseek-v4-pro", "deepseek-flash" }; }
	virtual void _add_thinking(Dictionary &r_body) const override { _add_thinking_switch(r_body); }
	virtual Vector<String> _get_flagship_families() const override { return { "deepseek-thinking" }; }

	// For a model models.dev does not list. V4: 1M context, up to 384K
	// output. V3: 128K context; deepseek-reasoner up to 64K output,
	// deepseek-chat up to 8K.
	virtual int _table_context_length() const override { return _is_v3_model() ? 128000 : 1000000; }
	virtual int _table_max_tokens() const override {
		if (_is_v3_model()) {
			return _get_effective_model() == "deepseek-reasoner" ? 32768 : 8192;
		}
		return 131072;
	}

public:
	virtual String get_provider_name() const override { return "deepseek"; }
	virtual String get_default_endpoint() const override { return "https://api.deepseek.com/chat/completions"; }
	virtual String get_default_model() const override { return _newest_model_or(_get_flagship_families(), "deepseek-v4-pro"); }

	// In thinking mode a request with tools is refused (400) unless every
	// earlier assistant message carries its reasoning_content.
	// A model nothing is known about gets nothing extra.
	virtual bool wants_reasoning_back() const override { return get_model_spec().thinks() && get_effective_thinking() != THINKING_OFF; }
};
