#include "ai_markdown.h"

#include "core/string/char_utils.h"

#include <cstring>

namespace {

const int MAX_INLINE_DEPTH = 8;
const int MAX_HEADING_LEVEL = 6;
const float HEADING_SCALE[MAX_HEADING_LEVEL] = { 1.5f, 1.3f, 1.15f, 1.05f, 1.0f, 1.0f };
const int TAB_WIDTH = 4;
const int MAX_ORDERED_DIGITS = 9;

String color_tag(const Color &p_color) {
	return "[color=#" + p_color.to_html(true) + "]";
}

bool is_word_char(char32_t p_char) {
	return is_ascii_alphanumeric_char(p_char) || p_char > 127;
}

bool is_ascii_punctuation(char32_t p_char) {
	return p_char > 32 && p_char < 127 && !is_ascii_alphanumeric_char(p_char);
}

int run_length(const String &p_text, int p_pos, char32_t p_char) {
	int end = p_pos;
	while (end < p_text.length() && p_text[end] == p_char) {
		end++;
	}
	return end - p_pos;
}

// Start of the run of exactly p_count backticks that closes a code span.
int find_code_close(const String &p_text, int p_from, int p_count) {
	int i = p_from;
	while (i < p_text.length()) {
		if (p_text[i] != '`') {
			i++;
			continue;
		}
		const int run = run_length(p_text, i, '`');
		if (run == p_count) {
			return i;
		}
		i += run;
	}
	return -1;
}

// Start of the p_count delimiters that close emphasis whose content begins at
// p_from, or -1. Code spans and nested emphasis are stepped over.
int find_emphasis_close(const String &p_text, int p_from, char32_t p_delimiter, int p_count) {
	int i = p_from;
	while (i < p_text.length()) {
		const char32_t c = p_text[i];
		if (c == '\\') {
			i += 2;
			continue;
		}
		if (c == '`') {
			const int run = run_length(p_text, i, '`');
			const int close = find_code_close(p_text, i + run, run);
			i = close < 0 ? i + run : close + run;
			continue;
		}
		if (c != p_delimiter) {
			i++;
			continue;
		}
		const int run = run_length(p_text, i, p_delimiter);
		const bool can_close = i > p_from && !is_whitespace(p_text[i - 1]);
		if (can_close && run >= p_count) {
			return i + run - p_count;
		}
		if (can_close) {
			i += run;
			continue;
		}
		// A run after whitespace opens emphasis of its own.
		const int nested = find_emphasis_close(p_text, i + run, p_delimiter, run);
		i = nested < 0 ? i + run : nested + run;
	}
	return -1;
}

bool is_web_url(const String &p_url) {
	if (!p_url.begins_with("http://") && !p_url.begins_with("https://")) {
		return false;
	}
	for (int i = 0; i < p_url.length(); i++) {
		const char32_t c = p_url[i];
		if (c <= 32 || c == '[' || c == ']' || c == '"' || c == '\'' || c == '<' || c == '>' || c == '\\') {
			return false;
		}
	}
	return true;
}

String link(const String &p_label_bbcode, const String &p_url, const AIMarkdownStyle &p_style) {
	return color_tag(p_style.link) + "[url=" + p_url + "]" + p_label_bbcode + "[/url][/color]";
}

String render_inline(const String &p_text, const AIMarkdownStyle &p_style, int p_depth);

// "[label](target)" with the "[" at p_pos. Returns the position after it, or
// -1 if this is not a link.
int take_link(const String &p_text, int p_pos, const AIMarkdownStyle &p_style, int p_depth, String &r_bbcode) {
	int depth = 0;
	int label_end = -1;
	for (int i = p_pos; i < p_text.length(); i++) {
		const char32_t c = p_text[i];
		if (c == '\\') {
			i++;
		} else if (c == '[') {
			depth++;
		} else if (c == ']' && --depth == 0) {
			label_end = i;
			break;
		}
	}
	if (label_end < 0 || label_end + 1 >= p_text.length() || p_text[label_end + 1] != '(') {
		return -1;
	}
	const int target_end = p_text.find_char(')', label_end + 2);
	if (target_end < 0) {
		return -1;
	}
	// A title may follow the address: [label](address "title").
	const String target = p_text.substr(label_end + 2, target_end - label_end - 2).strip_edges().get_slicec(' ', 0);
	// Code such as "items[0](x)" has the same shape; a target looks like an address.
	if (!target.contains_char('/') && !target.contains_char(':') && !target.contains_char('.') && !target.begins_with("#")) {
		return -1;
	}
	const String label = render_inline(p_text.substr(p_pos + 1, label_end - p_pos - 1), p_style, p_depth + 1);
	if (is_web_url(target)) {
		r_bbcode = link(label, target, p_style);
	} else {
		// Only web addresses are made clickable.
		r_bbcode = label + " (" + AIMarkdown::escape(target) + ")";
	}
	return target_end + 1;
}

// A bare web address starting at p_pos. Returns its end, or -1.
int take_bare_url(const String &p_text, int p_pos) {
	const String rest = p_text.substr(p_pos, 8);
	if (!rest.begins_with("http://") && !rest.begins_with("https://")) {
		return -1;
	}
	if (p_pos > 0 && is_ascii_alphanumeric_char(p_text[p_pos - 1])) {
		return -1;
	}
	const int scheme_end = p_pos + (rest.begins_with("https://") ? 8 : 7);
	int end = scheme_end;
	while (end < p_text.length()) {
		const char32_t c = p_text[end];
		// Stops at non-ASCII text too: prose often follows an address directly.
		if (c <= 32 || c > 126 || c == '<' || c == '>' || c == '"' || c == '\'' || c == '[' || c == ']' || c == '`' || c == '\\' || c == '|') {
			break;
		}
		end++;
	}
	while (end > scheme_end && String(".,;:!?)*_").contains_char(p_text[end - 1])) {
		end--;
	}
	return end > scheme_end ? end : -1;
}

// "<br>", "<br/>" or "<br />" at p_pos. Returns its length, or 0.
int line_break_length(const String &p_text, int p_pos) {
	static const char *FORMS[] = { "<br>", "<br/>", "<br />" };
	for (const char *form : FORMS) {
		const int length = strlen(form);
		if (p_text.substr(p_pos, length).to_lower() == form) {
			return length;
		}
	}
	return 0;
}

String render_inline(const String &p_text, const AIMarkdownStyle &p_style, int p_depth) {
	if (p_depth > MAX_INLINE_DEPTH) {
		return AIMarkdown::escape(p_text);
	}
	String out;
	String plain;
	const auto flush_plain = [&]() {
		out += AIMarkdown::escape(plain);
		plain = String();
	};

	const int length = p_text.length();
	int i = 0;
	while (i < length) {
		const char32_t c = p_text[i];

		if (c == '\\' && i + 1 < length && is_ascii_punctuation(p_text[i + 1])) {
			plain += p_text[i + 1];
			i += 2;
			continue;
		}

		if (c == '`') {
			const int run = run_length(p_text, i, '`');
			const int close = find_code_close(p_text, i + run, run);
			if (close < 0) {
				plain += p_text.substr(i, run);
				i += run;
				continue;
			}
			String code = p_text.substr(i + run, close - i - run);
			if (code.length() > 2 && code.begins_with(" ") && code.ends_with(" ")) {
				code = code.substr(1, code.length() - 2);
			}
			flush_plain();
			out += "[bgcolor=#" + p_style.code_background.to_html(true) + "]" + color_tag(p_style.code) + "[code]" + AIMarkdown::escape(code) + "[/code][/color][/bgcolor]";
			i = close + run;
			continue;
		}

		if (c == '*' || c == '_') {
			const int run = run_length(p_text, i, c);
			const int count = MIN(run, 3);
			const int content = i + count;
			bool opens = run <= 3 && content < length && !is_whitespace(p_text[content]);
			// Underscores inside a word are part of it: move_and_slide.
			if (opens && c == '_' && i > 0 && is_word_char(p_text[i - 1])) {
				opens = false;
			}
			int close = opens ? find_emphasis_close(p_text, content, c, count) : -1;
			if (close >= 0 && c == '_' && close + count < length && is_word_char(p_text[close + count])) {
				close = -1;
			}
			if (close < 0) {
				plain += p_text.substr(i, run);
				i += run;
				continue;
			}
			const String inner = render_inline(p_text.substr(content, close - content), p_style, p_depth + 1);
			flush_plain();
			if (count == 1) {
				out += "[i]" + inner + "[/i]";
			} else if (count == 2) {
				out += "[b]" + inner + "[/b]";
			} else {
				out += "[b][i]" + inner + "[/i][/b]";
			}
			i = close + count;
			continue;
		}

		if (c == '~' && i + 1 < length && p_text[i + 1] == '~') {
			const int close = p_text.find("~~", i + 2);
			if (close > i + 2) {
				flush_plain();
				out += color_tag(p_style.quote) + "[s]" + render_inline(p_text.substr(i + 2, close - i - 2), p_style, p_depth + 1) + "[/s][/color]";
				i = close + 2;
				continue;
			}
		}

		if (c == '[' || (c == '!' && i + 1 < length && p_text[i + 1] == '[')) {
			// An image is shown as a link to it.
			const int bracket = c == '!' ? i + 1 : i;
			String bbcode;
			const int end = take_link(p_text, bracket, p_style, p_depth, bbcode);
			if (end >= 0) {
				flush_plain();
				out += bbcode;
				i = end;
				continue;
			}
		}

		if (c == 'h') {
			const int end = take_bare_url(p_text, i);
			if (end >= 0) {
				const String url = p_text.substr(i, end - i);
				flush_plain();
				out += link(AIMarkdown::escape(url), url, p_style);
				i = end;
				continue;
			}
		}

		if (c == '<') {
			const int break_length = line_break_length(p_text, i);
			if (break_length > 0) {
				plain += "\n";
				i += break_length;
				continue;
			}
		}

		plain += c;
		i++;
	}
	flush_plain();
	return out;
}

// Columns a line is indented by.
int indent_width(const String &p_line) {
	int width = 0;
	for (int i = 0; i < p_line.length(); i++) {
		if (p_line[i] == ' ') {
			width++;
		} else if (p_line[i] == '\t') {
			width += TAB_WIDTH;
		} else {
			break;
		}
	}
	return width;
}

// p_text without p_width columns of leading whitespace (fewer if it has fewer).
String remove_indent(const String &p_text, int p_width) {
	int width = 0;
	int i = 0;
	while (i < p_text.length() && width < p_width && (p_text[i] == ' ' || p_text[i] == '\t')) {
		width += p_text[i] == '\t' ? TAB_WIDTH : 1;
		i++;
	}
	return p_text.substr(i);
}

// "###" -> 3 when followed by a space or the end of the line; otherwise 0.
int heading_level(const String &p_stripped) {
	const int level = run_length(p_stripped, 0, '#');
	if (level < 1 || level > MAX_HEADING_LEVEL) {
		return 0;
	}
	return level == p_stripped.length() || p_stripped[level] == ' ' || p_stripped[level] == '\t' ? level : 0;
}

// "---", "***", "___", also with spaces between the characters.
bool is_rule(const String &p_stripped) {
	const String compact = p_stripped.replace(" ", "");
	if (compact.length() < 3) {
		return false;
	}
	const char32_t c = compact[0];
	return (c == '-' || c == '*' || c == '_') && run_length(compact, 0, c) == compact.length();
}

struct ListItem {
	bool found = false;
	String number; // Empty for a bullet.
	String content;
};

ListItem parse_list_item(const String &p_stripped) {
	ListItem item;
	int marker_end = 0;
	const char32_t first = p_stripped.is_empty() ? 0 : p_stripped[0];
	if (first == '-' || first == '*' || first == '+') {
		marker_end = 1;
	} else {
		int digits = 0;
		while (digits < p_stripped.length() && is_digit(p_stripped[digits])) {
			digits++;
		}
		if (digits == 0 || digits > MAX_ORDERED_DIGITS || digits >= p_stripped.length() || (p_stripped[digits] != '.' && p_stripped[digits] != ')')) {
			return item;
		}
		item.number = p_stripped.left(digits);
		marker_end = digits + 1;
	}
	if (marker_end >= p_stripped.length() || (p_stripped[marker_end] != ' ' && p_stripped[marker_end] != '\t')) {
		return item;
	}
	item.found = true;
	item.content = p_stripped.substr(marker_end + 1).strip_edges();
	return item;
}

String repeat_tag(const char *p_tag, int p_count) {
	String out;
	for (int i = 0; i < p_count; i++) {
		out += p_tag;
	}
	return out;
}

// "| a | b |" -> ["a", "b"]. A pipe inside a code span or after a backslash
// does not separate cells.
Vector<String> split_table_row(const String &p_row) {
	String row = p_row.strip_edges().trim_prefix("|");
	if (row.ends_with("|") && !row.ends_with("\\|")) {
		row = row.left(row.length() - 1);
	}
	Vector<String> cells;
	String cell;
	bool in_code = false;
	for (int i = 0; i < row.length(); i++) {
		const char32_t c = row[i];
		if (c == '\\' && i + 1 < row.length() && row[i + 1] == '|') {
			cell += "|";
			i++;
		} else if (c == '|' && !in_code) {
			cells.push_back(cell.strip_edges());
			cell = String();
		} else {
			in_code = c == '`' ? !in_code : in_code;
			cell += c;
		}
	}
	cells.push_back(cell.strip_edges());
	return cells;
}

// The "|---|:--:|" row under a table's header.
bool is_table_separator(const String &p_row) {
	const Vector<String> cells = split_table_row(p_row);
	for (const String &cell : cells) {
		const String dashes = cell.trim_prefix(":").trim_suffix(":");
		if (dashes.is_empty() || run_length(dashes, 0, '-') != dashes.length()) {
			return false;
		}
	}
	return !cells.is_empty();
}

String cell_padding(const AIMarkdownStyle &p_style, float p_horizontal, float p_vertical) {
	const String horizontal = itos((int)Math::round(p_style.font_size * p_horizontal));
	const String vertical = itos((int)Math::round(p_style.font_size * p_vertical));
	return " padding=" + horizontal + "," + vertical + "," + horizontal + "," + vertical;
}

String render_table(const Vector<String> &p_rows, const AIMarkdownStyle &p_style) {
	const Vector<String> header = split_table_row(p_rows[0]);
	const int columns = header.size();
	const String cell_options = " border=#" + p_style.border.to_html(true) + cell_padding(p_style, 0.4f, 0.2f) + "]";

	String out = "[table=" + itos(columns) + "]";
	for (const String &cell : header) {
		out += "[cell bg=#" + p_style.table_header_background.to_html(true) + cell_options + "[b]" + render_inline(cell, p_style, 0) + "[/b][/cell]";
	}
	// Row 1 is the separator.
	for (int row = 2; row < p_rows.size(); row++) {
		const Vector<String> cells = split_table_row(p_rows[row]);
		for (int column = 0; column < columns; column++) {
			const String cell = column < cells.size() ? cells[column] : String();
			out += "[cell" + cell_options + (cell.is_empty() ? String(" ") : render_inline(cell, p_style, 0)) + "[/cell]";
		}
	}
	return out + "[/table]";
}

} // namespace

