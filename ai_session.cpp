#include "ai_session.h"

#include "core/math/math_funcs.h"
#include "core/string/char_utils.h"

static const int SUMMARY_TEXT_LIMIT = 300;
static const int MIN_COMPRESSION_THRESHOLD = 4000;
static const int MAX_COMPRESSION_THRESHOLD = 500000;
static const int OVERFLOW_SAFETY_MARGIN = 500;

static Dictionary _make_message(const String &p_role, const String &p_content) {
	Dictionary msg;
	msg["role"] = p_role;
	msg["content"] = p_content;
	return msg;
}

static String _truncate(const String &p_text, int p_limit) {
	return p_text.length() > p_limit ? p_text.left(p_limit) + "..." : p_text;
}

void AISession::clear() {
	working.clear();
	full.clear();
	summary = String();
	summary_attachment = String();
	chat_id = String();
	attachment_ids.clear();
	attachment_contents.clear();
	next_attachment_id = 1;
}

void AISession::push_user(const String &p_content, const String &p_display, bool p_automatic) {
	Dictionary msg = _make_message("user", p_content);
	if (p_automatic) {
		msg["automatic"] = true;
	} else if (!p_display.is_empty() && p_display != p_content) {
		msg["display"] = p_display;
	}
	working.push_back(msg);
	full.push_back(msg);
}

void AISession::push_assistant(const String &p_content, const String &p_thinking) {
	Dictionary msg = _make_message("assistant", p_content);
	if (!p_thinking.is_empty()) {
		msg["thinking"] = p_thinking; // Local only; never sent back to the API.
	}
	working.push_back(msg);
	full.push_back(msg);
}

bool AISession::pop_last_user(String &r_content) {
	if (working.is_empty()) {
		return false;
	}
	const Dictionary last = working[working.size() - 1];
	if (String(last.get("role", "")) != "user") {
		return false;
	}
	r_content = last.get("content", "");
	working.resize(working.size() - 1);
	if (!full.is_empty() && String(Dictionary(full[full.size() - 1]).get("role", "")) == "user") {
		full.resize(full.size() - 1);
	}
	return true;
}

Array AISession::build_api_messages() const {
	Array messages;
	if (!summary.is_empty()) {
		messages.push_back(_make_message("user", "[CONTEXT SUMMARY - Previous conversation compressed]\n" + summary + summary_attachment));
		messages.push_back(_make_message("assistant", "Understood. I have the context from our previous conversation."));
	}
	for (int i = 0; i < working.size(); i++) {
		// Stored messages carry local-only keys; APIs reject unknown fields.
		const Dictionary stored = working[i];
		messages.push_back(_make_message(stored.get("role", "user"), stored.get("content", "")));
	}
	return messages;
}

int AISession::estimate_tokens(const String &p_text) {
	int cjk_chars = 0;
	int other_chars = 0;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		if ((c >= 0x4E00 && c <= 0x9FFF) || // CJK Unified Ideographs
				(c >= 0x3400 && c <= 0x4DBF) || // CJK Extension A
				(c >= 0x20000 && c <= 0x2A6DF) || // CJK Extension B
				(c >= 0x3040 && c <= 0x309F) || // Hiragana
				(c >= 0x30A0 && c <= 0x30FF) || // Katakana
				(c >= 0xAC00 && c <= 0xD7AF)) { // Hangul syllables
			cjk_chars++;
		} else {
			other_chars++;
		}
	}
	return MAX(1, cjk_chars + other_chars / 4);
}

int AISession::estimate_history_tokens() const {
	int total = estimate_tokens(summary) + (summary_attachment.is_empty() ? 0 : estimate_tokens(summary_attachment));
	for (int i = 0; i < working.size(); i++) {
		total += estimate_tokens(String(Dictionary(working[i]).get("content", "")));
	}
	return total;
}

