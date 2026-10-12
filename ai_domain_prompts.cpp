#include "ai_domain_prompts.h"

#include "core/string/char_utils.h"

bool AIDomainPrompts::matches(const AIKnowledge::Domain &p_domain, const String &p_lower_message) {
	for (int i = 0; i < p_domain.keywords.size(); i++) {
		const String &keyword = p_domain.keywords[i];
		if (keyword.is_empty()) {
			continue;
		}
		// CJK text has no word boundaries: those keywords are plain substrings.
		const bool anchored = is_ascii_identifier_char(keyword[0]);

		int pos = p_lower_message.find(keyword);
		while (pos != -1) {
			if (!anchored) {
				return true;
			}
			// The left edge is always anchored: "tile" must not match inside
			// "versatile", nor "joint" inside "disjoint".
			const bool left_ok = pos == 0 || !is_ascii_identifier_char(p_lower_message[pos - 1]);
			bool right_ok = true;
			if (p_domain.whole_word) {
				// Whole words only, allowing a plural "s": a bare stem test would
				// fire "ui" on "build" and "tab" on "table".
				int end = pos + keyword.length();
				if (end < p_lower_message.length() && p_lower_message[end] == 's') {
					end++;
				}
				right_ok = end >= p_lower_message.length() || !is_ascii_identifier_char(p_lower_message[end]);
			}
			if (left_ok && right_ok) {
				return true;
			}
			pos = p_lower_message.find(keyword, pos + 1);
		}
	}
	return false;
}

Vector<String> AIDomainPrompts::detect_domains(const Vector<AIKnowledge::Domain> &p_domains, const String &p_message) {
	Vector<String> ids;
	const String lower = p_message.to_lower();
	for (int i = 0; i < p_domains.size(); i++) {
		if (matches(p_domains[i], lower)) {
			ids.push_back(p_domains[i].id);
		}
	}
	return ids;
}

String AIDomainPrompts::build_contextual_prompt(const Vector<AIKnowledge::Domain> &p_domains, const String &p_message) {
	String result;
	const String lower = p_message.to_lower();
	for (int i = 0; i < p_domains.size(); i++) {
		if (matches(p_domains[i], lower)) {
			result += p_domains[i].body;
		}
	}
	return result;
}

String AIDomainPrompts::build_contextual_prompt(const String &p_message) {
	return build_contextual_prompt(AIKnowledge::get_singleton()->get_domains(), p_message);
}
