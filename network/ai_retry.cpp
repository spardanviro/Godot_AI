#include "ai_retry.h"

// This file alone is built with exceptions (see SCsub): llm-retry learns that
// an attempt failed from what it throws. Nothing is thrown past AIRetry::run().
#define LLM_RETRY_IMPLEMENTATION
#include <llm_retry.hpp>

AIRetry::Attempt AIRetry::run(AttemptFunc p_attempt, void *p_userdata, const Policy &p_policy) {
	llm::RetryConfig config;
	config.max_attempts = p_policy.max_attempts;
	config.base_delay_ms = p_policy.base_delay_ms;
	config.max_delay_ms = p_policy.max_delay_ms;

	Attempt last;
	try {
		llm::with_retry<bool>([&]() {
			last = p_attempt(p_userdata);
			if (!last.ok && last.retryable) {
				// llm-retry decides from the code whether another try is worth it.
				throw llm::LLMError{ last.http_code, std::string(), true, last.retry_after_seconds };
			}
			return last.ok;
		},
				config);
	} catch (const llm::LLMError &) {
		// The attempts are spent, or the code is not one to retry: `last` says why.
	}
	return last;
}
