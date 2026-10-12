#include "ai_response_analysis.h"

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
		AssetRequest request;
		request.prompt = _quoted_attribute(images[i], "prompt");
		request.save_path = _quoted_attribute(images[i], "path");
		if (!request.prompt.is_empty()) {
			markers.assets.push_back(request);
		}
	}
	const Vector<String> audio = _marker_bodies(p_response, "[GENERATE_AUDIO:");
	for (int i = 0; i < audio.size(); i++) {
		AssetRequest request;
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

String AIResponseAnalysis::get_url_host(const String &p_url) {
	String rest;
	if (p_url.begins_with("https://")) {
		rest = p_url.substr(8);
	} else if (p_url.begins_with("http://")) {
		rest = p_url.substr(7);
	} else {
		return String();
	}
	String host = rest.get_slice("/", 0).get_slice("?", 0).get_slice("#", 0).to_lower();
	if (host.find("@") >= 0) {
		host = host.get_slice("@", host.get_slice_count("@") - 1);
	}
	if (host.begins_with("[")) {
		return host.get_slice("]", 0) + "]"; // IPv6 literal, kept bracketed.
	}
	host = host.get_slice(":", 0);
	while (host.ends_with(".")) {
		host = host.left(host.length() - 1); // "localhost." is localhost.
	}
	return host;
}

static bool _is_private_ipv4(int p_a, int p_b, int p_c) {
	return p_a == 0 || p_a == 10 || p_a == 127 || p_a >= 224 ||
			(p_a == 100 && p_b >= 64 && p_b <= 127) || // Carrier-grade NAT.
			(p_a == 169 && p_b == 254) || // Link-local, cloud metadata.
			(p_a == 172 && p_b >= 16 && p_b <= 31) ||
			(p_a == 192 && p_b == 168) ||
			(p_a == 192 && p_b == 0 && (p_c == 0 || p_c == 2)) || // Protocol assignments, documentation.
			(p_a == 192 && p_b == 88 && p_c == 99) || // 6to4 relay anycast.
			(p_a == 198 && (p_b == 18 || p_b == 19)) || // Benchmarking.
			(p_a == 198 && p_b == 51 && p_c == 100) || // Documentation.
			(p_a == 203 && p_b == 0 && p_c == 113); // Documentation.
}

bool AIResponseAnalysis::is_private_address(const String &p_ip) {
	if (p_ip.find_char(':') < 0) {
		const Vector<String> parts = p_ip.split(".");
		if (parts.size() != 4) {
			return true; // Not a plain dotted quad: do not guess.
		}
		int octets[4];
		for (int i = 0; i < 4; i++) {
			if (!parts[i].is_valid_int() || parts[i].length() > 3) {
				return true;
			}
			octets[i] = parts[i].to_int();
			if (octets[i] < 0 || octets[i] > 255) {
				return true;
			}
		}
		return _is_private_ipv4(octets[0], octets[1], octets[2]);
	}

	// IPv6 is judged by its bytes: the same address has many spellings
	// ("::1", "0:0:0:0:0:0:0:1"), and the engine's resolver uses the long one.
	if (!p_ip.is_valid_ip_address()) {
		return true;
	}
	const IPAddress address(p_ip);
	if (!address.is_valid() || address.is_wildcard()) {
		return true;
	}
	const uint8_t *bytes = address.get_ipv6();
	if (address.is_ipv4()) { // ::ffff:a.b.c.d
		return _is_private_ipv4(bytes[12], bytes[13], bytes[14]);
	}
	// NAT64 (64:ff9b::/96) leads to the IPv4 address in its last four bytes.
	static const uint8_t nat64_prefix[12] = { 0x00, 0x64, 0xff, 0x9b, 0, 0, 0, 0, 0, 0, 0, 0 };
	if (memcmp(bytes, nat64_prefix, sizeof(nat64_prefix)) == 0) {
		return _is_private_ipv4(bytes[12], bytes[13], bytes[14]);
	}
	// Everything public is global unicast, 2000::/3. That leaves out loopback,
	// unspecified, IPv4-compatible, unique-local, link-local, site-local and
	// multicast addresses in one test.
	if ((bytes[0] & 0xe0) != 0x20) {
		return true;
	}
	// Inside it: Teredo (2001::/32) and 6to4 (2002::/16) tunnel to an embedded
	// IPv4 address, and 2001:db8::/32 is documentation space.
	const bool is_teredo = bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00;
	const bool is_documentation = bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0d && bytes[3] == 0xb8;
	const bool is_6to4 = bytes[0] == 0x20 && bytes[1] == 0x02;
	return is_teredo || is_documentation || is_6to4;
}

bool AIResponseAnalysis::is_public_web_url(const String &p_url, String &r_reason) {
	if (p_url.length() > MAX_URL_LENGTH) {
		r_reason = "URL is too long";
		return false;
	}
	if (!p_url.begins_with("https://") && !p_url.begins_with("http://")) {
		r_reason = "only http(s) URLs are allowed";
		return false;
	}
	const String host = get_url_host(p_url);
	if (host.begins_with("[")) {
		r_reason = "IPv6 literal hosts are not allowed";
		return false;
	}

	// A host made only of digits, dots and hex letters is an address in some
	// spelling (127.1, 0x7f.0.0.1, 2130706433): accept only a public dotted quad.
	bool numeric = !host.is_empty() && is_digit(host[0]);
	for (int i = 0; i < host.length() && numeric; i++) {
		numeric = is_hex_digit(host[i]) || host[i] == '.' || host[i] == 'x';
	}
	const bool is_private = host.is_empty() || host.find(".") < 0 || host == "localhost" ||
			host.ends_with(".localhost") || host.ends_with(".local") || host.ends_with(".internal") ||
			(numeric && is_private_address(host));
	if (is_private) {
		r_reason = "local and private-network addresses are not allowed";
		return false;
	}
	return true;
}

bool AIResponseAnalysis::is_safe_asset_path(const String &p_path) {
	static const char *asset_extensions[] = { "png", "jpg", "jpeg", "webp", "mp3", "wav", "ogg", nullptr };
	if (p_path.is_empty()) {
		return true;
	}
	if (!p_path.begins_with("res://") || p_path.find("..") >= 0) {
		return false;
	}
	const String extension = p_path.get_extension().to_lower();
	for (int i = 0; asset_extensions[i]; i++) {
		if (extension == asset_extensions[i]) {
			return true;
		}
	}
	return false;
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

Vector<String> AIResponseAnalysis::parse_plan_steps(const String &p_plan_text) {
	Vector<String> steps;
	const Vector<String> lines = p_plan_text.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		int digits = 0;
		while (digits < line.length() && is_digit(line[digits])) {
			digits++;
		}
		if (digits > 0 && digits < line.length() && (line[digits] == '.' || line[digits] == ')')) {
			steps.push_back(line);
		}
	}
	return steps;
}

String AIResponseAnalysis::build_plan_markdown(const Vector<String> &p_steps, const String &p_generated_at) {
	String md = "# AI Plan\n\nGenerated: " + p_generated_at + "\n\n## Steps\n\n";
	for (int i = 0; i < p_steps.size(); i++) {
		md += "- [ ] " + p_steps[i] + "\n";
	}
	return md;
}

bool AIResponseAnalysis::mark_first_unchecked_step(String &r_markdown, String &r_step_label) {
	static const String UNCHECKED = "- [ ] ";
	// Steps run in order, so the first unchecked box is always the current one.
	Vector<String> lines = r_markdown.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].begins_with(UNCHECKED)) {
			const String label = lines[i].substr(UNCHECKED.length());
			r_step_label = label.strip_edges();
			lines.write[i] = "- [x] " + label;
			r_markdown = String("\n").join(lines);
			return true;
		}
	}
	return false;
}

