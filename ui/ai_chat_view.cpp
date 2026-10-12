#ifdef TOOLS_ENABLED

#include "ai_chat_view.h"

#include "../agent/ai_agent_controller.h"
#include "../common/ai_log.h"
#include "../agent/ai_response_analysis.h"
#include "../network/ai_stream_client.h"
#include "ai_theme.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/gui/rich_text_label.h"

static const Color COLOR_USER(0.9, 0.9, 0.9);
static const Color COLOR_AI(0.7, 0.85, 1.0);
static const Color COLOR_INFO(0.6, 0.8, 1.0);
static const Color COLOR_SUCCESS(0.5, 1.0, 0.5);
static const Color COLOR_WARNING(1.0, 0.75, 0.3);
static const Color COLOR_ERROR(1.0, 0.4, 0.4);
static const Color COLOR_THINKING(0.55, 0.55, 0.65);
static const Color COLOR_MUTED(0.5, 0.5, 0.6);
static const Color COLOR_STOPPED(0.6, 0.6, 0.6);
static const Color COLOR_SEPARATOR(0.3, 0.3, 0.4);
static const Color COLOR_LINK_SAVE(0.4, 0.8, 1.0);
static const Color COLOR_LINK_DETAILS(0.6, 0.6, 0.8);
static const Color COLOR_LINK_REVERT(0.5, 0.7, 1.0);
static const Color COLOR_LINK_THINKING(0.4, 0.7, 1.0);
static const Color COLOR_PLAN(0.85, 0.92, 0.88);
static const Color COLOR_PLAN_FILE(0.55, 0.75, 0.55);
static const Color COLOR_PLAN_BUTTON(0.3, 1.0, 0.55);
static const Color COLOR_PLAN_BUTTON_BACKGROUND(0.12, 0.38, 0.18, 0.6);

static const int SUMMARY_MAX_CHARS = 300;

// Link targets are "<kind>:<id>".
static const struct {
	AIChatView::LinkKind kind;
	const char *prefix;
} LINK_PREFIXES[] = {
	{ AIChatView::LINK_SAVE, "save:" },
	{ AIChatView::LINK_DETAILS, "details:" },
	{ AIChatView::LINK_REVERT, "revert:" },
	{ AIChatView::LINK_THINKING, "thinking:" },
	{ AIChatView::LINK_EXECUTE_PLAN, "execute_plan:" },
};

static String _link_meta(AIChatView::LinkKind p_kind, int p_id) {
	for (const auto &entry : LINK_PREFIXES) {
		if (entry.kind == p_kind) {
			return entry.prefix + itos(p_id);
		}
	}
	return String();
}

static String _at(const Vector<String> &p_items, int p_id) {
	return p_id >= 0 && p_id < p_items.size() ? p_items[p_id] : String();
}

void AIChatView::_bind_methods() {
	// kind is a LinkKind; id identifies the code block, reply or checkpoint.
	ADD_SIGNAL(MethodInfo("link_clicked", PropertyInfo(Variant::INT, "kind"), PropertyInfo(Variant::INT, "id")));
}

void AIChatView::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		ai_use_code_font(text);
	}
}

void AIChatView::_on_meta_clicked(const Variant &p_meta) {
	// A link the model wrote. The renderer only makes web addresses clickable;
	// checked again here, since this hands the address to the system.
	const String target = p_meta;
	if (target.begins_with("https://") || target.begins_with("http://")) {
		OS::get_singleton()->shell_open(target);
		return;
	}
	const Link link = parse_link(p_meta);
	if (link.kind != LINK_NONE) {
		emit_signal(SNAME("link_clicked"), (int)link.kind, link.id);
	}
}

AIChatView::Link AIChatView::parse_link(const String &p_meta) {
	Link link;
	for (const auto &entry : LINK_PREFIXES) {
		if (p_meta.begins_with(entry.prefix)) {
			link.kind = entry.kind;
			link.id = p_meta.trim_prefix(entry.prefix).to_int();
			break;
		}
	}
	return link;
}

String AIChatView::get_code_block(int p_id) const {
	return _at(code_blocks, p_id);
}

String AIChatView::get_detail_response(int p_id) const {
	return _at(detail_responses, p_id);
}

String AIChatView::get_thinking_text(int p_id) const {
	return _at(thinking_texts, p_id);
}

