#pragma once

#ifdef TOOLS_ENABLED

#include "../providers/ai_provider.h"
#include "ai_session.h"

#include "scene/main/node.h"

class HTTPRequest;

// Optional refinement of history compression: asks the model for a proper
// summary of the messages that were just folded away and, when it arrives,
// swaps it in for the heuristic block that stood in for them. It runs beside
// the conversation and only ever rewrites that one block, so a late, failed or
// cancelled reply simply leaves the heuristic summary in place.
class AIHistorySummarizer : public Node {
	GDCLASS(AIHistorySummarizer, Node);

	HTTPRequest *http_request = nullptr;
	AISession *session = nullptr;
	Ref<AIProvider> provider; // Must parse the reply it requested.
	String block;
	bool active = false;

	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

protected:
	static void _bind_methods() {}

public:
	void set_session(AISession *p_session) { session = p_session; }
	// Ignored while a summary is already in flight.
	void request(const Ref<AIProvider> &p_provider, const Array &p_messages, const String &p_block);
	// Drops an in-flight summary so it cannot land in a different conversation.
	void cancel();

	AIHistorySummarizer();
};

#endif // TOOLS_ENABLED
