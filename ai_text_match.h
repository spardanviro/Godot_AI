#pragma once

#include "core/string/char_utils.h"
#include "core/string/ustring.h"

// True if p_word occurs in p_haystack as a whole identifier-like token.
inline bool ai_contains_word(const String &p_haystack, const String &p_word) {
	if (p_word.is_empty()) {
		return false;
	}
	int pos = p_haystack.find(p_word);
	while (pos >= 0) {
		const int end = pos + p_word.length();
		const bool left_ok = pos == 0 || !is_ascii_identifier_char(p_haystack[pos - 1]);
		const bool right_ok = end >= p_haystack.length() || !is_ascii_identifier_char(p_haystack[end]);
		if (left_ok && right_ok) {
			return true;
		}
		pos = p_haystack.find(p_word, pos + 1);
	}
	return false;
}
