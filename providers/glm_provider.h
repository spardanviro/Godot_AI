#pragma once

#include "openai_compatible_provider.h"

// Zhipu AI GLM.
class GLMProvider : public OpenAICompatibleProvider {
	GDCLASS(GLMProvider, OpenAICompatibleProvider);

protected:
	static void _bind_methods() {}

	virtual bool _accepts_listed_model(const String &p_id) const override;
	virtual String _get_family_prefix() const override { return "glm-"; }
	virtual Vector<String> _get_preferred_models() const override { return { "glm-5.3", "glm-5.2", "glm-5.1" }; }
	virtual void _add_thinking(Dictionary &r_body) const override { _add_thinking_switch(r_body); }
	virtual Vector<String> _get_flagship_families() const override { return { "glm" }; }

	// For a model models.dev does not list.
	virtual int _table_context_length() const override { return _get_effective_model().find("glm-5") >= 0 ? 200000 : 128000; }
	virtual int _table_max_tokens() const override { return _get_effective_model().find("glm-5") >= 0 ? 32768 : 16384; }

public:
	virtual String get_provider_name() const override { return "glm"; }
	virtual String get_default_endpoint() const override { return "https://open.bigmodel.cn/api/coding/paas/v4/chat/completions"; }
	virtual String get_default_model() const override { return _newest_model_or(_get_flagship_families(), "glm-5.3"); }

	// GLM thinks by default, and between tool calls it expects its earlier
	// reasoning back with the results.
	// A model nothing is known about gets nothing extra.
	virtual bool wants_reasoning_back() const override { return get_model_spec().thinks() && get_effective_thinking() != THINKING_OFF; }
};
