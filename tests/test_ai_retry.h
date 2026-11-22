#pragma once

#include "../network/ai_retry.h"

#include "core/templates/vector.h"
#include "tests/test_macros.h"

namespace TestAIRetry {

// Plays back one scripted attempt per call; the last one repeats.
struct Script {
	Vector<AIRetry::Attempt> attempts;
	int calls = 0;
};

static AIRetry::Attempt _play(void *p_userdata) {
	Script *script = static_cast<Script *>(p_userdata);
	const int index = MIN(script->calls, script->attempts.size() - 1);
	script->calls++;
	return script->attempts[index];
}

static AIRetry::Attempt _failed(int p_http_code, bool p_retryable = true) {
	AIRetry::Attempt attempt;
	attempt.http_code = p_http_code;
	attempt.retryable = p_retryable;
	return attempt;
}

static AIRetry::Attempt _succeeded() {
	AIRetry::Attempt attempt;
	attempt.ok = true;
	return attempt;
}

static AIRetry::Policy _fast_policy() {
	AIRetry::Policy policy;
	policy.base_delay_ms = 1.0;
	policy.max_delay_ms = 1.0;
	return policy;
}

TEST_CASE("[AIAssistant][Retry] An overloaded server and a dropped connection are tried again") {
	Script script;
	script.attempts.push_back(_failed(529));
	script.attempts.push_back(_failed(0));
	script.attempts.push_back(_succeeded());

	const AIRetry::Attempt last = AIRetry::run(_play, &script, _fast_policy());

	CHECK(last.ok);
	CHECK(script.calls == 3);
}

TEST_CASE("[AIAssistant][Retry] A request the server refuses is not sent again") {
	Script script;
	script.attempts.push_back(_failed(401));

	const AIRetry::Attempt last = AIRetry::run(_play, &script, _fast_policy());

	CHECK_FALSE(last.ok);
	CHECK(last.http_code == 401);
	CHECK(script.calls == 1);
}

TEST_CASE("[AIAssistant][Retry] A final failure is not retried, whatever its code") {
	Script script;
	script.attempts.push_back(_failed(503, false));

	const AIRetry::Attempt last = AIRetry::run(_play, &script, _fast_policy());

	CHECK_FALSE(last.ok);
	CHECK(script.calls == 1);
}

TEST_CASE("[AIAssistant][Retry] The last failure is returned once the attempts are spent") {
	Script script;
	AIRetry::Attempt rate_limited = _failed(429);
	rate_limited.retry_after_seconds = 0.0;
	script.attempts.push_back(rate_limited);

	const AIRetry::Attempt last = AIRetry::run(_play, &script, _fast_policy());

	CHECK_FALSE(last.ok);
	CHECK(last.http_code == 429);
	CHECK(script.calls == 3);
}

} // namespace TestAIRetry