// First few declarations in a code-carrying reply, as a hint of what it defined.
static Vector<String> _collect_symbols(const String &p_content) {
	const int MAX_SYMBOLS = 5;
	const int MAX_SYMBOL_LENGTH = 60;
	static const char *keywords[] = { "func ", "class ", "var ", nullptr };

	Vector<String> symbols;
	for (int k = 0; keywords[k] && symbols.size() < MAX_SYMBOLS; k++) {
		int pos = 0;
		while (symbols.size() < MAX_SYMBOLS) {
			pos = p_content.find(keywords[k], pos);
			if (pos < 0) {
				break;
			}
			int end = p_content.find("\n", pos);
			if (end < 0) {
				end = MIN(pos + MAX_SYMBOL_LENGTH, p_content.length());
			}
			symbols.push_back(p_content.substr(pos, end - pos).strip_edges().left(MAX_SYMBOL_LENGTH));
			pos = end + 1;
		}
	}
	return symbols;
}

static Vector<String> _collect_res_paths(const String &p_content) {
	const int MAX_PATHS = 3;
	Vector<String> paths;
	int pos = 0;
	while (paths.size() < MAX_PATHS) {
		pos = p_content.find("res://", pos);
		if (pos < 0) {
			break;
		}
		int end = pos + 6;
		while (end < p_content.length() && p_content[end] != '"' && p_content[end] != '\'' &&
				p_content[end] != '\n' && p_content[end] != ' ') {
			end++;
		}
		const String path = p_content.substr(pos, end - pos);
		if (!paths.has(path)) {
			paths.push_back(path);
		}
		pos = end;
	}
	return paths;
}

String AISession::summarize_message(const Dictionary &p_message) {
	const String role = p_message.get("role", "");
	const String content = p_message.get("content", "");

	if (role == "user") {
		// Attachments and injected context dwarf the actual request; cut at the first.
		static const char *noise_markers[] = {
			"--- ATTACHED CONTEXT ---", "--- FILE:", "--- END FILE",
			"[CONTEXT SUMMARY", "[Node:", nullptr
		};
		String request = content;
		for (int m = 0; noise_markers[m]; m++) {
			const int pos = request.find(noise_markers[m]);
			if (pos >= 0) {
				request = request.left(pos);
				break;
			}
		}
		return "User: " + _truncate(request.strip_edges(), SUMMARY_TEXT_LIMIT);
	}

	if (role == "assistant") {
		if (content.find("```") < 0) {
			return "AI: " + _truncate(content.strip_edges(), SUMMARY_TEXT_LIMIT);
		}

		String line = "AI: [Code";
		const int fence = content.find("```");
		const int fence_line_end = content.find("\n", fence);
		if (fence_line_end > fence + 3) {
			const String language = content.substr(fence + 3, fence_line_end - fence - 3).strip_edges();
			if (!language.is_empty()) {
				line += "/" + language;
			}
		}
		line += "]";

		const Vector<String> paths = _collect_res_paths(content);
		if (!paths.is_empty()) {
			line += " files=" + String(",").join(paths);
		}
		const Vector<String> symbols = _collect_symbols(content);
		if (!symbols.is_empty()) {
			line += " defines=[" + String(", ").join(symbols) + "]";
		}
		return line;
	}

	return String();
}

