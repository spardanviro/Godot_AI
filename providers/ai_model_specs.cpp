#include "ai_model_specs.h"

#include "ai_model_names.h"
#include "ai_model_specs_data.gen.h"

#include "core/io/compression.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/time.h"
#include "core/templates/hash_set.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_paths.h"
#endif

Dictionary AIModelSpecs::data;
bool AIModelSpecs::loaded = false;
String AIModelSpecs::cache_path_override;

// Larger than any real copy; a bigger file is not read.
static const int64_t MAX_CACHE_BYTES = 8 * 1024 * 1024;
// No model reads or writes more than this; larger numbers are not believed.
static const double MAX_TOKENS_BELIEVED = 4000000.0;
// Longest model id, series name or thinking level kept.
static const int MAX_NAME_LENGTH = 100;

// models.dev's provider ids for each of the assistant's providers, the one
// whose endpoint is the default first. Kept in step with update_model_specs.py.
struct ProviderSources {
	const char *provider_id;
	const char *sources[3];
};
static const ProviderSources PROVIDER_SOURCES[] = {
	{ "anthropic", { "anthropic", nullptr } },
	{ "openai", { "openai", nullptr } },
	{ "gemini", { "google", nullptr } },
	{ "deepseek", { "deepseek", nullptr } },
	{ "glm", { "zhipuai-coding-plan", "zhipuai", nullptr } },
};
static const char *MODEL_KEYS[] = { "limit", "temperature", "reasoning_options", "release_date", "status", "tool_call", "family", nullptr };

static Vector<String> _sources_of(const String &p_provider_id) {
	Vector<String> sources;
	for (const ProviderSources &entry : PROVIDER_SOURCES) {
		if (p_provider_id == entry.provider_id) {
			for (int i = 0; entry.sources[i]; i++) {
				sources.push_back(entry.sources[i]);
			}
		}
	}
	return sources;
}

static Vector<String> _all_sources() {
	Vector<String> sources;
	for (const ProviderSources &entry : PROVIDER_SOURCES) {
		for (int i = 0; entry.sources[i]; i++) {
			sources.push_back(entry.sources[i]);
		}
	}
	return sources;
}

static Variant _parse_json(const String &p_text) {
	Ref<JSON> json;
	json.instantiate();
	return json->parse(p_text) == OK ? json->get_data() : Variant();
}

// A token count, or 0 when it is missing or not believable.
static int _tokens_of(const Dictionary &p_dict, const String &p_key) {
	const Variant value = p_dict.get(p_key, Variant());
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		return 0;
	}
	const double tokens = value;
	return Math::is_finite(tokens) && tokens > 0 && tokens <= MAX_TOKENS_BELIEVED ? int(tokens) : 0;
}

// A name as models.dev writes them: letters, digits and "._:-". A model id
// goes into a URL path (Gemini), so anything else is not taken.
static bool _is_plain_name(const String &p_name) {
	if (p_name.is_empty() || p_name.length() > MAX_NAME_LENGTH) {
		return false;
	}
	for (int i = 0; i < p_name.length(); i++) {
		const char32_t c = p_name[i];
		if (!is_ascii_alphanumeric_char(c) && c != '.' && c != '_' && c != ':' && c != '-') {
			return false;
		}
	}
	return true;
}

// "2026-08-12" or "2026-08"; anything else is dropped (it orders the models).
static bool _is_date(const String &p_text) {
	if (p_text.length() != 7 && p_text.length() != 10) {
		return false;
	}
	for (int i = 0; i < p_text.length(); i++) {
		const bool dash = i == 4 || i == 7;
		if (dash ? p_text[i] != '-' : !is_digit(p_text[i])) {
			return false;
		}
	}
	return true;
}

