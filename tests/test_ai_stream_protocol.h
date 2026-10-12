#pragma once

#include "../ai_stream_protocol.h"

#include "tests/test_macros.h"

namespace TestAIStreamProtocol {

static Vector<String> _decode(AISSEDecoder &p_decoder, const CharString &p_bytes) {
	Vector<String> payloads;
	p_decoder.push_bytes((const uint8_t *)p_bytes.get_data(), p_bytes.length(), payloads);
	return payloads;
}

static String _join(const Vector<AIStreamPiece> &p_pieces, bool p_thinking) {
	String text;
	for (int i = 0; i < p_pieces.size(); i++) {
		if (p_pieces[i].is_thinking == p_thinking) {
			text += p_pieces[i].text;
		}
	}
	return text;
}

TEST_CASE("[AIAssistant][Stream] SSE decoder returns one payload per complete event") {
	AISSEDecoder decoder;

	const Vector<String> payloads = _decode(decoder, String("data: {\"a\":1}\n\ndata: {\"b\":2}\n\n").utf8());

	REQUIRE(payloads.size() == 2);
	CHECK(payloads[0] == "{\"a\":1}");
	CHECK(payloads[1] == "{\"b\":2}");
}

TEST_CASE("[AIAssistant][Stream] SSE decoder buffers an event split across chunks") {
	AISSEDecoder decoder;

	const Vector<String> first = _decode(decoder, String("data: {\"text\":\"hel").utf8());
	const Vector<String> second = _decode(decoder, String("lo\"}\n\n").utf8());

	CHECK(first.is_empty());
	REQUIRE(second.size() == 1);
	CHECK(second[0] == "{\"text\":\"hello\"}");
}

TEST_CASE("[AIAssistant][Stream] SSE decoder keeps a multi-byte character split across chunks intact") {
	AISSEDecoder decoder;
	// "你" is E4 BD A0 in UTF-8; cut it after the first byte.
	const CharString whole = String::utf8("data: 你好\n\n").utf8();
	const int cut = 7;

	Vector<String> payloads;
	decoder.push_bytes((const uint8_t *)whole.get_data(), cut, payloads);
	decoder.push_bytes((const uint8_t *)whole.get_data() + cut, whole.length() - cut, payloads);

	REQUIRE(payloads.size() == 1);
	CHECK(payloads[0] == String::utf8("你好"));
}

TEST_CASE("[AIAssistant][Stream] SSE decoder accepts CRLF framing and data lines without a space") {
	AISSEDecoder decoder;

	const Vector<String> payloads = _decode(decoder, String("event: delta\r\ndata:first\r\n\r\ndata: second\r\n\r\n").utf8());

	REQUIRE(payloads.size() == 2);
	CHECK(payloads[0] == "first");
	CHECK(payloads[1] == "second");
}

TEST_CASE("[AIAssistant][Stream] SSE decoder joins multiple data lines and skips events without data") {
	AISSEDecoder decoder;

	const Vector<String> payloads = _decode(decoder, String(": keep-alive\n\ndata: line one\ndata: line two\n\n").utf8());

	REQUIRE(payloads.size() == 1);
	CHECK(payloads[0] == "line one\nline two");
}

TEST_CASE("[AIAssistant][Stream] SSE decoder flush returns a last event that was not terminated") {
	AISSEDecoder decoder;

	const Vector<String> complete = _decode(decoder, String("data: first\n\ndata: last").utf8());
	const String flushed = decoder.flush();
	const String flushed_again = decoder.flush();

	REQUIRE(complete.size() == 1);
	CHECK(flushed == "last");
	CHECK(flushed_again.is_empty());
}

TEST_CASE("[AIAssistant][Stream] utf8_complete_prefix_len stops before an incomplete sequence") {
	const uint8_t complete[] = { 'a', 0xE4, 0xBD, 0xA0 };
	const uint8_t cut_after_lead[] = { 'a', 0xE4 };
	const uint8_t cut_mid_sequence[] = { 'a', 0xE4, 0xBD };
	const uint8_t four_byte_cut[] = { 0xF0, 0x9F, 0x98 };

	CHECK(AISSEDecoder::utf8_complete_prefix_len(complete, 4) == 4);
	CHECK(AISSEDecoder::utf8_complete_prefix_len(cut_after_lead, 2) == 1);
	CHECK(AISSEDecoder::utf8_complete_prefix_len(cut_mid_sequence, 3) == 1);
	CHECK(AISSEDecoder::utf8_complete_prefix_len(four_byte_cut, 3) == 0);
}

TEST_CASE("[AIAssistant][Stream] think splitter separates reasoning from the answer") {
	AIThinkTagSplitter splitter;
	Vector<AIStreamPiece> pieces;

	splitter.push("intro <think>let me see</think>answer", pieces);
	splitter.flush(pieces);

	CHECK(_join(pieces, false) == "intro answer");
	CHECK(_join(pieces, true) == "let me see");
}

TEST_CASE("[AIAssistant][Stream] think splitter recognises tags split across deltas") {
	AIThinkTagSplitter splitter;
	Vector<AIStreamPiece> pieces;

	splitter.push("before<thi", pieces);
	splitter.push("nk>reasoning</th", pieces);
	splitter.push("ink>after", pieces);
	splitter.flush(pieces);

	CHECK(_join(pieces, false) == "beforeafter");
	CHECK(_join(pieces, true) == "reasoning");
}

TEST_CASE("[AIAssistant][Stream] think splitter releases text that only looked like a tag") {
	AIThinkTagSplitter splitter;
	Vector<AIStreamPiece> pieces;

	splitter.push("if a <", pieces);
	const String shown_before_flush = _join(pieces, false);
	splitter.push(" b: pass", pieces);
	splitter.flush(pieces);

	// The "<" is held back until it is clear no tag follows.
	CHECK(shown_before_flush == "if a ");
	CHECK(_join(pieces, false) == "if a < b: pass");
	CHECK(_join(pieces, true).is_empty());
}

TEST_CASE("[AIAssistant][Stream] think splitter flushes an unterminated think block as reasoning") {
	AIThinkTagSplitter splitter;
	Vector<AIStreamPiece> pieces;

	splitter.push("<think>never closed</thi", pieces);
	splitter.flush(pieces);

	CHECK(_join(pieces, false).is_empty());
	CHECK(_join(pieces, true) == "never closed</thi");
}

} // namespace TestAIStreamProtocol