AISession::CompressionResult AISession::compress_if_needed(int p_context_length, int p_max_output_tokens, int p_system_prompt_tokens, const CompressionConfig &p_config) {
	CompressionResult result;
	result.tokens_before = estimate_history_tokens();
	// The window left for history is what remains after the reply and the system prompt.
	result.threshold = CLAMP(
			(int)((p_context_length - p_max_output_tokens - p_system_prompt_tokens) * p_config.threshold_pct),
			MIN_COMPRESSION_THRESHOLD, MAX_COMPRESSION_THRESHOLD);

	// Rounded up to an even count so the kept history starts on a user turn.
	const int keep_recent = MAX(2, p_config.keep_recent + (p_config.keep_recent & 1));
	if (result.tokens_before <= result.threshold || working.size() <= keep_recent) {
		return result;
	}

	const int compress_count = working.size() - keep_recent;
	result.block = "--- Compressed messages ---\n";
	for (int i = 0; i < compress_count; i++) {
		result.removed.push_back(working[i]);
		const String line = summarize_message(working[i]);
		if (!line.is_empty()) {
			result.block += line + "\n";
		}
	}
	working = working.slice(compress_count);

	if (!summary.is_empty()) {
		summary += "\n";
	}
	summary += result.block;
	result.compressed = true;
	forget_attachments(); // Their full text may be among what was just folded away.
	return result;
}

bool AISession::replace_summary_block(const String &p_block, const String &p_replacement) {
	const int pos = p_block.is_empty() ? -1 : summary.find(p_block);
	if (pos < 0) {
		return false;
	}
	summary = summary.left(pos) + p_replacement + summary.substr(pos + p_block.length());
	return true;
}

// Reads the run of digits that starts at (p_forward) or ends just before p_pos.
static int _read_number(const String &p_text, int p_pos, bool p_forward) {
	int start = p_pos;
	int end = p_pos;
	if (p_forward) {
		while (end < p_text.length() && is_digit(p_text[end])) {
			end++;
		}
	} else {
		while (start > 0 && is_digit(p_text[start - 1])) {
			start--;
		}
	}
	const String digits = p_text.substr(start, end - start);
	return digits.is_valid_int() ? digits.to_int() : -1;
}

AISession::OverflowInfo AISession::parse_overflow_error(const String &p_error) {
	OverflowInfo info;

	// Anthropic: "prompt is too long: 195432 tokens > 180000 maximum"
	static const String ANTHROPIC_MARK = " tokens > ";
	const int gt_pos = p_error.find(ANTHROPIC_MARK);
	if (gt_pos >= 0) {
		const int actual = _read_number(p_error, gt_pos, false);
		const int limit = _read_number(p_error, gt_pos + ANTHROPIC_MARK.length(), true);
		if (actual >= 0 && limit >= 0) {
			info.actual_tokens = actual;
			info.limit_tokens = limit;
		}
	}

	// OpenAI: "maximum context length is 128000 tokens. However, your messages
	// resulted in 130500 tokens."
	if (info.actual_tokens < 0) {
		static const String OPENAI_LIMIT_MARK = "maximum context length is ";
		static const String OPENAI_ACTUAL_MARK = "resulted in ";
		const int limit_pos = p_error.find(OPENAI_LIMIT_MARK);
		const int actual_pos = p_error.find(OPENAI_ACTUAL_MARK);
		if (limit_pos >= 0 && actual_pos >= 0) {
			const int limit = _read_number(p_error, limit_pos + OPENAI_LIMIT_MARK.length(), true);
			const int actual = _read_number(p_error, actual_pos + OPENAI_ACTUAL_MARK.length(), true);
			if (actual >= 0 && limit >= 0) {
				info.actual_tokens = actual;
				info.limit_tokens = limit;
			}
		}
	}

	info.is_overflow = info.actual_tokens >= 0 ||
			p_error.find("context_length_exceeded") >= 0 ||
			p_error.find("too long") >= 0 ||
			p_error.find("maximum context") >= 0;
	return info;
}

AISession::DropResult AISession::drop_oldest_for_gap(int p_gap_tokens) {
	DropResult result;
	while (working.size() > 1 && result.freed_tokens < p_gap_tokens) {
		const Dictionary msg = working[0];
		result.freed_tokens += estimate_tokens(String(msg.get("content", "")));
		if (!summary.is_empty()) {
			summary += "\n";
		}
		summary += "[reactive-compacted] " + summarize_message(msg);
		working.remove_at(0);
		result.dropped_messages++;
	}
	if (result.dropped_messages > 0) {
		forget_attachments();
	}
	return result;
}