// The data with what cannot be true or safe taken out: ids that are not
// plain names, malformed dates and series names, output limits above the
// context window. Applies to the built-in copy, the download and the cache alike.
static Dictionary _cleaned(const Dictionary &p_data) {
	Dictionary cleaned;
	for (const Variant &source : p_data.keys()) {
		const Dictionary models = p_data[source].get_type() == Variant::DICTIONARY ? Dictionary(Dictionary(p_data[source]).get("models", Dictionary())) : Dictionary();
		Dictionary kept;
		for (const Variant &id : models.keys()) {
			if (id.get_type() != Variant::STRING || !_is_plain_name(id) || models[id].get_type() != Variant::DICTIONARY) {
				continue;
			}
			Dictionary model = Dictionary(models[id]).duplicate(true);
			if (!_is_date(model.get("release_date", ""))) {
				model.erase("release_date");
			}
			if (!_is_plain_name(model.get("family", ""))) {
				model.erase("family");
			}
			const Dictionary limit = model.get("limit", Dictionary());
			const Variant context = limit.get("context", Variant());
			const Variant output = limit.get("output", Variant());
			if ((context.get_type() == Variant::INT || context.get_type() == Variant::FLOAT) && (output.get_type() == Variant::INT || output.get_type() == Variant::FLOAT) &&
					double(context) > 0 && double(output) > double(context)) {
				continue; // A reply longer than the window holds is not a real model.
			}
			kept[id] = model;
		}
		if (!kept.is_empty()) {
			Dictionary provider;
			provider["models"] = kept;
			cleaned[source] = provider;
		}
	}
	return cleaned;
}

