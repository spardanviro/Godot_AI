#include "ai_stream_client.h"

#include "ai_network_thread.h"

#include "core/io/http_client.h"
#include "core/os/os.h"

static const uint64_t HEADER_TIMEOUT_MS = 180000;
// A stream that stops sending without closing would otherwise hang until Stop.
static const uint64_t IDLE_TIMEOUT_MS = 180000;
static const uint64_t ERROR_BODY_TIMEOUT_MS = 15000;
static const int POLL_INTERVAL_USEC = 10000;
static const int CONNECT_INTERVAL_USEC = 50000;

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

void AIStreamClient::_run() {
	String url = request.url;
	bool use_tls = false;
	int port = 80;
	if (url.begins_with("https://")) {
		use_tls = true;
		port = 443;
		url = url.substr(8);
	} else if (url.begins_with("http://")) {
		url = url.substr(7);
	}

	String host = url;
	String path = "/";
	const int slash_pos = url.find("/");
	if (slash_pos >= 0) {
		host = url.substr(0, slash_pos);
		path = url.substr(slash_pos);
	}
	const int colon_pos = host.find(":");
	if (colon_pos >= 0) {
		port = host.substr(colon_pos + 1).to_int();
		host = host.substr(0, colon_pos);
	}

	Ref<HTTPClient> client = Ref<HTTPClient>(HTTPClient::create());
	if (client.is_null()) {
		_finish(OUTCOME_ERROR, "Failed to create HTTPClient.");
		return;
	}

	if (request.proxy.is_set()) {
		client->set_http_proxy(request.proxy.host, request.proxy.port);
		client->set_https_proxy(request.proxy.host, request.proxy.port);
	}
	Ref<TLSOptions> tls_opts;
	if (use_tls) {
		tls_opts = TLSOptions::client();
	}
	if (client->connect_to_host(host, port, tls_opts) != OK) {
		_finish(OUTCOME_ERROR, "Failed to connect to " + host);
		return;
	}

	while (client->get_status() == HTTPClient::STATUS_CONNECTING || client->get_status() == HTTPClient::STATUS_RESOLVING) {
		if (_should_stop()) {
			_finish(OUTCOME_STOPPED);
			return;
		}
		client->poll();
		OS::get_singleton()->delay_usec(CONNECT_INTERVAL_USEC);
	}
	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		_finish(OUTCOME_ERROR, "Connection failed" + (request.proxy.is_set() ? " (through the proxy " + request.proxy.to_text() + ")" : String()) + ". Status: " + itos((int)client->get_status()));
		return;
	}

	const CharString body_utf8 = request.body.utf8();
	if (client->request(HTTPClient::METHOD_POST, path, request.headers, (const uint8_t *)body_utf8.get_data(), body_utf8.length()) != OK) {
		_finish(OUTCOME_ERROR, "Failed to send HTTP request.");
		return;
	}

	// Waiting for response headers: honour Stop, and give up on a server that
	// accepts the connection but never answers instead of hanging forever.
	const uint64_t request_started_ms = OS::get_singleton()->get_ticks_msec();
	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		if (_should_stop()) {
			_finish(OUTCOME_STOPPED);
			return;
		}
		if (OS::get_singleton()->get_ticks_msec() - request_started_ms > HEADER_TIMEOUT_MS) {
			_finish(OUTCOME_ERROR, "Timed out waiting for the server to respond.");
			return;
		}
		client->poll();
		OS::get_singleton()->delay_usec(CONNECT_INTERVAL_USEC);
	}

	if (!client->has_response()) {
		_finish(OUTCOME_ERROR, "No response from server.");
		return;
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
				_finish(OUTCOME_STOPPED);
				return;
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
		_finish(OUTCOME_ERROR, "API Error (HTTP " + itos(response_code) + "): " + error_body.left(500));
		return;
	}

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
			const String thinking_delta = delta.get("thinking", String());
			const String content_delta = delta.get("content", String());
			if (!thinking_delta.is_empty()) {
				AIStreamPiece piece;
				piece.text = thinking_delta;
				piece.is_thinking = true;
				pieces.push_back(piece);
			}
			if (!content_delta.is_empty()) {
				splitter.push(content_delta, pieces);
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
