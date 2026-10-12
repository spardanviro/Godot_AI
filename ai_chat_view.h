#pragma once

#ifdef TOOLS_ENABLED

#include "ai_markdown.h"
#include "ai_response_parser.h"

#include "scene/gui/margin_container.h"

class RichTextLabel;

// The chat transcript: messages, the reply as it streams in, one-line
// execution summaries and the clickable links attached to them. What the
// model writes is rendered as Markdown. It only renders what it is told; a
// click on a link is reported through the link_clicked(kind, id) signal, and
// a web link in a reply opens in the browser.
class AIChatView : public MarginContainer {
	GDCLASS(AIChatView, MarginContainer);

public:
	enum LinkKind {
		LINK_NONE,
		LINK_SAVE, // Save a code block to the project.
		LINK_DETAILS, // Show the full reply behind a summary line.
		LINK_REVERT, // Restore a checkpoint.
		LINK_THINKING, // Show the model's reasoning.
		LINK_EXECUTE_PLAN,
	};

	struct Link {
		LinkKind kind = LINK_NONE;
		int id = -1;
	};

	static Link parse_link(const String &p_meta);
	// Content behind a link id; empty for an unknown id.
	String get_code_block(int p_id) const;
	String get_detail_response(int p_id) const;
	String get_thinking_text(int p_id) const;

	// The transcript without formatting.
	String get_plain_text() const;

	// Clears the transcript and everything its links point to.
	void reset();
	void append_message(const String &p_sender, const String &p_text, const Color &p_color, bool p_bbcode = false);
	void append_user_message(const String &p_bbcode_text);
	// p_level is an AIAgentController::NoticeLevel.
	void append_notice(int p_level, const String &p_text);
	void append_separator();

	// --- The reply in flight ---
	// p_show_content: write the answer as it arrives (otherwise only the
	// reasoning is shown live and the outcome is summarised afterwards).
	void begin_stream(bool p_show_content);
	void add_stream_delta(const String &p_text, bool p_is_thinking);
	// p_outcome is an AIStreamClient::Outcome.
	void end_stream(int p_outcome);

	// --- Controller reports (see the signals of AIAgentController) ---
	void show_response(const Dictionary &p_response);
	void show_execution(const Dictionary &p_report);
	void show_plan(const Dictionary &p_plan);

	// Rebuilds the transcript from a session's full history.
	void show_history(const Array &p_history, const Ref<AIResponseParser> &p_parser);

	AIChatView();

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	RichTextLabel *text = nullptr; // RichTextLabel cannot be subclassed, so it is wrapped.
	Vector<String> code_blocks;
	Vector<String> detail_responses;
	Vector<String> thinking_texts;

	// How the reply currently streaming in is being shown.
	struct StreamDisplay {
		bool active = false; // Between begin_stream() and end_stream().
		bool show_content = false;
		bool placeholder_shown = false; // Generic "Thinking..." line is up.
		bool thinking_shown = false; // Reasoning tokens are being written.
		bool thinking_collapsed = false; // Reasoning was folded into a link.
		bool prefix_shown = false; // The answer has started.
		bool prefix_written = false; // "AI: " is in the transcript for good.
		int paragraph_start = 0; // First paragraph of the placeholder / reasoning.
		// First paragraph of the part of the answer that is rewritten as it
		// grows (see AIMarkdownStream); -1 when none is up.
		int tail_start = -1;
		String thinking_text;
		AIMarkdownStream markdown;
	};
	StreamDisplay stream_display;

	// While a reply streams in, the end of the transcript belongs to it:
	// the placeholder and the reasoning are removed again, and the answer
	// keeps rewriting its last block. A message that arrives meanwhile (an asset
	// finished generating, the game reported an error) is held back and
	// written once the reply is complete.
	struct DeferredMessage {
		String sender;
		String text;
		Color color;
		bool bbcode = false;
	};
	Vector<DeferredMessage> deferred_messages;

	void _append_ai_prefix();
	AIMarkdownStyle _markdown_style() const;
	// "AI: " followed by p_markdown, rendered.
	void _append_ai_markdown(const String &p_markdown);
	void _write_stream_answer(const String &p_bbcode);
	void _remove_stream_tail();
	void _append_link(LinkKind p_kind, int p_id, const String &p_text, const Color &p_color);
	void _append_thinking_link(const String &p_thinking);
	int _current_paragraph() const;
	void _remove_paragraphs_from(int p_paragraph);
	void _remove_stream_paragraphs();
	void _collapse_thinking();
	void _on_meta_clicked(const Variant &p_meta);
};

#endif // TOOLS_ENABLED
