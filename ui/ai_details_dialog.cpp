#ifdef TOOLS_ENABLED

#include "ai_details_dialog.h"

#include "../common/ai_code_text.h"
#include "../common/ai_log.h"
#include "ai_markdown.h"
#include "ai_theme.h"

#include "editor/themes/editor_scale.h"
#include "scene/gui/rich_text_label.h"

static const int CODE_FONT_SIZE = 13;
static const Size2 DIALOG_SIZE(700, 500);
static const Size2 TEXT_MIN_SIZE(650, 400);

static const Color COLOR_TEXT(0.85, 0.85, 0.9);
static const Color COLOR_MUTED(0.5, 0.5, 0.6);
static const Color COLOR_THINKING(0.55, 0.55, 0.65);
static const Color COLOR_CODE(0.9, 0.85, 0.7);
static const Color COLOR_CODE_BACKGROUND(0.12, 0.12, 0.16, 0.9);

void AIDetailsDialog::show_response(const String &p_response, const Ref<AIResponseParser> &p_parser) {
	if (p_response.is_empty()) {
		return;
	}
	const Dictionary parsed = p_parser->parse(p_response);
	const PackedStringArray text_segments = PackedStringArray(parsed["text_segments"]);
	Vector<String> code_blocks;
	const Array raw_blocks = parsed["code_blocks"];
	for (int i = 0; i < raw_blocks.size(); i++) {
		code_blocks.push_back(raw_blocks[i]);
	}
	code_blocks = AICodeText::dedupe_consecutive(code_blocks);

	text->clear();
	ai_use_code_font(text);
	const AIMarkdownStyle markdown_style = ai_markdown_style(text->get_theme_font_size(SNAME("normal_font_size")));
	for (int i = 0; i < text_segments.size(); i++) {
		text->push_color(ai_themed(COLOR_TEXT));
		text->append_text(AIMarkdown::to_bbcode(text_segments[i], markdown_style));
		text->pop();
		text->add_newline();
	}
	for (int i = 0; i < code_blocks.size(); i++) {
		text->push_color(ai_themed(COLOR_MUTED));
		text->add_text("--- GDScript ---");
		text->pop();
		text->add_newline();

		text->push_bgcolor(COLOR_CODE_BACKGROUND);
		text->push_color(ai_themed(COLOR_CODE));
		text->push_font_size(CODE_FONT_SIZE);
		text->add_text(code_blocks[i]);
		text->pop();
		text->pop();
		text->pop();
		text->add_newline();
		text->add_newline();
	}
	set_title(AI_L("AI Response Details", "AI 回复详情"));
	popup_centered(DIALOG_SIZE * EDSCALE);
}

void AIDetailsDialog::show_thinking(const String &p_thinking) {
	if (p_thinking.is_empty()) {
		return;
	}
	text->clear();
	text->push_color(ai_themed(COLOR_THINKING));
	text->push_italics();
	text->add_text(p_thinking);
	text->pop();
	text->pop();
	set_title(TR(STR_THINKING_LABEL));
	popup_centered(DIALOG_SIZE * EDSCALE);
}

AIDetailsDialog::AIDetailsDialog() {
	set_ok_button_text(AI_L("Close", "关闭"));

	text = memnew(RichTextLabel);
	text->set_custom_minimum_size(TEXT_MIN_SIZE * EDSCALE);
	text->set_selection_enabled(true);
	text->set_scroll_follow(false);
	add_child(text);
}

#endif // TOOLS_ENABLED
