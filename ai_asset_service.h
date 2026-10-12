#pragma once

#ifdef TOOLS_ENABLED

#include "ai_response_analysis.h"

#include "scene/main/node.h"

class HTTPRequest;

// Generates images and speech the model asked for via [GENERATE_IMAGE] /
// [GENERATE_AUDIO] markers and saves them into the project. Requests are
// queued and served one at a time; they run independently of the chat.
class AIAssetService : public Node {
	GDCLASS(AIAssetService, Node);

	HTTPRequest *http_request = nullptr;
	Vector<AIResponseAnalysis::AssetRequest> queue;
	bool request_active = false;
	AIResponseAnalysis::AssetRequest current;

	void _start_next();
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _notify(int p_level, const String &p_text);

protected:
	static void _bind_methods();

public:
	void enqueue(const AIResponseAnalysis::AssetRequest &p_request);

	AIAssetService();
};

#endif // TOOLS_ENABLED
