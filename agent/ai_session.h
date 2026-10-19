#pragma once

#include "../common/ai_tool_types.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// One conversation: what was said, what is sent to the model, and how the
// history is shrunk when it outgrows the context window. Holds no UI and does
// no network I/O, so every rule in here can be unit tested.
class AISession {
public:
	struct CompressionConfig {
		int keep_recent = 6; // Recent messages that are never compressed.
		float threshold_pct = 0.70f; // Fraction of the usable window that triggers compression.
	};

	struct CompressionResult {
		bool compressed = false;
		Array removed; // The messages that were folded into the summary.
		String block; // The summary block that stands in for them.
		int tokens_before = 0;
		int threshold = 0;
	};

	struct OverflowInfo {
		bool is_overflow = false;
		int actual_tokens = -1; // -1 when the error did not carry numbers.
		int limit_tokens = -1;
	};

	struct DropResult {
		int dropped_messages = 0;
		int freed_tokens = 0;
	};

	// --- Messages ---
	void clear();
	bool is_empty() const { return working.is_empty(); }
	int get_working_size() const { return working.size(); }
	const Array &get_full_history() const { return full; }
	const String &get_summary() const { return summary; }

	// p_display: what the user typed, when p_content carries more than that
	// (inlined files, context). p_automatic: the loop wrote this turn itself
	// (a retry prompt, web results); the transcript does not show it as
	// something the user said. Both stay local and are never sent.
	void push_user(const String &p_content, const String &p_display = String(), bool p_automatic = false);
	void push_assistant(const String &p_content, const String &p_thinking = String());
	// A tool round: the reply that asked for tools (its text may be empty),
	// then one result per call.
	// p_thinking_signature: see AIStreamClient::Result; empty for most providers.
	void push_assistant_tool_calls(const String &p_content, const String &p_thinking, const Vector<AIToolCall> &p_calls, const String &p_thinking_signature = String());
	void push_tool_result(const AIToolCall &p_call, const String &p_result);
	// Takes back the last message if it is a user message (to resend it after a
	// recoverable API error). Returns false if there is nothing to take back.
	bool pop_last_user(String &r_content);

	// History as the API expects it: the summary (if any) as a leading
	// user/assistant pair, then role + content of every working message.
	//
	// Tool rounds: with p_native_tools they are passed on as they were
	// stored — the assistant message with "tool_calls", each result as a
	// "tool" message with "tool_call_id" — for a provider that speaks tool
	// calls. Without it, and for a round that is no longer whole (compression
	// cut it, a result is missing), they are written out as ordinary text
	// that any provider accepts.
	//
	// With p_with_reasoning every assistant message also carries "thinking":
	// the reasoning stored with it, empty when none was kept; and
	// "thinking_signature" when one was stored with it.
	Array build_api_messages(bool p_native_tools = false, bool p_with_reasoning = false) const;

	// --- Compression ---
	// Rough token count: CJK characters count 1 each, everything else 1 per 4.
	static int estimate_tokens(const String &p_text);
	// Whether stored reasoning goes out with the history, and so takes room.
	void set_reasoning_sent(bool p_sent) { reasoning_sent = p_sent; }
	int estimate_history_tokens() const;
	// One-line stand-in for a message that is being compressed away.
	static String summarize_message(const Dictionary &p_message);
	// Folds the oldest messages into the summary once the history exceeds the
	// share of the context window that is left for it.
	CompressionResult compress_if_needed(int p_context_length, int p_max_output_tokens, int p_system_prompt_tokens, const CompressionConfig &p_config);
	// Extra context sent along with the summary (the scripts being edited).
	// Replaced, never accumulated, and not saved with the chat.
	void set_summary_attachment(const String &p_text) { summary_attachment = p_text; }
	// Swaps a block produced by compress_if_needed() for a model-written
	// summary. Returns false if that exact block is no longer in the summary.
	bool replace_summary_block(const String &p_block, const String &p_replacement);

	// Recognises "context too long" API errors and extracts the token counts.
	static OverflowInfo parse_overflow_error(const String &p_error);
	// How many tokens must be freed to get back under the limit.
	int get_overflow_gap(const OverflowInfo &p_info) const;
	// Drops the oldest messages (leaving the last one, the request that just
	// failed, in place) until at least p_gap_tokens are freed.
	DropResult drop_oldest_for_gap(int p_gap_tokens);

	// --- Attachments ---
	// Large files are sent once per session; attaching the same unchanged file
	// again yields a short back-reference instead of the content.
	String build_attachment_content(const String &p_path, const String &p_raw_content);
	// After this, every file is attached in full again. Called whenever the
	// model may no longer have (or never got) what "already provided" refers to.
	void forget_attachments();
	static bool is_attachment_backref(const String &p_attachment) { return p_attachment.begins_with("[Attached file #"); }

	// --- Persistence ---
	const String &get_chat_id() const { return chat_id; }
	void set_chat_id(const String &p_id) { chat_id = p_id; }
	// "chat_YYYYMMDD_HHMMSS" from a datetime dictionary.
	static String make_chat_id(const Dictionary &p_datetime);
	String get_title() const;
	Dictionary to_dict() const;
	// Returns false (leaving the session untouched) if the data is not a chat.
	bool from_dict(const Dictionary &p_data);

	static const int ATTACHMENT_INLINE_LIMIT = 1500; // Characters below which dedup is not worth it.

private:
	bool reasoning_sent = false;
	int _estimate_message_tokens(const Dictionary &p_message) const;

	Array working; // Sent to the model; may be compressed.
	Array full; // Complete record for display and saving; never trimmed.
	String summary; // Stands in for the messages removed from the working copy.
	String summary_attachment;
	String chat_id;

	HashMap<String, int> attachment_ids; // File path -> attachment id.
	HashMap<int, String> attachment_contents; // Attachment id -> last content sent.
	int next_attachment_id = 1;
};
