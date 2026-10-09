#include "ai_response_parser.h"
#include "core/object/class_db.h"

void AIResponseParser::_bind_methods() {
	ClassDB::bind_method(D_METHOD("parse", "response"), &AIResponseParser::parse);
	ClassDB::bind_method(D_METHOD("has_code_blocks", "response"), &AIResponseParser::has_code_blocks);
	ClassDB::bind_method(D_METHOD("extract_first_code_block", "response"), &AIResponseParser::extract_first_code_block);
}

// A fence counts as executable GDScript when it is untagged or tagged with one
// of these (models such as GLM/DeepSeek often label GDScript as python).
static bool _is_gdscript_fence_lang(const String &p_lang) {
	return p_lang.is_empty() || p_lang == "gdscript" || p_lang == "gd" || p_lang == "python" || p_lang == "py";
}

Dictionary AIResponseParser::parse(const String &p_response) const {
	Dictionary result;
	Array text_segments;
	Array code_blocks;

	const String &response = p_response;
	int search_from = 0;

	// Every fence is paired with its own closing fence, whatever its language.
	// Pairing only the GDScript ones would let the closing fence of a ```json or
	// ```gdshader block be read as the opening of a new untagged code block.
	while (search_from < response.length()) {
		const int start = response.find("```", search_from);
		if (start == -1) {
			String remaining = response.substr(search_from).strip_edges();
			if (!remaining.is_empty()) {
				text_segments.push_back(remaining);
			}
			break;
		}

		String text_before = response.substr(search_from, start - search_from).strip_edges();
		if (!text_before.is_empty()) {
			text_segments.push_back(text_before);
		}

		const int line_end = response.find("\n", start);
		if (line_end == -1) {
			break; // Opening fence with nothing after it.
		}
		const String lang = response.substr(start + 3, line_end - (start + 3)).strip_edges().to_lower();
		if (lang.find("```") != -1) {
			// Inline ```snippet``` on a single line — prose, not a block.
			text_segments.push_back(response.substr(start, line_end - start).strip_edges());
			search_from = line_end + 1;
			continue;
		}

		const int code_start = line_end + 1;
		const int code_end = response.find("```", code_start);
		const String body = (code_end == -1)
				? response.substr(code_start)
				: response.substr(code_start, code_end - code_start);

		if (_is_gdscript_fence_lang(lang)) {
			const String code = body.strip_edges();
			if (!code.is_empty()) {
				code_blocks.push_back(code);
			}
		} else {
			// Other languages (json, gdshader, bash, ...) are shown, never executed.
			text_segments.push_back("```" + lang + "\n" + body.strip_edges() + "\n```");
		}

		if (code_end == -1) {
			break; // Unclosed block: it ran to the end of the response.
		}
		search_from = code_end + 3;
	}

	result["text_segments"] = text_segments;
	result["code_blocks"] = code_blocks;
	return result;
}

bool AIResponseParser::has_code_blocks(const String &p_response) const {
	return !Array(parse(p_response)["code_blocks"]).is_empty();
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
