#include "ai_stream_protocol.h"

int AISSEDecoder::utf8_complete_prefix_len(const uint8_t *p_data, int p_len) {
	// Walk back over at most 3 continuation bytes to the last lead byte.
	for (int back = 1; back <= 4 && back <= p_len; back++) {
		const uint8_t b = p_data[p_len - back];
		if ((b & 0xC0) == 0x80) {
			continue; // Continuation byte — keep looking for its lead.
		}
		int need = 1;
		if ((b & 0xE0) == 0xC0) {
			need = 2;
		} else if ((b & 0xF0) == 0xE0) {
			need = 3;
		} else if ((b & 0xF8) == 0xF0) {
			need = 4;
		}
		return need > back ? p_len - back : p_len;
	}
	return p_len;
}

String AISSEDecoder::extract_data(const String &p_event) {
	const Vector<String> lines = p_event.split("\n");
	String data;
	bool first = true;
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (!line.begins_with("data:")) {
			continue;
		}
		if (!first) {
			data += "\n";
		}
		first = false;
		// The space after the colon is optional.
		data += line.substr(line.begins_with("data: ") ? 6 : 5);
	}
	return data;
}

void AISSEDecoder::push_bytes(const uint8_t *p_data, int p_len, Vector<String> &r_payloads) {
	if (p_len <= 0) {
		return;
	}
	const int old_size = pending_bytes.size();
	pending_bytes.resize(old_size + p_len);
	memcpy(pending_bytes.ptrw() + old_size, p_data, p_len);

	const int decodable = utf8_complete_prefix_len(pending_bytes.ptr(), pending_bytes.size());
	if (decodable > 0) {
		buffer += String::utf8((const char *)pending_bytes.ptr(), decodable);
		pending_bytes = pending_bytes.slice(decodable);
	}

	while (true) {
		// Events end with a blank line; accept both LF and CRLF framing.
		const int lf = buffer.find("\n\n");
		const int crlf = buffer.find("\r\n\r\n");
		int event_end = lf;
		int skip_len = 2;
		if (lf < 0 || (crlf >= 0 && crlf < lf)) {
			event_end = crlf;
			skip_len = 4;
		}
		if (event_end < 0) {
			break;
		}
		const String payload = extract_data(buffer.substr(0, event_end));
		buffer = buffer.substr(event_end + skip_len);
		if (!payload.is_empty()) {
			r_payloads.push_back(payload);
		}
	}
}

String AISSEDecoder::flush() {
	const String payload = extract_data(buffer);
	reset();
	return payload;
}

void AISSEDecoder::reset() {
	pending_bytes.clear();
	buffer = String();
}

// Number of trailing characters of p_text that are a proper prefix of p_tag.
static int _partial_tag_suffix_len(const String &p_text, const String &p_tag) {
	const int max_len = MIN(p_text.length(), p_tag.length() - 1);
	for (int len = max_len; len > 0; len--) {
		if (p_text.ends_with(p_tag.left(len))) {
			return len;
		}
	}
	return 0;
}

void AIThinkTagSplitter::_emit(const String &p_text, bool p_thinking, Vector<AIStreamPiece> &r_pieces) const {
	if (p_text.is_empty()) {
		return;
	}
	AIStreamPiece piece;
	piece.text = p_text;
	piece.is_thinking = p_thinking;
	r_pieces.push_back(piece);
}

void AIThinkTagSplitter::push(const String &p_delta, Vector<AIStreamPiece> &r_pieces) {
	static const String OPEN_TAG = "<think>";
	static const String CLOSE_TAG = "</think>";

	String remaining = held + p_delta;
	held = String();

	while (!remaining.is_empty()) {
		const String &tag = inside ? CLOSE_TAG : OPEN_TAG;
		const int tag_pos = remaining.find(tag);
		if (tag_pos >= 0) {
			_emit(remaining.left(tag_pos), inside, r_pieces);
			remaining = remaining.substr(tag_pos + tag.length());
			inside = !inside;
			continue;
		}
		// No whole tag: hold back a tail that could be the start of one.
		const int hold = _partial_tag_suffix_len(remaining, tag);
		_emit(remaining.left(remaining.length() - hold), inside, r_pieces);
		held = remaining.right(hold);
		break;
	}
}

void AIThinkTagSplitter::flush(Vector<AIStreamPiece> &r_pieces) {
	_emit(held, inside, r_pieces);
	held = String();
}

void AIThinkTagSplitter::reset() {
	inside = false;
	held = String();
}
