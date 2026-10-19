#include "ai_provider_catalog.h"

#include "ai_model_specs.h"
#include "ai_provider.h"

namespace {

struct Entry {
	const char *id;
	const char *display_name; // UTF-8.
	// Null-terminated; the provider's default first. Only used without any
	// model facts (AIModelSpecs), which list the current models.
	const char *known_models[16];
};

const Entry ENTRIES[] = {
	{ "anthropic", "Anthropic (Claude)",
			{ "claude-opus-5-5", "claude-opus-5", "claude-opus-4-8", "claude-opus-4-7", "claude-opus-4-6",
					"claude-sonnet-5-5", "claude-sonnet-5", "claude-sonnet-4-6", "claude-haiku-5-5", "claude-haiku-4-5",
					"claude-fable-5-1", nullptr } },
	{ "openai", "OpenAI (ChatGPT)",
			{ "gpt-6.1-sol", "gpt-6-sol", "gpt-6-astra", "gpt-6-luna", "gpt-5.6-sol", "gpt-5.6-terra", "gpt-5.6-luna",
					"gpt-5.5", "gpt-5.4", nullptr } },
	{ "gemini", "Google (Gemini)",
			{ "gemini-4-argon", "gemini-3.8-flash", "gemini-3.7-flash", "gemini-3.6-flash", "gemini-3.1-pro-preview", nullptr } },
	{ "glm", "\xe6\x99\xba\xe8\xb0\xb1 (GLM)", // 智谱
			{ "glm-5.3", "glm-5.3-flash", "glm-5.2", "glm-5.1", nullptr } },
	{ "deepseek", "DeepSeek",
			{ "deepseek-v4-pro", "deepseek-flash", "deepseek-chat", "deepseek-reasoner", nullptr } },
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
	// Enough for the current generation and the one before.
	static const int MAX_KNOWN_MODELS = 16;
	PackedStringArray models;
	if (p_index < 0 || p_index >= ENTRY_COUNT) {
		return models;
	}
	// The provider's default first, then what models.dev lists, newest first.
	const PackedStringArray listed = AIModelSpecs::get_models(ENTRIES[p_index].id, MAX_KNOWN_MODELS);
	if (!listed.is_empty()) {
		models.push_back(AIProvider::create(ENTRIES[p_index].id)->get_default_model());
		for (const String &name : listed) {
			if (!models.has(name)) {
				models.push_back(name);
			}
		}
		return models;
	}
	// No data on the provider at all: the names this file was written with.
	for (int i = 0; ENTRIES[p_index].known_models[i]; i++) {
		models.push_back(ENTRIES[p_index].known_models[i]);
	}
	return models;
}
