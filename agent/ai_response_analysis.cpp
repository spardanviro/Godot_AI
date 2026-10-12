#include "ai_response_analysis.h"
#include "../assets/ai_asset_request.h"

#include "core/io/ip_address.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"

// Value of key="..." inside a marker, or "" if absent.
static String _quoted_attribute(const String &p_marker, const String &p_key) {
	const String opening = p_key + "=\"";
	const int start = p_marker.find(opening);
	if (start < 0) {
		return String();
	}
	const int value_start = start + opening.length();
	const int value_end = p_marker.find("\"", value_start);
	return value_end > value_start ? p_marker.substr(value_start, value_end - value_start) : String();
}

// Bodies of every "[TAG ...]" occurrence, without the tag and the brackets.
static Vector<String> _marker_bodies(const String &p_response, const String &p_tag) {
	Vector<String> bodies;
	int pos = p_response.find(p_tag);
	while (pos >= 0) {
		const int end = p_response.find("]", pos);
		if (end < 0) {
			break;
		}
		bodies.push_back(p_response.substr(pos + p_tag.length(), end - pos - p_tag.length()));
		pos = p_response.find(p_tag, end + 1);
	}
	return bodies;
}

AIResponseAnalysis::Markers AIResponseAnalysis::parse_markers(const String &p_response) {
	Markers markers;

	const Vector<String> images = _marker_bodies(p_response, "[GENERATE_IMAGE:");
	for (int i = 0; i < images.size(); i++) {
		AIAssetRequest request;
		request.prompt = _quoted_attribute(images[i], "prompt");
		request.save_path = _quoted_attribute(images[i], "path");
		if (!request.prompt.is_empty()) {
			markers.assets.push_back(request);
		}
	}
	const Vector<String> audio = _marker_bodies(p_response, "[GENERATE_AUDIO:");
	for (int i = 0; i < audio.size(); i++) {
		AIAssetRequest request;
		request.is_audio = true;
		request.prompt = _quoted_attribute(audio[i], "text");
		request.save_path = _quoted_attribute(audio[i], "path");
		if (!request.prompt.is_empty()) {
			markers.assets.push_back(request);
		}
	}

	// One web request per reply: its result is fed back as a new turn.
	const Vector<String> searches = _marker_bodies(p_response, "[WEB_SEARCH:");
	if (!searches.is_empty()) {
		markers.web_search_query = searches[0].strip_edges();
	}
	const Vector<String> fetches = _marker_bodies(p_response, "[FETCH_URL:");
	if (!fetches.is_empty()) {
		markers.fetch_url = fetches[0].strip_edges();
	}

	const Vector<String> notes = _marker_bodies(p_response, "[WRITE_MEMORY:");
	for (int i = 0; i < notes.size(); i++) {
		const String note = notes[i].strip_edges();
		if (!note.is_empty()) {
			markers.memory_notes.push_back(note);
		}
	}
	return markers;
}

String AIResponseAnalysis::sanitize_memory_note(const String &p_content) {
	// Every kind of line break, including the Unicode ones a model could use to
	// smuggle a second line past the "one line" rule.
	String note = p_content.replace("\r", " ").replace("\n", " ").replace(String::chr(0x2028), " ").replace(String::chr(0x2029), " ").replace(String::chr(0x85), " ").strip_edges();
	// No headings: the note is loaded into every future system prompt.
	while (note.begins_with("#")) {
		note = note.substr(1).strip_edges();
	}
	if (note.length() > MAX_MEMORY_NOTE_LENGTH) {
		note = note.left(MAX_MEMORY_NOTE_LENGTH) + "...";
	}
	return note;
}

bool AIResponseAnalysis::is_conversational_input(const String &p_user_input) {
	const int MAX_TRIVIAL_LENGTH = 3;
	static const char *greetings[] = {
		"你好", "嗨", "hi", "hello", "hey", "yo",
		"thanks", "thank you", "谢谢", "感谢", "thx",
		"ok", "okay", "好的", "好", "是", "是的", "明白", "了解",
		"太好了", "很好", "nice", "great", "cool",
		nullptr
	};
	static const char *separators[] = { " ", "!", "！", "~", "，", ",", "。", nullptr };

	const String input = p_user_input.to_lower().strip_edges();
	if (input.length() <= MAX_TRIVIAL_LENGTH) {
		return true; // Too short to be a task.
	}
	for (int i = 0; greetings[i]; i++) {
		const String greeting = String::utf8(greetings[i]);
		if (input == greeting) {
			return true;
		}
		for (int s = 0; separators[s]; s++) {
			if (input.begins_with(greeting + String::utf8(separators[s]))) {
				return true;
			}
		}
	}
	return false;
}