String AIChatView::get_plain_text() const {
	return text->get_parsed_text();
}

void AIChatView::reset() {
	text->clear();
	code_blocks.clear();
	detail_responses.clear();
	thinking_texts.clear();
	stream_display = StreamDisplay();
	deferred_messages.clear();
}

// =============================================================================
// Messages
// =============================================================================

void AIChatView::append_message(const String &p_sender, const String &p_text, const Color &p_color, bool p_bbcode) {
	if (stream_display.active) {
		DeferredMessage message;
		message.sender = p_sender;
		message.text = p_text;
		message.color = p_color;
		message.bbcode = p_bbcode;
		deferred_messages.push_back(message);
		return;
	}
	text->push_color(ai_themed(p_color));
	text->push_bold();
	text->add_text(p_sender + ": ");
	text->pop();
	text->pop();

	if (p_bbcode) {
		text->append_text(p_text);
	} else {
		text->add_text(p_text);
	}
	text->add_newline();
	text->add_newline();
}

void AIChatView::append_user_message(const String &p_bbcode_text) {
	// BBCode, because mentions are rendered as chips.
	append_message("You", p_bbcode_text, COLOR_USER, true);
}

void AIChatView::append_notice(int p_level, const String &p_text) {
	Color color = COLOR_INFO;
	switch (p_level) {
		case AIAgentController::NOTICE_SUCCESS:
			color = COLOR_SUCCESS;
			break;
		case AIAgentController::NOTICE_WARNING:
			color = COLOR_WARNING;
			break;
		case AIAgentController::NOTICE_ERROR:
			color = COLOR_ERROR;
			break;
		default:
			break;
	}
	append_message("System", p_text, color);
}

void AIChatView::append_separator() {
	text->push_color(ai_themed(COLOR_SEPARATOR));
	text->add_text(String::utf8("────────────────────────────────────────"));
	text->pop();
	text->add_newline();
}

void AIChatView::_append_ai_prefix() {
	text->push_color(ai_themed(COLOR_AI));
	text->push_bold();
	text->add_text("AI: ");
	text->pop();
	text->pop();
}

AIMarkdownStyle AIChatView::_markdown_style() const {
	return ai_markdown_style(text->get_theme_font_size(SNAME("normal_font_size")));
}

void AIChatView::_append_ai_markdown(const String &p_markdown) {
	_append_ai_prefix();
	// Every block ends its own line.
	text->append_text(AIMarkdown::to_bbcode(p_markdown, _markdown_style()));
	text->add_newline();
}

void AIChatView::_append_link(LinkKind p_kind, int p_id, const String &p_text, const Color &p_color) {
	text->push_color(ai_themed(p_color));
	text->push_meta(_link_meta(p_kind, p_id));
	text->add_text(p_text);
	text->pop();
	text->pop();
}

void AIChatView::_append_thinking_link(const String &p_thinking) {
	thinking_texts.push_back(p_thinking);

	text->push_color(ai_themed(COLOR_MUTED));
	text->push_italics();
	text->add_text("[");
	_append_link(LINK_THINKING, thinking_texts.size() - 1, TR(STR_THINKING_COLLAPSED), COLOR_LINK_THINKING);
	text->add_text("]");
	text->pop();
	text->pop();
	text->add_newline();
}

// =============================================================================
// The reply in flight
// =============================================================================

void AIChatView::begin_stream(bool p_show_content) {
	stream_display = StreamDisplay();
	stream_display.active = true;
	stream_display.show_content = p_show_content;
	stream_display.markdown.reset(_markdown_style());

	// A placeholder until the first token arrives; reasoning tokens replace it
	// and the first answer token removes it.
	stream_display.paragraph_start = _current_paragraph();
	text->push_color(ai_themed(COLOR_THINKING));
	text->push_italics();
	text->add_text(TR(STR_THINKING_LABEL) + "...");
	text->pop();
	text->pop();
	text->add_newline();
	stream_display.placeholder_shown = true;
}

// Index of the paragraph that text added next goes into. The label always has
// one open paragraph at the end, so this is the last one.
int AIChatView::_current_paragraph() const {
	return text->get_paragraph_count() - 1;
}