String AIMarkdown::escape(const String &p_text) {
	return p_text.replace("[", "[lb]");
}

String AIMarkdown::inline_to_bbcode(const String &p_text, const AIMarkdownStyle &p_style) {
	return render_inline(p_text, p_style, 0);
}

String AIMarkdown::to_bbcode(const String &p_markdown, const AIMarkdownStyle &p_style) {
	AIMarkdownStream stream;
	stream.reset(p_style);
	String out = stream.push(p_markdown);
	out += stream.finish();
	return out;
}

// =============================================================================
// Blocks
// =============================================================================

String AIMarkdownStream::Blocks::_block(const String &p_bbcode) {
	const String out = (blank_before ? "\n" : "") + p_bbcode + "\n";
	blank_before = false;
	wrote_block = true;
	return out;
}

String AIMarkdownStream::Blocks::_code_block() const {
	const String body = String("\n").join(code_lines);
	return "[table=1][cell expand=1 bg=#" + style.code_background.to_html(true) + " border=#" + style.border.to_html(true) + cell_padding(style, 0.5f, 0.3f) + "]" +
			"[code]" + (body.is_empty() ? String(" ") : AIMarkdown::escape(body)) + "[/code][/cell][/table]";
}

String AIMarkdownStream::Blocks::_flush_table() {
	const Vector<String> rows = table_rows;
	table_rows.clear();
	if (rows.size() >= 2 && is_table_separator(rows[1])) {
		return _block(render_table(rows, style));
	}
	// Lines that start with a pipe but are not a table.
	String out;
	for (const String &row : rows) {
		out += _block(render_inline(row, style, 0));
	}
	return out;
}

