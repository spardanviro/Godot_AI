#pragma once

#include "openai_compatible_provider.h"

// Zhipu AI GLM.
class GLMProvider : public OpenAICompatibleProvider {
	GDCLASS(GLMProvider, OpenAICompatibleProvider);

protected:
	static void _bind_methods() {}

	virtual bool _accepts_listed_model(const String &p_id) const override;
	// glm-5.1 is undocumented but live.
	virtual Vector<String> _get_preferred_models() const override { return { "glm-5.1", "glm-5", "glm-5-turbo" }; }

public:
	virtual String get_provider_name() const override { return "glm"; }
	virtual String get_default_endpoint() const override { return "https://open.bigmodel.cn/api/coding/paas/v4/chat/completions"; }
	virtual String get_default_model() const override { return "glm-5.1"; }

	virtual int get_model_context_length() const override { return _get_effective_model().find("glm-5") >= 0 ? 200000 : 128000; }
	virtual int get_recommended_max_tokens() const override { return _get_effective_model().find("glm-5") >= 0 ? 32768 : 16384; }
};