// Removes everything from p_paragraph up to, but not including, the open
// paragraph at the end. Callers end what they wrote with a newline first, so
// the open paragraph is always an empty one that is safe to keep writing into.
void AIChatView::_remove_paragraphs_from(int p_paragraph) {
	const int count = _current_paragraph() - p_paragraph;
	for (int i = 0; i < count; i++) {
		text->remove_paragraph(p_paragraph);
	}
}

void AIChatView::_remove_stream_paragraphs() {
	_remove_paragraphs_from(stream_display.paragraph_start);
}

// Takes down the provisional end of the answer, to write it again with the
// text that has arrived since.
void AIChatView::_remove_stream_tail() {
	if (stream_display.tail_start >= 0) {
		_remove_paragraphs_from(stream_display.tail_start);
		stream_display.tail_start = -1;
	}
}

// p_bbcode comes from the Markdown stream: whole blocks, each ending its line.
void AIChatView::_write_stream_answer(const String &p_bbcode) {
	if (p_bbcode.is_empty()) {
		return;
	}
	// Until a block is final the prefix is part of the tail and goes down with it.
	if (!stream_display.prefix_written) {
		_append_ai_prefix();
	}
	text->append_text(p_bbcode);
}

void AIChatView::_collapse_thinking() {
	stream_display.thinking_shown = false;
	stream_display.thinking_collapsed = true;
	text->pop(); // italics
	text->pop(); // color
	text->add_newline();
	_remove_stream_paragraphs();
	_append_thinking_link(stream_display.thinking_text);
}

void AIChatView::add_stream_delta(const String &p_text, bool p_is_thinking) {
	if (p_is_thinking) {
		stream_display.thinking_text += p_text;
		if (stream_display.thinking_collapsed || stream_display.prefix_shown) {
			// Reasoning after the answer started is kept for the link only:
			// showing it would mean removing paragraphs the answer is in.
			return;
		}
		if (!stream_display.thinking_shown) {
			if (stream_display.placeholder_shown) {
				_remove_stream_paragraphs();
				stream_display.placeholder_shown = false;
			}
			stream_display.thinking_shown = true;
			stream_display.paragraph_start = _current_paragraph();
			text->push_color(ai_themed(COLOR_THINKING));
			text->push_italics();
			text->add_text(TR(STR_THINKING_LABEL) + ": ");
		}
		text->add_text(p_text);
		return;
	}

	if (stream_display.placeholder_shown) {
		_remove_stream_paragraphs();
		stream_display.placeholder_shown = false;
	}
	if (stream_display.thinking_shown) {
		_collapse_thinking();
	}
	if (!stream_display.show_content) {
		return;
	}
	stream_display.prefix_shown = true;

	// Blocks the new text completed are written for good; the block still
	// open is written provisionally and replaced on the next delta.
	_remove_stream_tail();
	const String completed = stream_display.markdown.push(p_text);
	if (!completed.is_empty()) {
		_write_stream_answer(completed);
		stream_display.prefix_written = true;
	}
	const String pending = stream_display.markdown.get_pending();
	if (!pending.is_empty()) {
		stream_display.tail_start = _current_paragraph();
		_write_stream_answer(pending);
	}
}

void AIChatView::end_stream(int p_outcome) {
	if (stream_display.placeholder_shown) {
		_remove_stream_paragraphs();
		stream_display.placeholder_shown = false;
	}
	if (stream_display.thinking_shown) {
		_collapse_thinking();
	}
	if (stream_display.thinking_collapsed && !thinking_texts.is_empty()) {
		// Reasoning that arrived after the fold belongs behind the same link.
		thinking_texts.write[thinking_texts.size() - 1] = stream_display.thinking_text;
	}

	bool answer_written = stream_display.prefix_written;
	if (stream_display.prefix_shown) {
		_remove_stream_tail();
		const String rest = stream_display.markdown.finish();
		_write_stream_answer(rest);
		answer_written = answer_written || !rest.is_empty();
	}

	const bool stopped = p_outcome == AIStreamClient::OUTCOME_STOPPED;
	if (stopped) {
		text->push_color(ai_themed(COLOR_STOPPED));
		text->push_italics();
		text->add_text(AI_L("[stopped]", "[已停止]"));
		text->pop();
		text->pop();
		text->add_newline();
	}
	// The answer's blocks and the marker each ended their own line.
	if (answer_written || stopped) {
		text->add_newline();
	}

	stream_display.active = false;
	const Vector<DeferredMessage> held_back = deferred_messages;
	deferred_messages.clear();
	for (int i = 0; i < held_back.size(); i++) {
		append_message(held_back[i].sender, held_back[i].text, held_back[i].color, held_back[i].bbcode);
	}
}

