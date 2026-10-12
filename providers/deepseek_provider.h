#pragma once

#include "openai_compatible_provider.h"

class DeepSeekProvider : public OpenAICompatibleProvider {
	GDCLASS(DeepSeekProvider, OpenAICompatibleProvider);

protected:
	static void _bind_methods() {}

	virtual bool _accepts_listed_model(const String &p_id) const override { return p_id.begins_with("deepseek-"); }
	virtual Vector<String> _get_preferred_models() const override { return { "deepseek-chat" }; }

public:
	virtual String get_provider_name() const override { return "deepseek"; }
	virtual String get_default_endpoint() const override { return "https://api.deepseek.com/chat/completions"; }
	virtual String get_default_model() const override { return "deepseek-chat"; }

	// Both current DeepSeek models use a 128K context.
	virtual int get_model_context_length() const override { return 128000; }
	// deepseek-reasoner supports up to 64K output; deepseek-chat up to 8K.
	virtual int get_recommended_max_tokens() const override { return _get_effective_model() == "deepseek-reasoner" ? 32768 : 8192; }
};
