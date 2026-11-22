#pragma once

// Makes an attempt again after a transient failure: a connection dropped
// before the answer, or HTTP 408, 429, 5xx or 529.
// The backoff (exponential, with jitter, honouring Retry-After) is llm-retry's,
// in thirdparty/llm-retry. Sleeps between attempts, so call it on a worker.
class AIRetry {
public:
	struct Attempt {
		bool ok = false;
		// The HTTP status of a failed attempt; 0 if there was none (no connection).
		int http_code = 0;
		// False: the failure is final whatever its code (Stop, a bad URL, a TLS error).
		bool retryable = false;
		// From the response's Retry-After header; negative if it had none.
		double retry_after_seconds = -1.0;
	};

	struct Policy {
		int max_attempts = 3;
		double base_delay_ms = 1000.0; // Doubled after each failed attempt.
		double max_delay_ms = 8000.0;
	};

	typedef Attempt (*AttemptFunc)(void *p_userdata);

	// Calls p_attempt until it succeeds, fails for good or the attempts are
	// spent. Returns the last attempt.
	static Attempt run(AttemptFunc p_attempt, void *p_userdata, const Policy &p_policy = Policy());
};
