#pragma once

#include "core/math/color.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Colours and sizes the Markdown renderer writes into its BBCode.
struct AIMarkdownStyle {
	int font_size = 16; // Of body text; headings are scaled from it.
	Color heading = Color(0.85, 0.92, 1.0);
	Color code = Color(0.95, 0.75, 0.55); // Inline code. A code block keeps the text colour.
	// Translucent greys, so they read as a tint on dark and light themes alike.
	Color code_background = Color(0.5, 0.5, 0.5, 0.16);
	Color border = Color(0.5, 0.5, 0.5, 0.4);
	Color table_header_background = Color(0.5, 0.5, 0.5, 0.22);
	Color quote = Color(0.62, 0.66, 0.72);
	Color link = Color(0.45, 0.72, 1.0);
};

// Turns the Markdown a model writes into RichTextLabel BBCode: headings,
// emphasis, inline code, fenced code blocks, lists, quotes, rules, links and
// tables. Anything it does not recognise is shown as written. Raw text never
// reaches the label as markup: every "[" in it is escaped.
class AIMarkdown {
public:
	static String escape(const String &p_text);
	// One line's worth of inline markup: emphasis, code spans, links.
	static String inline_to_bbcode(const String &p_text, const AIMarkdownStyle &p_style);
	// A whole document. Every block ends with a newline; blank lines at the
	// start and the end are dropped.
	static String to_bbcode(const String &p_markdown, const AIMarkdownStyle &p_style);
};

// The same conversion for text that arrives in pieces. Blocks that are
// complete can be written for good; what is still open — a half line, a table
// or a code block still being written — is rendered provisionally and
// replaced as more text comes in.
class AIMarkdownStream {
public:
	void reset(const AIMarkdownStyle &p_style);
	// Adds text and returns the BBCode of the blocks it completed.
	String push(const String &p_text);
	// The BBCode of what is still open, as it would look if the text ended here.
	String get_pending() const;
	// Ends the text: returns the BBCode of everything still open.
	String finish();

private:
	// What has been read of the document so far, line by line.
	struct Blocks {
		AIMarkdownStyle style;
		bool in_fence = false;
		String fence; // The run of ` or ~ that opened the code block.
		int fence_indent = 0; // Indentation of that run, taken off every code line.
		Vector<String> code_lines;
		Vector<String> table_rows;
		bool wrote_block = false;
		bool blank_before = false; // A blank line is owed before the next block.
		Vector<int> list_indents; // Indentation of each list the last line was nested in.

		String take_line(const String &p_line);
		// Closes a code block or table left open.
		String flush();

	private:
		String _block(const String &p_bbcode);
		String _flush_table();
		String _code_block() const;
		String _line(const String &p_line);
	};

	Blocks blocks;
	String partial; // The line still being received.
};