int AISession::get_overflow_gap(const OverflowInfo &p_info) const {
	if (p_info.actual_tokens > 0 && p_info.limit_tokens > 0) {
		return p_info.actual_tokens - p_info.limit_tokens + OVERFLOW_SAFETY_MARGIN;
	}
	// No numbers in the error: free roughly as much as the summary occupies.
	return estimate_tokens(summary);
}

void AISession::forget_attachments() {
	attachment_ids.clear();
	attachment_contents.clear();
}

String AISession::build_attachment_content(const String &p_path, const String &p_raw_content) {
	const String file_name = p_path.get_file();
	if (p_raw_content.length() <= ATTACHMENT_INLINE_LIMIT) {
		return "--- ATTACHED FILE: " + file_name + " ---\n" + p_raw_content + "\n--- END FILE ---";
	}

	if (attachment_ids.has(p_path)) {
		const int id = attachment_ids[p_path];
		if (attachment_contents[id] == p_raw_content) {
			// The model already has this exact content from an earlier turn.
			return "[Attached file #" + itos(id) + ": " + file_name + " (" + itos(p_raw_content.length()) +
					" chars) — already provided in this session]";
		}
		attachment_contents[id] = p_raw_content;
		return "--- ATTACHED FILE #" + itos(id) + " (updated): " + file_name + " ---\n" + p_raw_content + "\n--- END FILE ---";
	}

	const int id = next_attachment_id++;
	attachment_ids[p_path] = id;
	attachment_contents[id] = p_raw_content;
	return "--- ATTACHED FILE #" + itos(id) + ": " + file_name + " ---\n" + p_raw_content + "\n--- END FILE ---";
}

String AISession::make_chat_id(const Dictionary &p_datetime) {
	return vformat("chat_%04d%02d%02d_%02d%02d%02d",
			(int)p_datetime.get("year", 0), (int)p_datetime.get("month", 0), (int)p_datetime.get("day", 0),
			(int)p_datetime.get("hour", 0), (int)p_datetime.get("minute", 0), (int)p_datetime.get("second", 0));
}

String AISession::get_title() const {
	const int TITLE_LENGTH = 60;
	for (int i = 0; i < full.size(); i++) {
		const Dictionary msg = full[i];
		if (String(msg.get("role", "")) == "user") {
			return String(msg.get("content", "")).left(TITLE_LENGTH);
		}
	}
	return "Untitled";
}

Dictionary AISession::to_dict() const {
	Dictionary data;
	data["id"] = chat_id;
	data["title"] = get_title();
	data["message_count"] = full.size();
	data["messages"] = full;
	if (!summary.is_empty()) {
		data["context_summary"] = summary;
		// How many leading messages the summary stands in for, so a reload can
		// rebuild the same working copy instead of sending them twice.
		data["compressed_count"] = MAX(0, full.size() - working.size());
	}
	return data;
}

bool AISession::from_dict(const Dictionary &p_data) {
	if (p_data.get("messages", Variant()).get_type() != Variant::ARRAY) {
		return false;
	}
	Array messages;
	const Array stored = p_data["messages"];
	for (int i = 0; i < stored.size(); i++) {
		if (stored[i].get_type() == Variant::DICTIONARY) {
			messages.push_back(stored[i]);
		}
	}

	clear();
	chat_id = p_data.get("id", "");
	full = messages;
	working = full.duplicate();

	const String stored_summary = p_data.get("context_summary", "");
	const int compressed_count = p_data.get("compressed_count", -1);
	// A summary is only usable together with the count of messages it replaces;
	// without it (older saves) the full history is authoritative.
	if (!stored_summary.is_empty() && compressed_count > 0 && compressed_count < full.size()) {
		summary = stored_summary;
		working = full.slice(compressed_count);
	}
	return true;
}
