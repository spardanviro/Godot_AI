#include "../ai_provider_catalog.h"

#include "../ai_provider.h"

namespace {

struct Entry {
	const char *id;
	const char *display_name; // UTF-8.
	const char *known_models[16]; // Null-terminated; the provider's default first.
};

const Entry ENTRIES[] = {
	{ "anthropic", "Anthropic (Claude)",
			{ "claude-opus-5-5", "claude-opus-5", "claude-opus-4-8", "claude-opus-4-7", "claude-opus-4-6",
					"claude-sonnet-5-5", "claude-sonnet-5", "claude-sonnet-4-6", "claude-haiku-5-5", "claude-haiku-4-5",
					"claude-fable-5-1", nullptr } },
	{ "openai", "OpenAI (ChatGPT)",
			{ "gpt-5.4", "gpt-5.4-mini", "gpt-5.4-pro", "gpt-5.4-nano", "gpt-5.3-codex", "gpt-5.2-codex", "gpt-5.2",
					"gpt-5.1-codex-max", "gpt-5.1-codex", "gpt-5.1-codex-mini", "gpt-5.1", "gpt-5-codex",
					"gpt-5-codex-mini", "gpt-5", nullptr } },
	{ "gemini", "Google (Gemini)",
			{ "gemini-3.1-pro-preview", "gemini-3-flash-preview", "gemini-3.1-flash-lite-preview", nullptr } },
	{ "glm", "\xe6\x99\xba\xe8\xb0\xb1 (GLM)", // 智谱
			{ "glm-5.1", "glm-5", "glm-5-turbo", nullptr } },
	{ "deepseek", "DeepSeek",
			{ "deepseek-chat", "deepseek-reasoner", nullptr } },
};

const int ENTRY_COUNT = sizeof(ENTRIES) / sizeof(ENTRIES[0]);

} // namespace

int AIProviderCatalog::get_count() {
	return ENTRY_COUNT;
}

String AIProviderCatalog::get_id(int p_index) {
	return p_index >= 0 && p_index < ENTRY_COUNT ? String(ENTRIES[p_index].id) : String();
}

int AIProviderCatalog::find(const String &p_id) {
	for (int i = 0; i < ENTRY_COUNT; i++) {
		if (p_id == ENTRIES[i].id) {
			return i;
		}
	}
	return 0;
}

String AIProviderCatalog::get_display_name(int p_index) {
	return p_index >= 0 && p_index < ENTRY_COUNT ? String::utf8(ENTRIES[p_index].display_name) : String();
}

PackedStringArray AIProviderCatalog::get_known_models(int p_index) {
	PackedStringArray models;
	if (p_index < 0 || p_index >= ENTRY_COUNT) {
		return models;
	}
	for (int i = 0; ENTRIES[p_index].known_models[i]; i++) {
		models.push_back(ENTRIES[p_index].known_models[i]);
	}
	return models;
}
