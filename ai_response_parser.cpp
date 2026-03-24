#include "ai_response_parser.h"

void AIResponseParser::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "response"), &AIResponseParser::parse);
	ClassDB::bind_method(D_METHOD("has_code_blocks", "response"), &AIResponseParser::has_code_blocks);
	ClassDB::bind_method(D_METHOD("extract_first_code_block", "response"), &AIResponseParser::extract_first_code_block);
}

Dictionary AIResponseParser::parse(const String &p_response) const {
	Dictionary result;
	Array text_segments;
	Array code_blocks;

	// Look for ```gdscript ... ``` blocks.
	String response = p_response;
	int search_from = 0;

	while (search_from < response.length()) {
		// Find opening fence.
		int start = response.find("```gdscript", search_from);
		if (start == -1) {
			// Also try ```gd
			start = response.find("```gd\n", search_from);
		}
		if (start == -1) {
			// No more code blocks, rest is text.
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
	return p_response.find("```gdscript") != -1 || p_response.find("```gd\n") != -1;
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
