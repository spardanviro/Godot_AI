#pragma once

#ifdef TOOLS_ENABLED

#include "core/os/thread.h"
#include "scene/main/node.h"

class HTTPRequest;

// Keeps the model facts (AIModelSpecs) current: when the downloaded copy is
// missing or older than AIModelSpecs::MAX_AGE_DAYS, fetches models.dev's data
// once, keeps the part the editor uses and stores it. Emits "updated" when
// newer facts are in use. A failed download changes nothing: the copy there
// is, or the built-in one, stays in use, and the next start tries again.
class AIModelSpecsUpdater : public Node {
	GDCLASS(AIModelSpecsUpdater, Node);

	HTTPRequest *http_request = nullptr;
	bool in_flight = false;

	// The download is several megabytes of JSON; it is cut down on a worker
	// so the editor does not stall. The worker reads `download` and writes
	// `kept`; the main thread touches neither until it has joined it.
	Thread worker;
	String download;
	String kept;

	static void _filter_on_worker(void *p_userdata);
	void _on_filtered();
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

protected:
	static void _bind_methods();

public:
	// The editor setting that allows the download (on by default).
	static constexpr const char *SETTING = "auto_update_model_specs";

	// Starts the download if the copy is stale. Returns whether it started.
	bool update_if_stale();

	AIModelSpecsUpdater();
	~AIModelSpecsUpdater();
};

#endif // TOOLS_ENABLED
