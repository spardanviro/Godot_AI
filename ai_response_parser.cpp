#include "ai_response_parser.h"
#include "core/object/class_db.h"

void AIResponseParser::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "response"), &AIResponseParser::parse);
	ClassDB::bind_method(D_METHOD("has_code_blocks", "response"), &AIResponseParser::has_code_blocks);
	ClassDB::bind_method(D_METHOD("extract_first_code_block", "response"), &AIResponseParser::extract_first_code_block);
}

// Returns the position of the next GDScript-like code fence opening, and sets
// r_fence_len to the length of the opening tag (so caller can skip past it).
// Accepted fences (models like GLM/DeepSeek often use python or generic fences):
//   ```gdscript   ```gd   ```python   ``` (generic — no language tag)
static int _find_code_fence(const String &p_response, int p_from, int &r_fence_len) {
	struct FenceTag {
		const char *tag;
		int len;
	};
	static const FenceTag tags[] = {
		{ "```gdscript", 11 },
		{ "```python",   9  },
		{ "```gd",       5  },
	};

	int best = -1;
	int best_len = 0;

	// Check all named fences.
	for (int i = 0; i < 3; i++) {
		int pos = p_response.find(tags[i].tag, p_from);
		if (pos != -1 && (best == -1 || pos < best)) {
			// Make sure it is followed by whitespace/newline (not e.g. ```gdscript_ext).
			int after = pos + tags[i].len;
			if (after >= p_response.length() || p_response[after] == '\n' || p_response[after] == '\r' || p_response[after] == ' ') {
				best = pos;
				best_len = tags[i].len;
			}
		}
	}

	// Also check for generic ``` (no language tag) — must be ``` followed immediately by newline.
	int gen_pos = p_from;
	while (true) {
		gen_pos = p_response.find("```", gen_pos);
		if (gen_pos == -1) break;
		int after = gen_pos + 3;
		if (after < p_response.length() && (p_response[after] == '\n' || p_response[after] == '\r')) {
			// It's a generic fence with no language.
			if (best == -1 || gen_pos < best) {
				best = gen_pos;
				best_len = 3;
			}
			break;
		}
		gen_pos += 3;
	}

	r_fence_len = best_len;
	return best;
}

Dictionary AIResponseParser::parse(const String &p_response) const {
	Dictionary result;
	Array text_segments;
	Array code_blocks;

	String response = p_response;
	int search_from = 0;

	while (search_from < response.length()) {
		int fence_len = 0;
		int start = _find_code_fence(response, search_from, fence_len);

		if (start == -1) {
			// No more code blocks — rest is text.
			String remaining = response.substr(search_from).strip_edges();
			if (!remaining.is_empty()) {
				text_segments.push_back(remaining);
			}
			break;
		}

		// Text before this code block.
		String text_before = response.substr(search_from, start - search_from).strip_edges();
		if (!text_before.is_empty()) {
			text_segments.push_back(text_before);
		}

		// Find end of opening line.
		int code_start = response.find("\n", start);
		if (code_start == -1) {
			break;
		}
		code_start += 1; // Skip the newline.

		// Find closing fence.
		int code_end = response.find("```", code_start);
		if (code_end == -1) {
			// Unclosed code block, take the rest.
			String code = response.substr(code_start).strip_edges();
			if (!code.is_empty()) {
				code_blocks.push_back(code);
			}
			break;
		}

		String code = response.substr(code_start, code_end - code_start).strip_edges();
		if (!code.is_empty()) {
			code_blocks.push_back(code);
		}

		// Move past the closing fence.
		search_from = code_end + 3;
	}

	result["text_segments"] = text_segments;
	result["code_blocks"] = code_blocks;
	return result;
}

bool AIResponseParser::has_code_blocks(const String &p_response) const {
	int dummy = 0;
	return _find_code_fence(p_response, 0, dummy) != -1;
}

String AIResponseParser::extract_first_code_block(const String &p_response) const {
	Dictionary parsed = parse(p_response);
	Array blocks = parsed["code_blocks"];
	if (blocks.is_empty()) {
		return "";
	}
	return blocks[0];
}

AIResponseParser::AIResponseParser() {
}
