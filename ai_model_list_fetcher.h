#pragma once

#ifdef TOOLS_ENABLED

#include "ai_provider.h"

#include "scene/main/node.h"

class HTTPRequest;

// Asks a provider's API which models the configured key can use. One request
// at a time: a new fetch replaces the one in flight. Reports through
// fetched(models, best) or failed(message).
class AIModelListFetcher : public Node {
	GDCLASS(AIModelListFetcher, Node);

	HTTPRequest *http_request = nullptr;
	Ref<AIProvider> provider; // Parses the answer of the request in flight.

	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

protected:
	static void _bind_methods();

public:
	// p_provider carries the key and endpoint to use. Returns false if the
	// provider has no model list or the request could not be sent; no signal
	// follows in that case.
	bool fetch(const Ref<AIProvider> &p_provider);
	void cancel();

	AIModelListFetcher();
};

#endif // TOOLS_ENABLED