String AIResponseAnalysis::summarize_code(const String &p_code) {
	// The last print("...") literal is usually the script's own summary.
	static const String PRINT_OPEN = "print(\"";
	String last_print;
	int search_from = 0;
	while (true) {
		const int print_pos = p_code.find(PRINT_OPEN, search_from);
		if (print_pos < 0) {
			break;
		}
		const int text_start = print_pos + PRINT_OPEN.length();
		const int text_end = p_code.find("\"", text_start);
		if (text_end > text_start) {
			last_print = p_code.substr(text_start, text_end - text_start);
			search_from = text_end + 1;
		} else {
			search_from = text_start + 1;
		}
	}
	if (!last_print.is_empty()) {
		return last_print;
	}

	if (p_code.find("add_child") >= 0) {
		static const String NAME_ASSIGN = ".name = \"";
		const int name_pos = p_code.find(NAME_ASSIGN);
		if (name_pos >= 0) {
			const int name_start = name_pos + NAME_ASSIGN.length();
			const int name_end = p_code.find("\"", name_start);
			if (name_end > name_start) {
				return "Added node '" + p_code.substr(name_start, name_end - name_start) + "' to the scene.";
			}
		}
		return "Node added to the scene.";
	}
	if (p_code.find("remove_child") >= 0) {
		return "Node removed from the scene.";
	}
	if (p_code.find("ResourceSaver.save") >= 0) {
		return "Resource saved to disk.";
	}
	if (p_code.find(".source_code") >= 0) {
		return "Script created or modified.";
	}
	if (p_code.find(".position") >= 0 || p_code.find(".rotation") >= 0) {
		return "Node transform modified.";
	}
	return "Code executed successfully.";
}

