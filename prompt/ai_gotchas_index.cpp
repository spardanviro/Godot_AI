#include "ai_gotchas_index.h"

#include "../common/ai_text_match.h"

#include "core/math/math_funcs.h"
#include "core/templates/hash_map.h"

// An entry needs at least one reasonably specific hit: a keyword shared by
// more than ~1 in 16 entries does not qualify on its own.
static const float MIN_SCORE = 2.8f;

// Keyword lists are derived from entry headings, so they mix real API names
// with ordinary heading words ("In", "Never", "SILENTLY"). Matching on those
// floods the budget with unrelated entries, so only meaningful ones count:
//   - identifier-like names (CamelCase, snake_case, digits): always kept
//   - lowercase topic words: kept unless too generic to discriminate
//   - ALL-CAPS words: kept only when they are a known acronym
//   - plain Capitalized words: kept unless they are common English
bool AIGotchasIndex::is_useful_keyword(const String &p_keyword) {
	static const char *generic_lower[] = {
		"gdscript", "int", "float", "scene", "property", "resource", "print", "error",
		"file", "warning", "load", "static", "global", "editor", "debug", "settings",
		"typed", "cached", "3d", "2d", nullptr
	};
	static const char *acronyms[] = {
		"AABB", "ACES", "AR", "ASTC", "BPTC", "CSG", "FABRIK", "FPS", "GLSL", "GLTF",
		"JSON", "LINQ", "MSAA", "PSSM", "RPC", "SDFGI", "SSAO", "SSIL", "UI", "VR",
		"VRAM", "XR", nullptr
	};
	static const char *common_capitalized[] = {
		"Advance", "Align", "All", "Always", "Apply", "Avoid", "Begin", "Bind", "Bool",
		"Call", "Cannot", "Cast", "Code", "Color", "Condition", "Create", "Cross", "Custom",
		"Cut", "Default", "Deleting", "Direct", "Disabled", "Don", "Enable", "End",
		"English", "Ensure", "Exclusive", "Far", "Feature", "File", "Float", "For",
		"Forward", "Freeing", "From", "Get", "Global", "Godot", "Height", "High", "Hor",
		"Identity", "In", "Invert", "Invoke", "Keep", "Large", "Limit", "Map", "Method",
		"Mix", "Mode", "Moving", "Name", "Near", "Never", "Node", "Nodes", "Normal", "Not",
		"Object", "Only", "Play", "Position", "Precision", "Project", "Property", "Python",
		"Reading", "Record", "Resource", "Resources", "Rest", "Root", "Rotate", "Runtime",
		"Sample", "Scale", "Scaling", "Scene", "Scripts", "Self", "Set", "Settings",
		"Static", "String", "Text", "Time", "Tool", "Track", "Tree", "Typed", "Unique",
		"Upgrade", "Use", "Vert", "Width", nullptr
	};

	bool has_lower = false, has_upper = false, inner_upper = false, has_digit_or_underscore = false;
	for (int i = 0; i < p_keyword.length(); i++) {
		const char32_t c = p_keyword[i];
		if (c >= 'a' && c <= 'z') {
			has_lower = true;
		} else if (c >= 'A' && c <= 'Z') {
			has_upper = true;
			inner_upper = inner_upper || i > 0;
		} else if ((c >= '0' && c <= '9') || c == '_') {
			has_digit_or_underscore = true;
		}
	}

	if (has_lower && !has_upper) {
		for (int i = 0; generic_lower[i]; i++) {
			if (p_keyword == generic_lower[i]) {
				return false;
			}
		}
		return true;
	}
	if (has_upper && !has_lower) {
		for (int i = 0; acronyms[i]; i++) {
			if (p_keyword == acronyms[i]) {
				return true;
			}
		}
		return has_digit_or_underscore && p_keyword.length() > 3; // e.g. NOTIFICATION_READY
	}
	if (has_digit_or_underscore || inner_upper) {
		return true; // CharacterBody3D, move_and_slide, AnimationPlayer
	}
	for (int i = 0; common_capitalized[i]; i++) {
		if (p_keyword == common_capitalized[i]) {
			return false;
		}
	}
	return true; // Tween, Shader, Camera, ...
}

AIGotchasIndex::Index AIGotchasIndex::_build_index(const Vector<AIKnowledge::Gotcha> &p_entries) {
	Index index;
	HashMap<String, int> entry_count;
	index.keywords.resize(p_entries.size());
	index.weights.resize(p_entries.size());

	for (int i = 0; i < p_entries.size(); i++) {
		Vector<String> kept;
		for (int j = 0; j < p_entries[i].keywords.size(); j++) {
			if (!is_useful_keyword(p_entries[i].keywords[j])) {
				continue;
			}
			const String lower = p_entries[i].keywords[j].to_lower();
			if (!kept.has(lower)) {
				kept.push_back(lower);
				entry_count[lower] = entry_count.has(lower) ? entry_count[lower] + 1 : 1;
			}
		}
		index.keywords.write[i] = kept;
	}
	for (int i = 0; i < p_entries.size(); i++) {
		Vector<float> weights;
		for (int j = 0; j < index.keywords[i].size(); j++) {
			weights.push_back(Math::log((float)p_entries.size() / (float)entry_count[index.keywords[i][j]]));
		}
		index.weights.write[i] = weights;
	}
	return index;
}

String AIGotchasIndex::_select(const Vector<AIKnowledge::Gotcha> &p_entries, const Index &p_index, const String &p_message) {
	if (p_message.is_empty()) {
		return String();
	}
	const String message = p_message.to_lower();

	struct Match {
		int entry = 0;
		float score = 0.0f;
		bool operator<(const Match &p_other) const {
			// Higher score first; file order breaks ties so output is stable.
			return score == p_other.score ? entry < p_other.entry : score > p_other.score;
		}
	};
	Vector<Match> matches;
	HashMap<String, bool> hit_cache;

	for (int i = 0; i < p_entries.size(); i++) {
		float score = 0.0f;
		for (int j = 0; j < p_index.keywords[i].size(); j++) {
			const String &keyword = p_index.keywords[i][j];
			if (!hit_cache.has(keyword)) {
				hit_cache[keyword] = ai_contains_word(message, keyword);
			}
			if (hit_cache[keyword]) {
				score += p_index.weights[i][j];
			}
		}
		if (score >= MIN_SCORE) {
			Match match;
			match.entry = i;
			match.score = score;
			matches.push_back(match);
		}
	}
	if (matches.is_empty()) {
		return String();
	}
	matches.sort();

	// Most relevant first, and a hard budget: an entry that does not fit is
	// skipped rather than allowed to overshoot.
	String result = "## Relevant Gotchas\n\n";
	int used = 0;
	for (int i = 0; i < matches.size(); i++) {
		const String &content = p_entries[matches[i].entry].content;
		if (used + content.length() > BUDGET_CHARS) {
			continue;
		}
		result += content;
		used += content.length();
	}
	return used > 0 ? result : String();
}

String AIGotchasIndex::select(const Vector<AIKnowledge::Gotcha> &p_entries, const String &p_message) {
	return _select(p_entries, _build_index(p_entries), p_message);
}

String AIGotchasIndex::get_for_message(const String &p_message) {
	// The index only depends on the entries, so it is rebuilt when they reload.
	static Index index;
	static uint64_t index_generation = 0;

	const AIKnowledge *knowledge = AIKnowledge::get_singleton();
	if (index_generation != knowledge->get_generation()) {
		index = _build_index(knowledge->get_gotchas());
		index_generation = knowledge->get_generation();
	}
	return _select(knowledge->get_gotchas(), index, p_message);
}
