#pragma once

#ifdef TOOLS_ENABLED

#include "ai_web_fetch.h"
#include "ai_web_search.h"

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "scene/main/node.h"

class Timer;

// Fetches the web search or page a reply asked for. One request at a time;
// when and whether a request may start, and what is made of the result, is
// the caller's decision.
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
	// Starts a search for p_search_query or, if that is empty, a fetch of
	// p_fetch_url. Returns false — after emitting a notice saying why — if the
	// URL is not allowed or the request could not be sent. On true, "finished"
	// follows.
	bool start(const String &p_search_query, const String &p_fetch_url);
	void cancel();
	// What the last request was: a page fetch or a search, and of what.
	bool was_fetch() const { return is_fetch; }
	String get_subject() const { return subject; }

	AIWebRequester();
	~AIWebRequester();
};

#endif // TOOLS_ENABLED