static AIModelSpec _spec_of(const Dictionary &p_model) {
	AIModelSpec spec;
	spec.found = true;
	const Variant limit = p_model.get("limit", Variant());
	if (limit.get_type() == Variant::DICTIONARY) {
		spec.context = _tokens_of(limit, "context");
		spec.output = _tokens_of(limit, "output");
	}
	const Variant temperature = p_model.get("temperature", true);
	spec.temperature = temperature.get_type() != Variant::BOOL || bool(temperature);
	spec.deprecated = String(p_model.get("status", "")) == "deprecated";
	spec.released = p_model.get("release_date", "");
	spec.family = p_model.get("family", "");

	const Variant options = p_model.get("reasoning_options", Variant());
	if (options.get_type() != Variant::ARRAY) {
		return spec;
	}
	const Array listed = options;
	for (int i = 0; i < listed.size(); i++) {
		if (listed[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary option = listed[i];
		const String type = option.get("type", "");
		if (type == "toggle") {
			spec.can_turn_off_thinking = true;
		} else if (type == "budget_tokens") {
			spec.has_budget = true;
			spec.budget_min = _tokens_of(option, "min");
			spec.budget_max = _tokens_of(option, "max");
		} else if (type == "effort" && option.get("values", Variant()).get_type() == Variant::ARRAY) {
			const Array values = option["values"];
			for (int v = 0; v < values.size(); v++) {
				if (values[v].get_type() == Variant::STRING && _is_plain_name(values[v]) && !spec.efforts.has(values[v])) {
					spec.efforts.push_back(values[v]);
				}
			}
		}
	}
	return spec;
}

// "deepseek-ai/DeepSeek-V4-Pro" -> "deepseek-v4-pro".
static String _plain_name(const String &p_model) {
	const String lower = p_model.strip_edges().to_lower();
	const int slash = lower.rfind_char('/');
	return slash < 0 ? lower : lower.substr(slash + 1);
}

static Dictionary _models_of(const Dictionary &p_data, const String &p_source) {
	const Variant provider = p_data.get(p_source, Variant());
	if (provider.get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	const Variant models = Dictionary(provider).get("models", Variant());
	return models.get_type() == Variant::DICTIONARY ? Dictionary(models) : Dictionary();
}

void AIModelSpecs::_ensure_loaded() {
	if (!loaded) {
		reload();
	}
}

void AIModelSpecs::reload() {
	use_builtin();
	const String cache = get_cache_path();
	if (cache.is_empty() || !FileAccess::exists(cache)) {
		return;
	}
	Ref<FileAccess> file = FileAccess::open(cache, FileAccess::READ);
	const bool readable = file.is_valid() && file->get_length() <= MAX_CACHE_BYTES;
	if (!readable || !use(file->get_as_utf8_string())) {
		// Damaged: removed, so it counts as missing and is downloaded again.
		file.unref();
		DirAccess::remove_absolute(cache);
	}
}

void AIModelSpecs::use_builtin() {
	loaded = true;
	PackedByteArray text;
	text.resize(_ai_model_specs_uncompressed_size + 1);
	const int64_t size = Compression::decompress(text.ptrw(), _ai_model_specs_uncompressed_size, _ai_model_specs_compressed, _ai_model_specs_compressed_size, Compression::MODE_DEFLATE);
	data = Dictionary();
	if (size != _ai_model_specs_uncompressed_size || !use(String::utf8((const char *)text.ptr(), size))) {
		ERR_PRINT("The built-in model facts could not be read; model tables are used instead.");
	}
}

bool AIModelSpecs::use(const String &p_json) {
	const Variant parsed = _parse_json(p_json);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary candidate = _cleaned(parsed);
	for (const String &source : _all_sources()) {
		if (!_models_of(candidate, source).is_empty()) {
			data = candidate;
			loaded = true;
			return true;
		}
	}
	return false;
}

AIModelSpec AIModelSpecs::find(const String &p_provider_id, const String &p_model) {
	_ensure_loaded();
	const String name = _plain_name(p_model);
	if (name.is_empty()) {
		return AIModelSpec();
	}
	Vector<String> order = _sources_of(p_provider_id);
	for (const String &source : _all_sources()) {
		if (!order.has(source)) {
			order.push_back(source);
		}
	}
	for (const String &source : order) {
		const Dictionary models = _models_of(data, source);
		const Variant model = models.get(name, Variant());
		if (model.get_type() == Variant::DICTIONARY) {
			return _spec_of(model);
		}
	}
	return AIModelSpec();
}

PackedStringArray AIModelSpecs::get_models(const String &p_provider_id, int p_max) {
	_ensure_loaded();
	struct Entry {
		String id;
		String released;
		bool operator<(const Entry &p_other) const {
			return released != p_other.released ? released > p_other.released : id < p_other.id;
		}
	};
	Vector<Entry> entries;
	HashSet<String> seen; // A model both GLM endpoints list is offered once.
	for (const String &source : _sources_of(p_provider_id)) {
		const Dictionary models = _models_of(data, source);
		for (const Variant &key : models.keys()) {
			const String id = key;
			if (models[key].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary model = models[key];
			const AIModelSpec spec = _spec_of(model);
			const bool calls_tools = model.get("tool_call", false).booleanize();
			if (!calls_tools || spec.deprecated || spec.output <= 0 || AIModelNames::is_dated_snapshot(id) || seen.has(id)) {
				continue;
			}
			seen.insert(id);
			entries.push_back({ id, spec.released });
		}
	}
	entries.sort();
	PackedStringArray result;
	for (int i = 0; i < entries.size() && result.size() < p_max; i++) {
		result.push_back(entries[i].id);
	}
	return result;
}

String AIModelSpecs::get_newest(const String &p_provider_id, const Vector<String> &p_families) {
	_ensure_loaded();
	String newest;
	String newest_released;
	for (const String &source : _sources_of(p_provider_id)) {
		const Dictionary models = _models_of(data, source);
		for (const Variant &key : models.keys()) {
			const String id = key;
			if (models[key].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary model = models[key];
			const AIModelSpec spec = _spec_of(model);
			// An alias ("-latest") moves under the user; a dated name is a snapshot.
			if (!p_families.has(spec.family) || spec.deprecated || !model.get("tool_call", false).booleanize() ||
					AIModelNames::is_dated_snapshot(id) || id.contains("latest")) {
				continue;
			}
			// Same day: the plain name before its variants ("glm-5.3" before "glm-5.3-highspeed").
			const bool newer = newest.is_empty() || spec.released > newest_released ||
					(spec.released == newest_released && (id.length() < newest.length() || (id.length() == newest.length() && id < newest)));
			if (newer) {
				newest = id;
				newest_released = spec.released;
			}
		}
	}
	return newest;
}

String AIModelSpecs::get_newest_listed(const String &p_provider_id, const PackedStringArray &p_models, const Vector<String> &p_families) {
	String newest;
	AIModelSpec newest_spec;
	for (const String &id : p_models) {
		const AIModelSpec spec = find(p_provider_id, id);
		if (!spec.found || spec.deprecated || AIModelNames::is_dated_snapshot(id) || id.contains("latest")) {
			continue;
		}
		const bool in_family = p_families.has(spec.family);
		const bool newest_in_family = !newest.is_empty() && p_families.has(newest_spec.family);
		bool better = newest.is_empty() || (in_family && !newest_in_family);
		if (!better && in_family == newest_in_family) {
			better = spec.released > newest_spec.released ||
					(spec.released == newest_spec.released && (id.length() < newest.length() || (id.length() == newest.length() && id < newest)));
		}
		if (better) {
			newest = id;
			newest_spec = spec;
		}
	}
	return newest;
}

String AIModelSpecs::filter(const String &p_api_json) {
	const Variant parsed = _parse_json(p_api_json);
	if (parsed.get_type() != Variant::DICTIONARY) {
		return String();
	}
	const Dictionary all = parsed;
	Dictionary kept;
	for (const String &source : _all_sources()) {
		const Dictionary models = _models_of(all, source);
		if (models.is_empty()) {
			continue;
		}
		Dictionary kept_models;
		for (const Variant &id : models.keys()) {
			if (models[id].get_type() != Variant::DICTIONARY) {
				continue;
			}
			const Dictionary model = models[id];
			Dictionary entry;
			for (int k = 0; MODEL_KEYS[k]; k++) {
				if (model.has(MODEL_KEYS[k])) {
					entry[MODEL_KEYS[k]] = model[MODEL_KEYS[k]];
				}
			}
			if (entry.get("limit", Variant()).get_type() == Variant::DICTIONARY) {
				const Dictionary limit = entry["limit"];
				Dictionary kept_limit;
				for (const char *field : { "context", "output" }) {
					if (limit.has(field)) {
						kept_limit[field] = limit[field];
					}
				}
				entry["limit"] = kept_limit;
			}
			kept_models[id] = entry;
		}
		Dictionary provider;
		provider["models"] = kept_models;
		kept[source] = provider;
	}
	return kept.is_empty() ? String() : JSON::stringify(kept, "", true, false);
}

String AIModelSpecs::get_cache_path() {
	if (!cache_path_override.is_empty()) {
		return cache_path_override;
	}
#ifdef TOOLS_ENABLED
	if (EditorPaths::get_singleton()) {
		return EditorPaths::get_singleton()->get_config_dir().path_join("ai_assistant").path_join("model_specs.json");
	}
#endif
	return String();
}

bool AIModelSpecs::is_cache_stale() {
	const String cache = get_cache_path();
	if (cache.is_empty() || !FileAccess::exists(cache)) {
		return true;
	}
	const uint64_t modified = FileAccess::get_modified_time(cache);
	const uint64_t now = (uint64_t)Time::get_singleton()->get_unix_time_from_system();
	return modified == 0 || now < modified || now - modified > (uint64_t)MAX_AGE_DAYS * 24 * 3600;
}

bool AIModelSpecs::store_cache(const String &p_json) {
	if (!use(p_json)) {
		return false;
	}
	const String cache = get_cache_path();
	if (cache.is_empty()) {
		return true; // Used, just not kept.
	}
	// Written beside it, then moved over it: a cut-off write never replaces a good copy.
	DirAccess::make_dir_recursive_absolute(cache.get_base_dir());
	const String partial = cache + ".partial";
	if (p_json.length() > MAX_CACHE_BYTES) {
		return true; // Used, but too large to be read back; it is fetched again next time.
	}
	{
		Ref<FileAccess> file = FileAccess::open(partial, FileAccess::WRITE);
		if (file.is_null() || !file->store_string(p_json)) {
			file.unref();
			DirAccess::remove_absolute(partial);
			return false;
		}
	}
	if (DirAccess::rename_absolute(partial, cache) == OK) {
		return true;
	}
	// Some platforms do not move over an existing file.
	DirAccess::remove_absolute(cache);
	if (DirAccess::rename_absolute(partial, cache) == OK) {
		return true;
	}
	DirAccess::remove_absolute(partial);
	return false;
}
