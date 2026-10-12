#include "../ai_provider.h"

#include "anthropic_provider.h"
#include "deepseek_provider.h"
#include "gemini_provider.h"
#include "glm_provider.h"
#include "openai_provider.h"

Vector<String> AIProvider::get_provider_ids() {
	return { "anthropic", "openai", "gemini", "glm", "deepseek" };
}

Ref<AIProvider> AIProvider::create(const String &p_provider_id) {
	if (p_provider_id == "openai") {
		return memnew(OpenAIProvider);
	}
	if (p_provider_id == "gemini") {
		return memnew(GeminiProvider);
	}
	if (p_provider_id == "glm") {
		return memnew(GLMProvider);
	}
	if (p_provider_id == "deepseek") {
		return memnew(DeepSeekProvider);
	}
	return memnew(AnthropicProvider);
}
