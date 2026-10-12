#pragma once

#include "../ai_session.h"

#include "tests/test_macros.h"

namespace TestAISession {

// A session with p_pairs user/assistant exchanges of roughly p_chars characters each.
static AISession _make_session(int p_pairs, int p_chars) {
	AISession session;
	for (int i = 0; i < p_pairs; i++) {
		session.push_user("question " + itos(i) + " " + String("x").repeat(p_chars));
		session.push_assistant("answer " + itos(i) + " " + String("y").repeat(p_chars));
	}
	return session;
}

static String _role(const Array &p_messages, int p_index) {
	return Dictionary(p_messages[p_index])["role"];
}

static String _content(const Array &p_messages, int p_index) {
	return Dictionary(p_messages[p_index])["content"];
}

TEST_CASE("[AIAssistant][Session] estimate_tokens counts CJK per character and other text per four") {
	CHECK(AISession::estimate_tokens(String("a").repeat(400)) == 100);
	CHECK(AISession::estimate_tokens(String::utf8("你好世界")) == 4);
	CHECK(AISession::estimate_tokens(String::utf8("你好") + String("a").repeat(8)) == 4);
	CHECK(AISession::estimate_tokens("") == 1);
}

TEST_CASE("[AIAssistant][Session] build_api_messages sends only role and content") {
	AISession session;
	session.push_user("hello");
	session.push_assistant("hi there", "private reasoning");

	const Array messages = session.build_api_messages();

	REQUIRE(messages.size() == 2);
	const Dictionary assistant = messages[1];
	CHECK(assistant.size() == 2);
	CHECK(String(assistant["content"]) == "hi there");
	CHECK_FALSE(assistant.has("thinking"));
	// The reasoning is still kept in the full record for display.
	CHECK(String(Dictionary(session.get_full_history()[1])["thinking"]) == "private reasoning");
}

TEST_CASE("[AIAssistant][Session] pop_last_user takes back only a trailing user message") {
	AISession session;
	session.push_user("first");
	session.push_assistant("reply");
	session.push_user("failed request");

	String popped;
	const bool took_user = session.pop_last_user(popped);
	String ignored;
	const bool took_assistant = session.pop_last_user(ignored);

	CHECK(took_user);
	CHECK(popped == "failed request");
	CHECK_FALSE(took_assistant);
	CHECK(session.get_working_size() == 2);
	CHECK(session.get_full_history().size() == 2);
}

TEST_CASE("[AIAssistant][Session] compress_if_needed leaves a history below the threshold alone") {
	AISession session = _make_session(10, 40);
	AISession::CompressionConfig config;

	const AISession::CompressionResult result = session.compress_if_needed(128000, 4096, 0, config);

	CHECK_FALSE(result.compressed);
	CHECK(session.get_working_size() == 20);
	CHECK(session.get_summary().is_empty());
}

TEST_CASE("[AIAssistant][Session] compress_if_needed folds old messages into the summary and keeps the recent ones") {
	// 20 messages of ~2000 tokens each against an 8K window.
	AISession session = _make_session(10, 8000);
	AISession::CompressionConfig config;
	config.keep_recent = 6;

	const AISession::CompressionResult result = session.compress_if_needed(8192, 1024, 0, config);

	CHECK(result.compressed);
	CHECK(result.removed.size() == 14);
	CHECK(session.get_working_size() == 6);
	CHECK(_role(session.build_api_messages(), 2) == "user"); // After the summary pair.
	CHECK(session.get_summary().begins_with("--- Compressed messages ---\n"));
	CHECK(session.get_summary().find("User: question 0") >= 0);
	// The full record is never trimmed.
	CHECK(session.get_full_history().size() == 20);
}

TEST_CASE("[AIAssistant][Session] compress_if_needed rounds an odd keep_recent up so history starts on a user turn") {
	AISession session = _make_session(10, 8000);
	AISession::CompressionConfig config;
	config.keep_recent = 5;

	session.compress_if_needed(8192, 1024, 0, config);

	CHECK(session.get_working_size() == 6);
	const Array messages = session.build_api_messages();
	CHECK(_content(messages, 0).begins_with("[CONTEXT SUMMARY"));
	CHECK(_role(messages, 1) == "assistant");
	CHECK(_content(messages, 2).begins_with("question 7"));
}

TEST_CASE("[AIAssistant][Session] compress_if_needed counts the system prompt against the window") {
	AISession session = _make_session(4, 4000); // ~8000 tokens of history.
	AISession::CompressionConfig config;
	config.keep_recent = 2;

	// 40K window: fine on its own, too small once a 28K system prompt is counted.
	AISession roomy = session;
	const bool compressed_without_prompt = roomy.compress_if_needed(40000, 4000, 0, config).compressed;
	const bool compressed_with_prompt = session.compress_if_needed(40000, 4000, 28000, config).compressed;

	CHECK_FALSE(compressed_without_prompt);
	CHECK(compressed_with_prompt);
}

TEST_CASE("[AIAssistant][Session] summarize_message keeps the request and drops attached context") {
	Dictionary message;
	message["role"] = "user";
	message["content"] = "add a player\n\n--- ATTACHED CONTEXT ---\n" + String("z").repeat(5000);

	const String summary = AISession::summarize_message(message);

	CHECK(summary == "User: add a player");
}

TEST_CASE("[AIAssistant][Session] summarize_message describes code replies by language, files and symbols") {
	Dictionary message;
	message["role"] = "assistant";
	message["content"] = "Done.\n```gdscript\nfunc build_level():\n\tvar scene = load(\"res://level.tscn\")\n```";

	const String summary = AISession::summarize_message(message);

	CHECK(summary.begins_with("AI: [Code/gdscript]"));
	CHECK(summary.find("files=res://level.tscn") >= 0);
	CHECK(summary.find("func build_level():") >= 0);
}

TEST_CASE("[AIAssistant][Session] replace_summary_block swaps exactly the block it was given") {
	AISession session = _make_session(10, 8000);
	AISession::CompressionConfig config;
	const AISession::CompressionResult result = session.compress_if_needed(8192, 1024, 0, config);

	const bool replaced = session.replace_summary_block(result.block, "--- AI Summary ---\nshort\n");
	const bool replaced_again = session.replace_summary_block(result.block, "stale");

	CHECK(replaced);
	CHECK(session.get_summary() == "--- AI Summary ---\nshort\n");
	// A reply for a block that is no longer there is a no-op.
	CHECK_FALSE(replaced_again);
}

TEST_CASE("[AIAssistant][Session] the summary attachment is replaced, not accumulated") {
	AISession session = _make_session(10, 8000);
	AISession::CompressionConfig config;
	session.compress_if_needed(8192, 1024, 0, config);
	const int tokens_without = session.estimate_history_tokens();

	session.set_summary_attachment(String("s").repeat(4000));
	session.set_summary_attachment(String("s").repeat(4000));

	// Two snapshots in a row cost as much as one.
	CHECK(session.estimate_history_tokens() == tokens_without + 1000);
	CHECK(_content(session.build_api_messages(), 0).ends_with(String("s").repeat(4000)));
	CHECK(session.to_dict()["context_summary"] == Variant(session.get_summary()));
}

TEST_CASE("[AIAssistant][Session] parse_overflow_error reads Anthropic and OpenAI token counts") {
	const AISession::OverflowInfo anthropic = AISession::parse_overflow_error("API Error (HTTP 400): prompt is too long: 195432 tokens > 180000 maximum");
	const AISession::OverflowInfo openai = AISession::parse_overflow_error("This model's maximum context length is 128000 tokens. However, your messages resulted in 130500 tokens.");
	const AISession::OverflowInfo generic = AISession::parse_overflow_error("error code: context_length_exceeded");
	const AISession::OverflowInfo unrelated = AISession::parse_overflow_error("API Error (HTTP 401): invalid api key");

	CHECK(anthropic.is_overflow);
	CHECK(anthropic.actual_tokens == 195432);
	CHECK(anthropic.limit_tokens == 180000);
	CHECK(openai.is_overflow);
	CHECK(openai.actual_tokens == 130500);
	CHECK(openai.limit_tokens == 128000);
	CHECK(generic.is_overflow);
	CHECK(generic.actual_tokens == -1);
	CHECK_FALSE(unrelated.is_overflow);
}

TEST_CASE("[AIAssistant][Session] drop_oldest_for_gap frees enough tokens but never the failed request") {
	AISession session = _make_session(3, 4000); // 6 messages of ~1000 tokens.
	session.push_user("the request that overflowed");

	const AISession::DropResult dropped = session.drop_oldest_for_gap(2500);

	CHECK(dropped.dropped_messages == 3);
	CHECK(dropped.freed_tokens >= 2500);
	CHECK(session.get_working_size() == 4);
	CHECK(session.get_summary().find("[reactive-compacted] User: question 0") >= 0);

	// Even an impossible gap leaves the last message in place.
	session.drop_oldest_for_gap(1000000);
	String last;
	CHECK(session.get_working_size() == 1);
	CHECK(session.pop_last_user(last));
	CHECK(last == "the request that overflowed");
}

TEST_CASE("[AIAssistant][Session] get_overflow_gap adds a safety margin to the reported excess") {
	AISession session;
	AISession::OverflowInfo info;
	info.is_overflow = true;
	info.actual_tokens = 130500;
	info.limit_tokens = 128000;

	CHECK(session.get_overflow_gap(info) == 3000);
}

TEST_CASE("[AIAssistant][Session] large attachments are sent once and referenced afterwards") {
	AISession session;
	const String big = String("line\n").repeat(1000);

	const String first = session.build_attachment_content("res://notes.md", big);
	const String second = session.build_attachment_content("res://notes.md", big);
	const String changed = session.build_attachment_content("res://notes.md", big + "more");
	const String small = session.build_attachment_content("res://tiny.md", "hi");

	CHECK(first.begins_with("--- ATTACHED FILE #1: notes.md ---\n"));
	CHECK(AISession::is_attachment_backref(second));
	CHECK(second.length() < 200);
	CHECK(changed.begins_with("--- ATTACHED FILE #1 (updated): notes.md ---\n"));
	CHECK(small == "--- ATTACHED FILE: tiny.md ---\nhi\n--- END FILE ---");
}

TEST_CASE("[AIAssistant][Session] clear forgets attachments so a new chat sends them in full again") {
	AISession session;
	const String big = String("line\n").repeat(1000);
	session.build_attachment_content("res://notes.md", big);

	session.clear();
	const String after_clear = session.build_attachment_content("res://notes.md", big);

	CHECK_FALSE(AISession::is_attachment_backref(after_clear));
	CHECK(session.is_empty());
	CHECK(session.get_chat_id().is_empty());
}

TEST_CASE("[AIAssistant][Session] a compressed chat reloads with the same working copy") {
	AISession session = _make_session(10, 8000);
	session.set_chat_id("chat_20260101_120000");
	AISession::CompressionConfig config;
	session.compress_if_needed(8192, 1024, 0, config);

	AISession restored;
	const bool loaded = restored.from_dict(session.to_dict());

	CHECK(loaded);
	CHECK(restored.get_chat_id() == "chat_20260101_120000");
	CHECK(restored.get_full_history().size() == 20);
	CHECK(restored.get_working_size() == session.get_working_size());
	CHECK(restored.get_summary() == session.get_summary());
	CHECK(restored.get_title() == session.get_title());
}

TEST_CASE("[AIAssistant][Session] a summary without its message count is dropped rather than sent twice") {
	Dictionary data = _make_session(3, 10).to_dict();
	data["context_summary"] = "old summary from a save that did not record how much it covers";

	AISession restored;
	restored.from_dict(data);

	CHECK(restored.get_summary().is_empty());
	CHECK(restored.get_working_size() == 6);
}

TEST_CASE("[AIAssistant][Session] from_dict rejects data that is not a chat and keeps the session") {
	AISession session = _make_session(1, 10);
	Dictionary not_a_chat;
	not_a_chat["title"] = "no messages key";

	const bool loaded = session.from_dict(not_a_chat);

	CHECK_FALSE(loaded);
	CHECK(session.get_working_size() == 2);
}

TEST_CASE("[AIAssistant][Session] make_chat_id produces a sortable timestamp id") {
	Dictionary datetime;
	datetime["year"] = 2026;
	datetime["month"] = 3;
	datetime["day"] = 7;
	datetime["hour"] = 9;
	datetime["minute"] = 5;
	datetime["second"] = 1;

	CHECK(AISession::make_chat_id(datetime) == "chat_20260307_090501");
}


TEST_CASE("[AIAssistant][Session] a forgotten attachment is sent in full again") {
	AISession session;
	const String big = String("x").repeat(20000);
	const String first = session.build_attachment_content("res://big.gd", big);
	REQUIRE(session.build_attachment_content("res://big.gd", big).contains("already provided"));

	session.forget_attachments(); // The request carrying it failed, or it was compressed away.
	const String again = session.build_attachment_content("res://big.gd", big);

	CHECK_FALSE(again.contains("already provided"));
	CHECK(again.contains(big));
	CHECK(first.contains(big));
}

TEST_CASE("[AIAssistant][Session] what the transcript shows for a user turn is kept apart from what is sent") {
	AISession session;

	session.push_user("Make it jump\n--- FILE: player.gd ---\nextends Node", "Make it jump");
	session.push_assistant("ok");
	session.push_user("The code failed with error: x", String(), true);

	const Array stored = session.get_full_history();
	const Array sent = session.build_api_messages();
	REQUIRE(stored.size() == 3);
	CHECK(String(Dictionary(stored[0])["display"]) == "Make it jump");
	CHECK(bool(Dictionary(stored[2])["automatic"]));
	// The model gets the full text, and none of the local-only keys.
	CHECK(String(Dictionary(sent[0])["content"]).contains("--- FILE: player.gd ---"));
	CHECK_FALSE(Dictionary(sent[0]).has("display"));
	CHECK_FALSE(Dictionary(sent[2]).has("automatic"));
}

TEST_CASE("[AIAssistant][Session] a user turn typed as-is carries no separate display text") {
	AISession session;

	session.push_user("Add a sprite", "Add a sprite");

	CHECK_FALSE(Dictionary(session.get_full_history()[0]).has("display"));
}

} // namespace TestAISession
