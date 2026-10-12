#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

// Turns raw response bytes into Server-Sent-Events "data:" payloads. Network
// chunks split wherever they like — inside a multi-byte character, inside an
// event — so both are buffered until complete.
class AISSEDecoder {
	PackedByteArray pending_bytes; // Undecoded tail (an incomplete UTF-8 sequence).
	String buffer; // Decoded text that does not yet form a whole event.

public:
	// Appends the payload of every event completed by these bytes.
	void push_bytes(const uint8_t *p_data, int p_len, Vector<String> &r_payloads);
	// The stream ended: returns the payload of a last event that was not
	// terminated by a blank line ("" if there is none).
	String flush();
	void reset();

	// Length of the longest prefix that ends on a UTF-8 character boundary.
	static int utf8_complete_prefix_len(const uint8_t *p_data, int p_len);
	// Joins the "data:" lines of one event block.
	static String extract_data(const String &p_event);
};

// Separates <think>...</think> reasoning that some models embed in their
// content stream from the answer itself. Tags may be split across deltas.
class AIThinkTagSplitter {
	bool inside = false;
	String held; // Trailing text that might be the start of a tag.

	void _emit(const String &p_text, bool p_thinking, Vector<struct AIStreamPiece> &r_pieces) const;

public:
	void push(const String &p_delta, Vector<struct AIStreamPiece> &r_pieces);
	// Releases text held back as a possible partial tag. Call at end of stream.
	void flush(Vector<struct AIStreamPiece> &r_pieces);
	void reset();
};

struct AIStreamPiece {
	String text;
	bool is_thinking = false;
};
