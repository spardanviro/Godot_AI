#pragma once

#include "../providers/ai_provider.h"
#include "ai_proxy.h"
#include "ai_stream_protocol.h"

#include "core/os/mutex.h"
#include "core/os/thread.h"

// Runs one streaming chat request on a worker thread and hands the decoded
// deltas to the main thread. Exactly one request can be in flight; the owner
// polls from the main thread until poll() reports the result.
class AIStreamClient {
public:
	struct Request {
		String url;
		Vector<String> headers;
		String body;
		Ref<AIProvider> provider; // Parses the provider-specific event payloads.
		AIProxyConfig proxy; // Set: connect through this HTTP proxy.
	};

	enum Outcome {
		OUTCOME_COMPLETED, // The provider signalled the end of the response.
		OUTCOME_STOPPED, // request_stop() was called; the text is partial.
		OUTCOME_ERROR, // Could not connect, HTTP error, or the connection dropped.
	};

	struct Result {
		Outcome outcome = OUTCOME_COMPLETED;
		String content;
		String thinking;
		String error;
		int response_code = 0;
		// The reply stopped at the output limit: whatever it was writing
		// (a code block, most likely) is incomplete.
		bool truncated = false;
		// The provider withheld (part of) the reply.
		bool blocked = false;
		// Tools the reply asks to have run before it goes on.
		Vector<AIToolCall> tool_calls;
	};

	// Returns false (and does nothing) while a request is in flight.
	bool start(const Request &p_request);
	void request_stop();
	bool is_busy() const { return busy; }

	// Main thread. Moves the deltas received since the last call into r_pieces.
	// Returns true exactly once per request, when it has ended; r_result is
	// then filled in and the client is ready for the next start().
	bool poll(Vector<AIStreamPiece> &r_pieces, Result &r_result);

	~AIStreamClient();

private:
	Thread thread;
	Mutex mutex;
	Request request;
	bool busy = false; // Main thread only.

	// Shared with the worker; guarded by mutex.
	Vector<AIStreamPiece> queue;
	Result result;
	bool finished = false;
	bool stop_requested = false;
	AIToolCallAccumulator tool_calls; // Worker only, until the request ends.

	static void _thread_func(void *p_userdata);
	void _run();
	bool _should_stop();
	void _finish(Outcome p_outcome, const String &p_error = String());
	void _push_pieces(const Vector<AIStreamPiece> &p_pieces);
};
