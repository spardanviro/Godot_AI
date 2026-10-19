#pragma once

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// What is known about a model: its context window, output limit, whether it
// takes a temperature, and which thinking settings it has. The facts come
// from models.dev (https://models.dev), which tracks them per provider and
// model. A copy is built into the editor (data/model_specs.json); a newer one,
// downloaded by AIModelSpecsUpdater, is kept in the editor's config folder and
// replaces it once it is there.
struct AIModelSpec {
	bool found = false;
	int context = 0; // Tokens the model reads at most, prompt and history included.
	int output = 0; // Tokens it writes at most in one reply, thinking included.
	bool temperature = true; // Accepts a sampling temperature.
	bool can_turn_off_thinking = false;
	// Thinking levels in the provider's own words and order ("low", "high",
	// "max"; "minimal", ...; "none"). Empty: none can be chosen.
	PackedStringArray efforts;
	// A thinking budget in tokens can be set (older models); 0: not this way.
	int budget_min = 0;
	int budget_max = 0; // 0: no upper bound given.
	bool has_budget = false;
	bool deprecated = false;
	String family; // models.dev's series name: "claude-opus", "gpt-sol".
	String released; // "2026-08-12".

	bool thinks() const { return can_turn_off_thinking || !efforts.is_empty() || has_budget; }
};

// Main thread only: the data is shared and not locked. filter() is the
// exception; it touches no shared state and may run on a worker.
class AIModelSpecs {
public:
	// How old the downloaded copy may be before a new one is fetched.
	static constexpr int MAX_AGE_DAYS = 7;
	static constexpr const char *SOURCE_URL = "https://models.dev/api.json";

	// The model as one of the assistant's providers ("anthropic", "openai",
	// "gemini", "deepseek", "glm") calls it. A name with a vendor prefix
	// ("deepseek-ai/DeepSeek-V4-Pro", "models/gemini-3-pro") is looked up
	// by what follows it; a name the provider does not list is looked for
	// with the others, which is what a relay serving another provider's
	// models needs.
	static AIModelSpec find(const String &p_provider_id, const String &p_model);
	// The provider's current chat models that call tools, newest first, at
	// most p_max: no deprecated ones and no dated snapshots.
	static PackedStringArray get_models(const String &p_provider_id, int p_max);
	// The newest of the provider's current models in one of p_families
	// (models.dev series names), for the model to use by default. Empty if
	// there is none.
	static String get_newest(const String &p_provider_id, const Vector<String> &p_families);
	// Of p_models (a provider's list), the newest current one the data knows,
	// preferring p_families. Empty if it knows none of them.
	static String get_newest_listed(const String &p_provider_id, const PackedStringArray &p_models, const Vector<String> &p_families);

	// The part of models.dev's api.json the editor keeps, as JSON text in the
	// shape of data/model_specs.json. Empty if p_api_json is not that file.
	static String filter(const String &p_api_json);
	// Replaces the data with p_json (the filtered shape). False, and nothing
	// changes, if it does not parse or holds none of the providers.
	static bool use(const String &p_json);
	// Back to the built-in copy, ignoring the downloaded one (tests).
	static void use_builtin();
	// The built-in copy, then the downloaded one over it if it reads.
	static void reload();

	// <editor config>/ai_assistant/model_specs.json. Empty outside the editor.
	static String get_cache_path();
	// Tests: a scratch file instead of the editor's. Empty: back to normal.
	static void set_cache_path_override(const String &p_path) { cache_path_override = p_path; }
	// Whether the downloaded copy is missing or older than MAX_AGE_DAYS.
	static bool is_cache_stale();
	// Writes p_json (filtered) as the downloaded copy and uses it.
	static bool store_cache(const String &p_json);

private:
	static Dictionary data; // models.dev provider id -> {"models": {id: {...}}}.
	static bool loaded;
	static String cache_path_override;
	static void _ensure_loaded();
};
