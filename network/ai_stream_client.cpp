#include "ai_stream_client.h"

#include "../common/ai_log.h"
#include "ai_network_thread.h"

#include "core/io/http_client.h"
#include "core/os/os.h"

static const uint64_t HEADER_TIMEOUT_MS = 180000;
// A stream that stops sending without closing would otherwise hang until Stop.
static const uint64_t IDLE_TIMEOUT_MS = 180000;
static const uint64_t ERROR_BODY_TIMEOUT_MS = 15000;
static const int POLL_INTERVAL_USEC = 10000;
static const int CONNECT_INTERVAL_USEC = 50000;
// The wait between attempts does not see Stop: a server that asks for a longer
// one gets its error shown at once instead.
static const double MAX_RETRY_AFTER_SEC = 10.0;

// One request's way to the server, tried again by AIRetry until the response
// headers are in. Worker only.
struct AIStreamClient::Connection {
	AIStreamClient *owner = nullptr;
	String host;
	String path;
	int port = 80;
	bool use_tls = false;
	int attempts = 0;
	Ref<HTTPClient> client; // After a successful attempt: at the response body.
	Outcome outcome = OUTCOME_ERROR; // How a failed attempt ends the request.
	String error;
};

// Seconds from a Retry-After header in delta-seconds form; -1 for none (or an
// HTTP date, which then gets the usual backoff).
static double _retry_after_seconds(const Ref<HTTPClient> &p_client) {
	List<String> headers;
	p_client->get_response_headers(&headers);
	for (const String &header : headers) {
		if (header.to_lower().begins_with("retry-after:")) {
			const String value = header.substr(12).strip_edges();
			return value.is_valid_float() ? MAX(value.to_float(), 0.0) : -1.0;
		}
	}
	return -1.0;
}

bool AIStreamClient::start(const Request &p_request) {
	if (busy) {
		return false;
	}
	request = p_request;
	{
		MutexLock lock(mutex);
		queue.clear();
		result = Result();
		finished = false;
		stop_requested = false;
	}
	tool_calls.clear();
	thinking_signatures.clear();
	thinking_redacted = false;
	busy = true;
	thread.start(_thread_func, this);
	return true;
}

void AIStreamClient::request_stop() {
	MutexLock lock(mutex);
	stop_requested = true;
}

bool AIStreamClient::poll(Vector<AIStreamPiece> &r_pieces, Result &r_result) {
	if (!busy) {
		return false;
	}
	bool done = false;
	{
		MutexLock lock(mutex);
		r_pieces.append_array(queue);
		queue.clear();
		done = finished;
	}
	if (!done) {
		return false;
	}
	thread.wait_to_finish();
	// The worker is gone, so its state can be read without the lock.
	r_result = result;
	request = Request();
	busy = false;
	return true;
}

AIStreamClient::~AIStreamClient() {
	if (thread.is_started()) {
		// Ask the worker to stop first; otherwise closing the editor would wait
		// for the whole response to finish generating.
		request_stop();
		thread.wait_to_finish();
	}
}

void AIStreamClient::_thread_func(void *p_userdata) {
	AINetworkThread::Scope network_thread;
	static_cast<AIStreamClient *>(p_userdata)->_run();
}

bool AIStreamClient::_should_stop() {
	MutexLock lock(mutex);
	return stop_requested;
}

void AIStreamClient::_finish(Outcome p_outcome, const String &p_error) {
	MutexLock lock(mutex);
	result.outcome = p_outcome;
	result.error = p_error;
	result.tool_calls = tool_calls.get_calls();
	result.thinking_signature = thinking_signatures.size() == 1 && !thinking_redacted ? thinking_signatures[0] : String();
	finished = true;
}

void AIStreamClient::_push_pieces(const Vector<AIStreamPiece> &p_pieces) {
	if (p_pieces.is_empty()) {
		return;
	}
	MutexLock lock(mutex);
	for (int i = 0; i < p_pieces.size(); i++) {
		queue.push_back(p_pieces[i]);
		if (p_pieces[i].is_thinking) {
			result.thinking += p_pieces[i].text;
		} else {
			result.content += p_pieces[i].text;
		}
	}
}

AIRetry::Attempt AIStreamClient::_connect_attempt(void *p_userdata) {
	Connection *connection = static_cast<Connection *>(p_userdata);
	return connection->owner->_connect(*connection);
}

