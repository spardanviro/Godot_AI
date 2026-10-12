#pragma once

#include "../ai_markdown.h"

#include "tests/test_macros.h"

namespace TestAIMarkdown {

static String _spans(const String &p_text) {
	return AIMarkdown::inline_to_bbcode(p_text, AIMarkdownStyle());
}

static String _render(const String &p_markdown) {
	return AIMarkdown::to_bbcode(p_markdown, AIMarkdownStyle());
}

TEST_CASE("[AIAssistant][Markdown] Text reaches the label escaped, never as markup") {
	CHECK(_spans("[b]not bold[/b]") == "[lb]b]not bold[lb]/b]");
	CHECK(_spans("`[url=x]`").contains("[code][lb]url=x][/code]"));
	CHECK(_render("```\n[img]res://icon.svg[/img]\n```").contains("[lb]img]res://icon.svg[lb]/img]"));
	CHECK(_render("| [color=red]a |\n|---|\n| [b]c |").contains("[lb]color=red]a"));
}

TEST_CASE("[AIAssistant][Markdown] Emphasis") {
	CHECK(_spans("a **bold** b") == "a [b]bold[/b] b");
	CHECK(_spans("a *it* b") == "a [i]it[/i] b");
	CHECK(_spans("a _it_ b") == "a [i]it[/i] b");
	CHECK(_spans("***both***") == "[b][i]both[/i][/b]");
	CHECK(_spans("~~gone~~").contains("[s]gone[/s]"));
	CHECK(_spans("**a *b* c**") == "[b]a [i]b[/i] c[/b]");
	CHECK(_spans(U"**加粗**文字") == U"[b]加粗[/b]文字");
}

TEST_CASE("[AIAssistant][Markdown] What only looks like emphasis is left alone") {
	CHECK(_spans("move_and_slide and is_on_floor") == "move_and_slide and is_on_floor");
	CHECK(_spans("__init__ file") == "[b]init[/b] file");
	CHECK(_spans("2 * 3 * 4") == "2 * 3 * 4");
	CHECK(_spans("**unclosed") == "**unclosed");
	CHECK(_spans("a \\*literal\\* b") == "a *literal* b");
	CHECK(_spans("`a * b * c`").contains("[code]a * b * c[/code]"));
}

TEST_CASE("[AIAssistant][Markdown] Code spans keep their content as written") {
	CHECK(_spans("call `move_and_slide()` now").contains("[code]move_and_slide()[/code]"));
	CHECK(_spans("``a ` b``").contains("[code]a ` b[/code]"));
	CHECK(_spans("`**x**`").contains("[code]**x**[/code]"));
	CHECK(_spans("an ` unclosed") == "an ` unclosed");
}

TEST_CASE("[AIAssistant][Markdown] Only web addresses become links") {
	CHECK(_spans("[docs](https://docs.godotengine.org/en/stable/)").contains("[url=https://docs.godotengine.org/en/stable/]docs[/url]"));
	CHECK(_spans("see https://godotengine.org.").contains("[url=https://godotengine.org]https://godotengine.org[/url]"));
	CHECK(_spans(U"见https://godotengine.org。很好").contains("[url=https://godotengine.org]"));

	CHECK_FALSE(_spans("[run](file:///C:/Windows/System32/calc.exe)").contains("[url"));
	CHECK_FALSE(_spans("[script](res://player.gd)").contains("[url"));
	CHECK(_spans("[script](res://player.gd)") == "script (res://player.gd)");
	// A target that could close the tag early is not a link.
	CHECK_FALSE(_spans("[x](https://a.b/]c[img]d)").contains("[url"));
	CHECK(_spans("array[0](1)") == "array[lb]0](1)");
	CHECK(_spans("[not a link]") == "[lb]not a link]");
}

TEST_CASE("[AIAssistant][Markdown] Headings, rules and quotes") {
	AIMarkdownStyle style;
	style.font_size = 20;
	const String heading = AIMarkdown::to_bbcode("# Title", style);
	CHECK(heading.begins_with("[font_size=30]"));
	CHECK(heading.contains("[b]Title[/b]"));
	CHECK(AIMarkdown::to_bbcode("### Small ###", style).contains("[font_size=23]"));
	CHECK(AIMarkdown::to_bbcode("### Small ###", style).contains("[b]Small[/b]"));

	CHECK(_render("#hashtag") == "#hashtag\n");
	CHECK(_render("---").begins_with("[hr "));
	CHECK(_render("> quoted **text**").contains("[i]quoted [b]text[/b][/i]"));
}

TEST_CASE("[AIAssistant][Markdown] Lists keep their numbers and nest by indentation") {
	const String list = _render("- one\n- two\n  - inner\n- three");
	const PackedStringArray lines = list.split("\n");
	REQUIRE(lines.size() == 5);
	CHECK(lines[0] == String(U"[indent]• one[/indent]"));
	CHECK(lines[2] == String(U"[indent][indent]◦ inner[/indent][/indent]"));
	CHECK(lines[3] == String(U"[indent]• three[/indent]"));

	CHECK(_render("3. third\n4. fourth") == "[indent]3. third[/indent]\n[indent]4. fourth[/indent]\n");
	CHECK(_render("- [ ] open\n- [x] done") == String(U"[indent]☐ open[/indent]\n[indent]☑ done[/indent]\n"));
	// A marker needs a space after it.
	CHECK(_render("-5 degrees") == "-5 degrees\n");
	CHECK(_render("3.14 is pi") == "3.14 is pi\n");
}

TEST_CASE("[AIAssistant][Markdown] Fenced code is one block, untouched by inline rules") {
	const String code = _render("Before\n```gdscript\nfunc _ready():\n\tprint(\"**hi**\")\n```\nAfter");
	CHECK(code.begins_with("Before\n[table=1]"));
	CHECK(code.contains("[code]func _ready():\n\tprint(\"**hi**\")[/code]"));
	CHECK(code.ends_with("[/table]\nAfter\n"));
	CHECK_FALSE(code.contains("gdscript"));

	// A longer fence holds a shorter one; an unclosed fence runs to the end.
	CHECK(_render("````\n```\ninner\n```\n````").contains("[code]```\ninner\n```[/code]"));
	CHECK(_render("```\nnever closed").contains("[code]never closed[/code]"));
	// Indentation of the fence itself is taken off the code.
	CHECK(_render("- item\n  ```\n  x = 1\n    y = 2\n  ```").contains("[code]x = 1\n  y = 2[/code]"));
}

TEST_CASE("[AIAssistant][Markdown] Tables") {
	const String table = _render("| Name | Type |\n|:-----|-----:|\n| `hp` | int |\n| only one |");
	CHECK(table.begins_with("[table=2]"));
	CHECK(table.contains("[b]Name[/b]"));
	CHECK(table.contains("[code]hp[/code]"));
	// Four cells of two rows plus the padded one.
	CHECK(table.count("[/cell]") == 6);
	CHECK(table.ends_with("[/table]\n"));

	// Pipes without a separator row are ordinary lines.
	CHECK(_render("| just | text |") == "| just | text |\n");
	CHECK(_render("| a \\| b | `c|d` |\n|---|---|").begins_with("[table=2]"));
}

TEST_CASE("[AIAssistant][Markdown] Blank lines separate blocks once and are dropped at the edges") {
	CHECK(_render("\n\nfirst\n\n\n\nsecond\n\n") == "first\n\nsecond\n");
	CHECK(_render("a\nb") == "a\nb\n");
	CHECK(_render("") == "");
	CHECK(_render("line one<br>line two") == "line one\nline two\n");
}

TEST_CASE("[AIAssistant][Markdown] A stream renders the same however the text is cut") {
	const String document = String(U"# 标题\n\n一段 **加粗** 和 `代码`，还有 [链接](https://example.com/a_b)。\n\n- 第一\n  - 嵌套\n1. 编号\n\n") +
			"```gdscript\nvar a := [1, 2]\n```\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n> quote\n\n---\nlast *line*";
	const AIMarkdownStyle style;
	const String whole = AIMarkdown::to_bbcode(document, style);

	for (int chunk = 1; chunk <= 7; chunk++) {
		AIMarkdownStream stream;
		stream.reset(style);
		String streamed;
		for (int pos = 0; pos < document.length(); pos += chunk) {
			streamed += stream.push(document.substr(pos, chunk));
			// What is shown at any moment is what is final plus the open block.
			const String shown = streamed + stream.get_pending();
			const String prefix = AIMarkdown::to_bbcode(document.left(MIN(pos + chunk, document.length())), style);
			const String where = vformat("chunk size %d at %d", chunk, pos);
			CHECK_MESSAGE(shown == prefix, where);
		}
		streamed += stream.finish();
		const String where = vformat("chunk size %d", chunk);
		CHECK_MESSAGE(streamed == whole, where);
	}
}

TEST_CASE("[AIAssistant][Markdown] The open block of a stream is provisional") {
	AIMarkdownStream stream;
	stream.reset(AIMarkdownStyle());

	CHECK(stream.push("Intro\n```gd") == "Intro\n");
	// The box is up as soon as the fence opens.
	CHECK(stream.get_pending().begins_with("[table=1]"));
	CHECK(stream.push("script\nvar a") == "");
	CHECK(stream.get_pending().contains("[code]var a[/code]"));
	CHECK(stream.push(" = 1\n```") == "");
	// The closing fence is not final until its line ends.
	CHECK(stream.get_pending().contains("[code]var a = 1[/code]"));
	CHECK(stream.push("\n").contains("[code]var a = 1[/code]"));
	CHECK(stream.get_pending().is_empty());

	CHECK(stream.push("| A |\n") == "");
	CHECK(stream.get_pending() == "| A |\n");
	CHECK(stream.push("|---|\n| 1") == "");
	CHECK(stream.get_pending().begins_with("[table=1]"));
	CHECK(stream.finish().begins_with("[table=1]"));
	CHECK(stream.finish().is_empty());
}

} // namespace TestAIMarkdown