// =============================================================================
// Controller reports
// =============================================================================

void AIChatView::show_response(const Dictionary &p_response) {
	const PackedStringArray text_segments = p_response["text_segments"];
	const PackedStringArray blocks = p_response["code_blocks"];
	const bool streamed = p_response["streamed"];
	const bool show_text = p_response["show_text"];

	// Text that was not shown while streaming is shown now.
	if (show_text && !streamed && !text_segments.is_empty()) {
		_append_ai_markdown(String("\n\n").join(text_segments));
	}

	for (int i = 0; i < blocks.size(); i++) {
		code_blocks.push_back(blocks[i]);
		// Code read in ASK mode is not executed; offer to save it instead.
		if (streamed) {
			_append_link(LINK_SAVE, code_blocks.size() - 1, "[Save]", COLOR_LINK_SAVE);
			text->add_text(" ");
		}
	}
	if (streamed && !blocks.is_empty()) {
		text->add_newline();
		text->add_newline();
	}
}

void AIChatView::show_execution(const Dictionary &p_report) {
	const bool success = p_report["success"];
	const bool compact = p_report["compact"];
	const int checkpoint_id = p_report["checkpoint_id"];
	const String error = p_report["error"];

	if (!compact) {
		if (!success) {
			append_message("System", AI_L("Execution failed: ", "执行失败：") + error, COLOR_ERROR);
			return;
		}
		if (checkpoint_id >= 0) {
			_append_link(LINK_REVERT, checkpoint_id, "[Revert]", COLOR_LINK_REVERT);
			text->add_text(" ");
		}
		append_message("System", String(U"✅ ") + String(p_report["output"]), COLOR_SUCCESS);
		return;
	}

	// One line: "AI: ✅ what happened  [Details] [Save] [Revert]".
	detail_responses.push_back(p_report["response"]);

	_append_ai_prefix();
	if (success) {
		const String summary = p_report["summary"];
		text->push_color(ai_themed(COLOR_SUCCESS));
		text->add_text(String(U"✅ ") + (summary.length() > SUMMARY_MAX_CHARS ? summary.left(SUMMARY_MAX_CHARS) + "..." : summary));
	} else {
		text->push_color(ai_themed(COLOR_ERROR));
		text->add_text(String(U"❌ ") + error);
	}
	text->pop();
	text->add_text("  ");

	_append_link(LINK_DETAILS, detail_responses.size() - 1, String(U"[▶ Details]"), COLOR_LINK_DETAILS);
	if (success && !code_blocks.is_empty()) {
		text->add_text(" ");
		_append_link(LINK_SAVE, code_blocks.size() - 1, "[Save]", COLOR_LINK_SAVE);
	}
	if (success && checkpoint_id >= 0) {
		text->add_text(" ");
		_append_link(LINK_REVERT, checkpoint_id, "[Revert]", COLOR_LINK_REVERT);
	}
	text->add_newline();
	text->add_newline();
}

void AIChatView::show_plan(const Dictionary &p_plan) {
	const PackedStringArray text_segments = p_plan["text_segments"];
	const PackedStringArray steps = p_plan["steps"];
	const String file = p_plan["file"];

	text->push_color(ai_themed(COLOR_PLAN));
	text->push_bold();
	text->add_text(String(U"📋 ") + AI_L("Plan:", "计划："));
	text->pop();
	text->pop();
	text->add_newline();
	text->append_text(AIMarkdown::to_bbcode(String("\n\n").join(text_segments), _markdown_style()));

	if (!file.is_empty()) {
		text->add_newline();
		text->push_color(ai_themed(COLOR_PLAN_FILE));
		text->add_text(String(U"📄 ") + file);
		text->pop();
	}

	text->add_newline();
	if (steps.is_empty()) {
		// Without numbered steps there is nothing to run one by one.
		text->push_color(ai_themed(COLOR_WARNING));
		text->add_text(AI_L("No numbered steps were found in this plan, so it cannot be executed step by step.", "计划中没有找到编号步骤，无法逐步执行。"));
		text->pop();
	} else {
		text->push_bgcolor(COLOR_PLAN_BUTTON_BACKGROUND);
		text->push_color(ai_themed(COLOR_PLAN_BUTTON));
		text->push_bold();
		text->push_meta(_link_meta(LINK_EXECUTE_PLAN, 0));
		text->add_text(String(U"  ▶  ") + AI_L("Execute Plan", "执行计划") + "  ");
		text->pop(); // meta
		text->pop(); // bold
		text->pop(); // color
		text->pop(); // bgcolor
	}
	text->add_newline();
	text->add_newline();
}

