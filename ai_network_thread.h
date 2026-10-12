#pragma once

// Marks the worker threads on which the assistant talks to the network, so
// that what the engine prints there (a refused connection, a TLS alert) can be
// told apart from errors of the user's project. See AIErrorMonitor.
class AINetworkThread {
	inline static thread_local int depth = 0;

public:
	// Keep one alive for as long as the thread does network work.
	struct Scope {
		Scope() { depth++; }
		~Scope() { depth--; }
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;
	};

	static bool is_current() { return depth > 0; }
};