Vector<String> AIResponseAnalysis::extract_script_paths(const String &p_error_text) {
	Vector<String> paths;
	HashSet<String> seen;
	int pos = 0;
	while (true) {
		pos = p_error_text.find("res://", pos);
		if (pos < 0) {
			break;
		}
		int end = pos + 6;
		while (end < p_error_text.length()) {
			const char32_t c = p_error_text[end];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'' || c == ')' || c == ']') {
				break;
			}
			// "path.gd:42" — stop before the line number.
			if (c == ':' && end + 1 < p_error_text.length() && is_digit(p_error_text[end + 1])) {
				break;
			}
			end++;
		}
		const String path = p_error_text.substr(pos, end - pos);
		if (path.ends_with(".gd") && !seen.has(path)) {
			seen.insert(path);
			paths.push_back(path);
		}
		pos = end;
	}
	return paths;
}

String AIResponseAnalysis::build_saved_script(const String &p_code, int p_index, String &r_file_name) {
	static const char *declaration_starts[] = {
		"@tool", "extends ", "class_name ", "func ", "var ", "const ", "signal ",
		"enum ", "class ", "static ", "@export", "@onready", nullptr
	};
	static const String EXTENDS = "extends ";

	const Vector<String> lines = p_code.split("\n");
	r_file_name = "ai_generated_" + itos(p_index) + ".gd";

	// The first line that is not blank or a comment decides.
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}
		for (int d = 0; declaration_starts[d]; d++) {
			if (!line.begins_with(declaration_starts[d])) {
				continue;
			}
			if (line.begins_with(EXTENDS)) {
				r_file_name = line.substr(EXTENDS.length()).strip_edges().to_lower() + "_script_" + itos(p_index) + ".gd";
			}
			return p_code;
		}
		break;
	}

	r_file_name = "ai_editor_script_" + itos(p_index) + ".gd";
	String script = "@tool\nextends EditorScript\n\nfunc _run():\n";
	for (int i = 0; i < lines.size(); i++) {
		script += "\t" + lines[i] + "\n";
	}
	return script;
}

Vector<String> AIResponseAnalysis::dedupe_consecutive(const Vector<String> &p_blocks) {
	Vector<String> unique;
	for (int i = 0; i < p_blocks.size(); i++) {
		if (i == 0 || p_blocks[i] != p_blocks[i - 1]) {
			unique.push_back(p_blocks[i]);
		}
	}
	return unique;
}