void AIChatView::show_history(const Array &p_history, const Ref<AIResponseParser> &p_parser) {
	static const String THINK_OPEN = "<think>";
	static const String THINK_CLOSE = "</think>";

	reset();
	if (p_history.is_empty()) {
		return;
	}
	// Following the scroll while bulk-loading would leave the view at the bottom.
	text->set_scroll_follow(false);

	for (int i = 0; i < p_history.size(); i++) {
		const Dictionary message = p_history[i];
		const String role = message.get("role", "");
		String content = message.get("content", "");

		if (role == "user") {
			// Shown as it was typed. Turns the loop wrote itself (retry
			// prompts, web results) were never the user's words.
			if (bool(message.get("automatic", false))) {
				continue;
			}
			append_message("You", message.get("display", content), COLOR_USER);
			continue;
		}
		if (role != "assistant") {
			continue;
		}

		String thinking = message.get("thinking", "");
		// Older chats stored the reasoning inline.
		const int think_start = content.find(THINK_OPEN);
		const int think_end = content.find(THINK_CLOSE);
		if (think_start >= 0 && think_end > think_start) {
			if (thinking.is_empty()) {
				thinking = content.substr(think_start + THINK_OPEN.length(), think_end - think_start - THINK_OPEN.length()).strip_edges();
			}
			content = (content.left(think_start) + content.substr(think_end + THINK_CLOSE.length())).strip_edges();
		}
		if (!thinking.is_empty()) {
			_append_thinking_link(thinking);
		}

		const Dictionary parsed = p_parser->parse(content);
		const PackedStringArray text_segments = PackedStringArray(parsed["text_segments"]);
		Vector<String> blocks;
		const Array raw_blocks = parsed["code_blocks"];
		for (int j = 0; j < raw_blocks.size(); j++) {
			blocks.push_back(raw_blocks[j]);
		}
		blocks = AIResponseAnalysis::dedupe_consecutive(blocks);

		// One message, however many code blocks cut its text into segments.
		const String reply_text = String("\n\n").join(text_segments).strip_edges();
		if (!reply_text.is_empty()) {
			_append_ai_markdown(reply_text);
		}
		if (blocks.is_empty()) {
			continue;
		}

		detail_responses.push_back(content);
		code_blocks.append_array(blocks);

		_append_ai_prefix();
		text->push_color(ai_themed(COLOR_SUCCESS));
		// The history does not say whether the code ran (in ASK mode it does
		// not), so the fallback summary, which says it did, is not used here.
		const String summary = AIResponseAnalysis::summarize_code(blocks[blocks.size() - 1]);
		const bool says_nothing = summary == AIResponseAnalysis::summarize_code(String());
		text->add_text(says_nothing ? AI_L("Code", "代码") + " (" + itos(blocks.size()) + ")" : summary);
		text->pop();
		text->add_text("  ");
		_append_link(LINK_DETAILS, detail_responses.size() - 1, String(U"[▶ Details]"), COLOR_LINK_DETAILS);
		text->add_text(" ");
		_append_link(LINK_SAVE, code_blocks.size() - 1, "[Save]", COLOR_LINK_SAVE);
		text->add_newline();
		text->add_newline();
	}

	text->scroll_to_line(0);
	text->set_scroll_follow(true);
}

AIChatView::AIChatView() {
	text = memnew(RichTextLabel);
	text->connect("meta_clicked", callable_mp(this, &AIChatView::_on_meta_clicked));
	add_child(text);
	text->set_scroll_follow(true);
	text->set_selection_enabled(true);
	text->set_context_menu_enabled(true);
	text->set_use_bbcode(true);
}

#endif // TOOLS_ENABLED