String AIMarkdownStream::Blocks::_line(const String &p_line) {
	const String stripped = p_line.strip_edges();
	const int indent = indent_width(p_line);

	const int level = heading_level(stripped);
	if (level > 0) {
		list_indents.clear();
		String title = stripped.substr(level).strip_edges();
		// A closing run of #s is decoration.
		const int closing = title.rfind_char(' ');
		if (closing >= 0 && run_length(title, closing + 1, '#') == title.length() - closing - 1) {
			title = title.left(closing).strip_edges();
		}
		const int size = (int)Math::round(style.font_size * HEADING_SCALE[level - 1]);
		return "[font_size=" + itos(size) + "]" + color_tag(style.heading) + "[b]" + render_inline(title, style, 0) + "[/b][/color][/font_size]";
	}

	if (is_rule(stripped)) {
		list_indents.clear();
		return "[hr width=100% height=1 color=#" + style.border.to_html(true) + "]";
	}

	if (stripped.begins_with(">")) {
		list_indents.clear();
		int depth = 0;
		String content = stripped;
		while (content.begins_with(">")) {
			depth++;
			content = content.substr(1).strip_edges();
		}
		// A bar (▎) per level of quoting.
		return color_tag(style.quote) + String::utf8("\xe2\x96\x8e").repeat(depth) + " [i]" + render_inline(content, style, 0) + "[/i][/color]";
	}

	const ListItem item = parse_list_item(stripped);
	if (item.found) {
		while (!list_indents.is_empty() && list_indents[list_indents.size() - 1] > indent) {
			list_indents.remove_at(list_indents.size() - 1);
		}
		if (list_indents.is_empty() || list_indents[list_indents.size() - 1] < indent) {
			list_indents.push_back(indent);
		}
		const int depth = list_indents.size();

		String marker;
		String content = item.content;
		if (content.begins_with("[ ] ")) {
			marker = String::utf8("\xe2\x98\x90"); // ☐
			content = content.substr(4);
		} else if (content.begins_with("[x] ") || content.begins_with("[X] ")) {
			marker = String::utf8("\xe2\x98\x91"); // ☑
			content = content.substr(4);
		} else if (!item.number.is_empty()) {
			marker = item.number + ".";
		} else {
			// •, then ◦ for nested items.
			marker = depth == 1 ? String::utf8("\xe2\x80\xa2") : String::utf8("\xe2\x97\xa6");
		}
		return repeat_tag("[indent]", depth) + marker + " " + render_inline(content, style, 0) + repeat_tag("[/indent]", depth);
	}

	// An indented line after a list item continues it.
	if (!list_indents.is_empty() && indent > 0) {
		const int depth = list_indents.size();
		return repeat_tag("[indent]", depth) + render_inline(stripped, style, 0) + repeat_tag("[/indent]", depth);
	}
	list_indents.clear();
	return render_inline(stripped, style, 0);
}

