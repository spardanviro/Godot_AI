#pragma once

#ifdef TOOLS_ENABLED

#include "ai_asset_request.h"

#include "scene/main/node.h"

class HTTPRequest;

// Generates the images and speech the model asked for (with a tool, or with
// a [GENERATE_IMAGE] / [GENERATE_AUDIO] marker where it has no tools) and
// saves them into the project. Requests are queued and served one at a time;
// they run independently of the chat.
class AIAssetService : public Node {
	GDCLASS(AIAssetService, Node);

	HTTPRequest *http_request = nullptr;
	Vector<AIAssetRequest> queue;
	bool request_active = false;
	AIAssetRequest current;

	void _start_next();
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _notify(int p_level, const String &p_text);

protected:
	static void _bind_methods();

public:
	void enqueue(const AIAssetRequest &p_request);
	// Whether enqueue() would take the request. r_reason says why not, in
	// plain English, for the model.
	static bool can_generate(const AIAssetRequest &p_request, String &r_reason);
	// Where the result of a request is saved (speech is always an .mp3).
	static String get_save_path(const AIAssetRequest &p_request);

	AIAssetService();
};

#endif // TOOLS_ENABLED
