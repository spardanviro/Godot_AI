#pragma once

#include "core/string/char_utils.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include <initializer_list>

// Reading model names as providers write them, so that the rules built on
// them are about versions and kinds, not about the names current today:
// a "gpt-6" or a "glm-5.2" is handled without a change here.
namespace AIModelNames {

// The version that follows p_prefix, as major * 1000 + minor: 5004 for
// ("gpt-5.4-mini", "gpt-"), 5000 for ("glm-5", "glm-"). -1 when the name does
// not start with the prefix followed by a number. r_end, if given, receives
// the position after the version.
inline int version_after(const String &p_name, const String &p_prefix, int *r_end = nullptr) {
	static const int MAX_DIGITS = 3;
	if (!p_name.begins_with(p_prefix)) {
		return -1;
	}
	int pos = p_prefix.length();
	int major = 0;
	int digits = 0;
	while (pos < p_name.length() && is_digit(p_name[pos]) && digits < MAX_DIGITS) {
		major = major * 10 + (p_name[pos] - '0');
		pos++;
		digits++;
	}
	if (digits == 0 || (pos < p_name.length() && is_digit(p_name[pos]))) {
		return -1; // No number, or one too long to be a version (a date).
	}
	int minor = 0;
	if (pos + 1 < p_name.length() && p_name[pos] == '.' && is_digit(p_name[pos + 1])) {
		pos++;
		digits = 0;
		while (pos < p_name.length() && is_digit(p_name[pos]) && digits < MAX_DIGITS) {
			minor = minor * 10 + (p_name[pos] - '0');
			pos++;
			digits++;
		}
	}
	if (r_end) {
		*r_end = pos;
	}
	return major * 1000 + minor;
}

// A snapshot pinned to a date: "gpt-5-2025-08-07", "glm-5-20260101".
inline bool is_dated_snapshot(const String &p_name) {
	for (int i = 0; i + 4 < p_name.length(); i++) {
		if (p_name[i] == '-' && p_name[i + 1] == '2' && p_name[i + 2] == '0' && is_digit(p_name[i + 3]) && is_digit(p_name[i + 4])) {
			return true;
		}
	}
	return false;
}

inline bool contains_any(const String &p_name, std::initializer_list<const char *> p_words) {
	for (const char *word : p_words) {
		if (p_name.contains(word)) {
			return true;
		}
	}
	return false;
}

// The newest model named just prefix + version ("gpt-5.4", not
// "gpt-5.4-mini"): the family's main model. "" if there is none.
inline String newest_plain(const Vector<String> &p_models, const String &p_prefix) {
	String best;
	int best_version = -1;
	for (const String &name : p_models) {
		int end = 0;
		const int version = version_after(name, p_prefix, &end);
		if (version > best_version && end == name.length()) {
			best_version = version;
			best = name;
		}
	}
	return best;
}

} // namespace AIModelNames