String AIMarkdownStream::Blocks::take_line(const String &p_line) {
	const String line = p_line.trim_suffix("\r");
	const String stripped = line.strip_edges();

	if (in_fence) {
		const bool closes = stripped.begins_with(fence) && run_length(stripped, 0, fence[0]) == stripped.length();
		if (!closes) {
			code_lines.push_back(remove_indent(line, fence_indent));
			return String();
		}
		in_fence = false;
		const String out = _block(_code_block());
		code_lines.clear();
		return out;
	}

	const bool is_table_row = stripped.begins_with("|");
	String out;
	if (!table_rows.is_empty() && !is_table_row) {
		out += _flush_table();
	}
	if (is_table_row) {
		table_rows.push_back(stripped);
		return out;
	}

	const char32_t first = stripped.is_empty() ? 0 : stripped[0];
	if (first == '`' || first == '~') {
		const int run = run_length(stripped, 0, first);
		// After the fence comes the language, which cannot contain a backtick
		// (that would be an inline code span).
		if (run >= 3 && (first == '~' || !stripped.substr(run).contains_char('`'))) {
			in_fence = true;
			fence = stripped.left(run);
			fence_indent = indent_width(line);
			return out;
		}
	}

	if (stripped.is_empty()) {
		blank_before = wrote_block;
		return out;
	}
	return out + _block(_line(line));
}

String AIMarkdownStream::Blocks::flush() {
	String out;
	if (in_fence) {
		in_fence = false;
		out += _block(_code_block());
		code_lines.clear();
	}
	if (!table_rows.is_empty()) {
		out += _flush_table();
	}
	return out;
}

// =============================================================================
// Stream
// =============================================================================

void AIMarkdownStream::reset(const AIMarkdownStyle &p_style) {
	blocks = Blocks();
	blocks.style = p_style;
	partial = String();
}

String AIMarkdownStream::push(const String &p_text) {
	partial += p_text;
	String out;
	int start = 0;
	int newline = partial.find_char('\n', start);
	while (newline >= 0) {
		out += blocks.take_line(partial.substr(start, newline - start));
		start = newline + 1;
		newline = partial.find_char('\n', start);
	}
	partial = partial.substr(start);
	return out;
}

String AIMarkdownStream::get_pending() const {
	Blocks ahead = blocks;
	String out = partial.is_empty() ? String() : ahead.take_line(partial);
	out += ahead.flush();
	return out;
}

String AIMarkdownStream::finish() {
	String out = partial.is_empty() ? String() : blocks.take_line(partial);
	partial = String();
	out += blocks.flush();
	return out;
}