AIRetry::Attempt AIStreamClient::_connect(Connection &r_connection) {
	AIRetry::Attempt attempt;
	r_connection.attempts++;
	if (r_connection.attempts > 1) {
		AI_LOG("Retrying the request (attempt " + itos(r_connection.attempts) + ") after: " + r_connection.error.left(200));
	}
	r_connection.outcome = OUTCOME_ERROR;
	r_connection.error = String();
	r_connection.client.unref();
	if (_should_stop()) { // Stop during the wait before this attempt.
		r_connection.outcome = OUTCOME_STOPPED;
		return attempt;
	}

	Ref<HTTPClient> client = Ref<HTTPClient>(HTTPClient::create());
	if (client.is_null()) {
		r_connection.error = "Failed to create HTTPClient.";
		return attempt;
	}
	r_connection.client = client;

	if (request.proxy.is_set()) {
		client->set_http_proxy(request.proxy.host, request.proxy.port);
		client->set_https_proxy(request.proxy.host, request.proxy.port);
	}
	Ref<TLSOptions> tls_opts;
	if (r_connection.use_tls) {
		tls_opts = TLSOptions::client();
	}
	if (client->connect_to_host(r_connection.host, r_connection.port, tls_opts) != OK) {
		r_connection.error = "Failed to connect to " + r_connection.host;
		return attempt;
	}

	while (client->get_status() == HTTPClient::STATUS_CONNECTING || client->get_status() == HTTPClient::STATUS_RESOLVING) {
		if (_should_stop()) {
			r_connection.outcome = OUTCOME_STOPPED;
			return attempt;
		}
		client->poll();
		OS::get_singleton()->delay_usec(CONNECT_INTERVAL_USEC);
	}
	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		// Not tried again: a host that cannot be reached at all rarely can a
		// second later, and each attempt may wait out the whole connect timeout.
		r_connection.error = "Connection failed" + (request.proxy.is_set() ? " (through the proxy " + request.proxy.to_text() + ")" : String()) + ". Status: " + itos((int)client->get_status());
		return attempt;
	}

	const CharString body_utf8 = request.body.utf8();
	if (client->request(HTTPClient::METHOD_POST, r_connection.path, request.headers, (const uint8_t *)body_utf8.get_data(), body_utf8.length()) != OK) {
		r_connection.error = "Failed to send HTTP request.";
		return attempt;
	}

	// Waiting for response headers: honour Stop, and give up on a server that
	// accepts the connection but never answers instead of hanging forever.
	const uint64_t request_started_ms = OS::get_singleton()->get_ticks_msec();
	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		if (_should_stop()) {
			r_connection.outcome = OUTCOME_STOPPED;
			return attempt;
		}
		if (OS::get_singleton()->get_ticks_msec() - request_started_ms > HEADER_TIMEOUT_MS) {
			r_connection.error = "Timed out waiting for the server to respond.";
			return attempt;
		}
		client->poll();
		OS::get_singleton()->delay_usec(CONNECT_INTERVAL_USEC);
	}

	if (!client->has_response()) {
		// The connection dropped before the answer: nothing of the reply is lost.
		r_connection.error = "No response from server.";
		attempt.retryable = true;
		return attempt;
	}

	const int response_code = client->get_response_code();
	{
		MutexLock lock(mutex);
		result.response_code = response_code;
	}

	if (response_code != 200) {
		PackedByteArray error_bytes;
		const uint64_t error_body_started_ms = OS::get_singleton()->get_ticks_msec();
		while (client->get_status() == HTTPClient::STATUS_BODY) {
			if (_should_stop()) {
				r_connection.outcome = OUTCOME_STOPPED;
				return attempt;
			}
			if (OS::get_singleton()->get_ticks_msec() - error_body_started_ms > ERROR_BODY_TIMEOUT_MS) {
				break; // The status code alone will have to do.
			}
			client->poll();
			if (client->get_status() != HTTPClient::STATUS_BODY) {
				break;
			}
			error_bytes.append_array(client->read_response_body_chunk());
			OS::get_singleton()->delay_usec(POLL_INTERVAL_USEC);
		}
		const String error_body = String::utf8((const char *)error_bytes.ptr(), error_bytes.size());
		r_connection.error = "API Error (HTTP " + itos(response_code) + "): " + error_body.left(500);
		// Whether the code is worth another try (429, 5xx, 529) is llm-retry's call.
		attempt.http_code = response_code;
		attempt.retry_after_seconds = _retry_after_seconds(client);
		attempt.retryable = attempt.retry_after_seconds <= MAX_RETRY_AFTER_SEC;
		return attempt;
	}

	attempt.ok = true;
	return attempt;
}

