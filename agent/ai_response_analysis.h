#pragma once

#include "core/string/ustring.h"
#include "../assets/ai_asset_request.h"
#include "core/templates/vector.h"

// Pure text analysis of what a reply asks for besides its code: the
// side-effect markers, and the checks on the text they carry. Nothing here
// touches the editor, so all of it is unit tested.
class AIResponseAnalysis {
public:
	struct Markers {
		Vector<AIAssetRequest> assets; // [GENERATE_IMAGE: ...] / [GENERATE_AUDIO: ...]
		String web_search_query; // [WEB_SEARCH: query]
		String fetch_url; // [FETCH_URL: url]
		Vector<String> memory_notes; // [WRITE_MEMORY: note]

		bool has_web_request() const { return !web_search_query.is_empty() || !fetch_url.is_empty(); }
	};

	static Markers parse_markers(const String &p_response);

	// Reduces a memory note to one short inert line; empty if nothing is left.
	static String sanitize_memory_note(const String &p_content);

	// Greetings and acknowledgements: a text-only reply to these is correct and
	// must not trigger the "you forgot the code block" retry.
	static bool is_conversational_input(const String &p_user_input);

	static const int MAX_MEMORY_NOTE_LENGTH = 300;
};
