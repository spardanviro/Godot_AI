#pragma once

#ifdef TOOLS_ENABLED

#include "ai_response_analysis.h"
#include "ai_web_fetch.h"
#include "ai_web_search.h"

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "scene/main/node.h"

class Timer;

// Fetches the web search or page a reply asked for and turns the result into
// the follow-up message for the model. One request at a time; when and whether
// a request may start is the controller's decision.
class AIWebRequester : public Node {
	GDCLASS(AIWebRequester, Node);

	Ref<AIWebSearch> web_search;
	bool is_fetch = false;
	String subject; // The URL or the search query.

	// The fetch runs on a worker so that neither name resolution nor a slow
	// host can freeze the editor. The worker only touches `url`, `options` and
	// `result`, and the main thread leaves those alone until it has joined it.
	Thread thread;
	Timer *poll_timer = nullptr;
	SafeFlag stop_requested;
	SafeFlag worker_done;
	String url;
	AIWebFetch::Options options;
	AIWebFetch::Result result;

	static void _thread_func(void *p_userdata);
	static bool _should_stop(void *p_userdata);
	void _join_worker();
	void _on_poll();
	void _notify(int p_level, const String &p_text);

protected:
	static void _bind_methods();

public:
	// Starts the search (preferred) or fetch the markers ask for. Returns false
	// — after emitting a notice saying why — if the URL is not allowed or the
	// request could not be sent. On true, "finished" follows.
	bool start(const AIResponseAnalysis::Markers &p_markers);
	void cancel();
	// The message that hands the fetched content back to the model.
	String build_follow_up(const String &p_content) const;

	AIWebRequester();
	~AIWebRequester();
};

#endif // TOOLS_ENABLED
