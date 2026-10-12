#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Pure text analysis of model replies and of the strings fed back to the
// model: side-effect markers, plan steps, safety filters on model-supplied
// URLs and paths. Nothing here touches the editor, so all of it is unit tested.
class AIResponseAnalysis {
public:
	struct AssetRequest {
		bool is_audio = false;
		String prompt;
		String save_path; // May be empty; always validated with is_safe_asset_path().
	};

	struct Markers {
		Vector<AssetRequest> assets; // [GENERATE_IMAGE: ...] / [GENERATE_AUDIO: ...]
		String web_search_query; // [WEB_SEARCH: query]
		String fetch_url; // [FETCH_URL: url]
		Vector<String> memory_notes; // [WRITE_MEMORY: note]

		bool has_web_request() const { return !web_search_query.is_empty() || !fetch_url.is_empty(); }
	};

	static Markers parse_markers(const String &p_response);

	// Model-supplied URLs may only point at plain public web addresses: no
	// loopback or private-network hosts (the editor would act as a proxy into
	// the local network) and no oversized URLs (the easy way to smuggle data out).
	static bool is_public_web_url(const String &p_url, String &r_reason);
	// Host part of an http(s) URL, lowercased, without user info, port or a
	// trailing dot. Empty for anything else.
	static String get_url_host(const String &p_url);
	// Loopback, private, link-local and other non-public addresses (IPv4 or
	// IPv6 literal). Used on what a host name resolves to.
	static bool is_private_address(const String &p_ip);
	// Model-supplied save paths must stay inside the project and name a new
	// image or audio file, so generation cannot overwrite project files.
	static bool is_safe_asset_path(const String &p_path);
	// Reduces a memory note to one short inert line; empty if nothing is left.
	static String sanitize_memory_note(const String &p_content);

	// Greetings and acknowledgements: a text-only reply to these is correct and
	// must not trigger the "you forgot the code block" retry.
	static bool is_conversational_input(const String &p_user_input);

	// Lines of a plan that start with "N." or "N)".
	static Vector<String> parse_plan_steps(const String &p_plan_text);
	static String build_plan_markdown(const Vector<String> &p_steps, const String &p_generated_at);
	// Ticks the first unchecked "- [ ] " box. Returns false if there is none.
	static bool mark_first_unchecked_step(String &r_markdown, String &r_step_label);

	// One-line description of what a code block does, for the chat summary.
	static String summarize_code(const String &p_code);
	// Unique res://*.gd paths mentioned in error output, in order of appearance.
	static Vector<String> extract_script_paths(const String &p_error_text);
	// Turns a code block into the contents of a .gd file. A block that is
	// already a script is kept as is; EditorScript body code is wrapped so the
	// file parses and can be run from the script editor. r_file_name receives a
	// suggested name (p_index keeps names from different blocks apart).
	static String build_saved_script(const String &p_code, int p_index, String &r_file_name);
	// Models sometimes repeat the same block twice in a row.
	static Vector<String> dedupe_consecutive(const Vector<String> &p_blocks);

	static const int MAX_URL_LENGTH = 500;
	static const int MAX_MEMORY_NOTE_LENGTH = 300;
};