void AIStreamClient::_run() {
	Connection connection;
	connection.owner = this;
	String url = request.url;
	if (url.begins_with("https://")) {
		connection.use_tls = true;
		connection.port = 443;
		url = url.substr(8);
	} else if (url.begins_with("http://")) {
		url = url.substr(7);
	}

	connection.host = url;
	connection.path = "/";
	const int slash_pos = url.find("/");
	if (slash_pos >= 0) {
		connection.host = url.substr(0, slash_pos);
		connection.path = url.substr(slash_pos);
	}
	const int colon_pos = connection.host.find(":");
	if (colon_pos >= 0) {
		connection.port = connection.host.substr(colon_pos + 1).to_int();
		connection.host = connection.host.substr(0, colon_pos);
	}

	// Only this part is tried again: once the body is read, deltas have been
	// shown, and a second attempt would repeat them.
	if (!AIRetry::run(_connect_attempt, &connection).ok) {
		_finish(connection.outcome, connection.error);
		return;
	}
	Ref<HTTPClient> client = connection.client;

	AISSEDecoder decoder;
	AIThinkTagSplitter splitter;
	bool done = false;
	bool stopped = false;
	bool stalled = false;
	String stream_error; // An error the provider reported inside the stream.
	uint64_t last_data_ms = OS::get_singleton()->get_ticks_msec();

	// Turns event payloads into pieces; sets `done` at the provider's end marker.
	auto handle_payloads = [&](const Vector<String> &p_payloads) {
		Vector<AIStreamPiece> pieces;
		for (int i = 0; i < p_payloads.size() && !done; i++) {
			if (request.provider->is_stream_done_marker(p_payloads[i])) {
				done = true;
				break;
			}
			const Dictionary delta = request.provider->parse_stream_delta_ex(p_payloads[i]);
			if (delta.has("error")) {
				stream_error = delta["error"];
				done = true;
				break;
			}
			if (delta.has("finish")) {
				MutexLock lock(mutex);
				result.truncated = result.truncated || String(delta["finish"]) == AIProvider::FINISH_LENGTH;
				result.blocked = result.blocked || String(delta["finish"]) == AIProvider::FINISH_BLOCKED;
			}
			if (delta.has("tool_calls")) {
				const Array fragments = delta["tool_calls"];
				for (int j = 0; j < fragments.size(); j++) {
					const Dictionary fragment = fragments[j];
					if (bool(fragment.get("whole", false))) {
						tool_calls.add_whole(fragment.get("id", String()), fragment.get("name", String()), fragment.get("arguments", String()), fragment.get("provider_data", String()));
					} else {
						tool_calls.add(fragment.get("index", j), fragment.get("id", String()), fragment.get("name", String()), fragment.get("arguments", String()));
					}
				}
			}
			if (delta.has("signature")) {
				thinking_signatures.push_back(delta["signature"]);
			}
			thinking_redacted = thinking_redacted || bool(delta.get("redacted_thinking", false));
			const String thinking_delta = delta.get("thinking", String());
			const String content_delta = delta.get("content", String());
			if (!thinking_delta.is_empty()) {
				AIStreamPiece piece;
				piece.text = thinking_delta;
				piece.is_thinking = true;
				pieces.push_back(piece);
			}
			if (!content_delta.is_empty()) {
				const int before = pieces.size();
				splitter.push(content_delta, pieces);
				for (int p = before; p < pieces.size(); p++) {
					// Thinking split out of the text: the signature does not cover it.
					thinking_redacted = thinking_redacted || pieces[p].is_thinking;
				}
			}
		}
		_push_pieces(pieces);
	};

	while (client->get_status() == HTTPClient::STATUS_BODY && !done) {
		if (_should_stop()) {
			stopped = true;
			break;
		}

		client->poll();
		if (client->get_status() != HTTPClient::STATUS_BODY) {
			break; // The connection closed; reading now would only log an error.
		}
		const PackedByteArray chunk = client->read_response_body_chunk();
		if (chunk.size() > 0) {
			last_data_ms = OS::get_singleton()->get_ticks_msec();
			Vector<String> payloads;
			decoder.push_bytes(chunk.ptr(), chunk.size(), payloads);
			handle_payloads(payloads);
		} else if (OS::get_singleton()->get_ticks_msec() - last_data_ms > IDLE_TIMEOUT_MS) {
			stalled = true;
			break;
		}

		OS::get_singleton()->delay_usec(POLL_INTERVAL_USEC);
	}

	if (!stopped && !stalled && !done) {
		// The server closed the stream: a last event without the terminating
		// blank line still counts.
		Vector<String> payloads;
		const String last_payload = decoder.flush();
		if (!last_payload.is_empty()) {
			payloads.push_back(last_payload);
		}
		handle_payloads(payloads);
	}
	{
		Vector<AIStreamPiece> tail;
		splitter.flush(tail);
		_push_pieces(tail);
	}
	if (stalled) {
		_finish(OUTCOME_ERROR, "The server stopped sending data before the response finished.");
		return;
	}
	if (!stream_error.is_empty() && !stopped) {
		// What arrived before it is a fragment, not a reply.
		_finish(OUTCOME_ERROR, "API Error: " + stream_error.left(500));
		return;
	}

	if (stopped) {
		_finish(OUTCOME_STOPPED);
		return;
	}
	// A dropped connection is not a finished response: the text so far is
	// truncated and its last code block must not be executed.
	const HTTPClient::Status end_status = client->get_status();
	if (!done && (end_status == HTTPClient::STATUS_CONNECTION_ERROR || end_status == HTTPClient::STATUS_TLS_HANDSHAKE_ERROR)) {
		_finish(OUTCOME_ERROR, "Connection lost before the response finished.");
		return;
	}
	_finish(OUTCOME_COMPLETED);
}
