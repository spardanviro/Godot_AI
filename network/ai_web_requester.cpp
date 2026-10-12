#ifdef TOOLS_ENABLED

#include "ai_web_requester.h"
#include "../common/ai_notice.h"
#include "ai_url_policy.h"

#include "../common/ai_log.h"
#include "ai_network_thread.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/timer.h"

static const double POLL_INTERVAL_SEC = 0.05;
static const int TEXT_LIMIT_CHARS = 8000;
static const int LOGGED_URL_CHARS = 120;

// For the log: without the query, which is where tokens travel.
static String _loggable(const String &p_url) {
	return p_url.get_slice("?", 0).left(LOGGED_URL_CHARS);
}

void AIWebRequester::_bind_methods() {
	// content is empty when the request failed or returned nothing usable.
	ADD_SIGNAL(MethodInfo("finished", PropertyInfo(Variant::STRING, "content")));
	ADD_SIGNAL(MethodInfo("notice", PropertyInfo(Variant::INT, "level"), PropertyInfo(Variant::STRING, "text")));
}

void AIWebRequester::_notify(int p_level, const String &p_text) {
	emit_signal(SNAME("notice"), p_level, p_text);
}

void AIWebRequester::_thread_func(void *p_userdata) {
	AINetworkThread::Scope network_thread;
	AIWebRequester *self = static_cast<AIWebRequester *>(p_userdata);
	self->result = AIWebFetch::get(self->url, self->options);
	self->worker_done.set();
}

bool AIWebRequester::_should_stop(void *p_userdata) {
	return static_cast<AIWebRequester *>(p_userdata)->stop_requested.is_set();
}

// The worker checks stop_requested every few milliseconds, so this is short.
void AIWebRequester::_join_worker() {
	if (thread.is_started()) {
		stop_requested.set();
		thread.wait_to_finish();
	}
	poll_timer->stop();
}

bool AIWebRequester::start(const String &p_search_query, const String &p_fetch_url) {
	_join_worker();
	is_fetch = p_search_query.is_empty();
	subject = is_fetch ? p_fetch_url : p_search_query;
	last_error = String();

	options = AIWebFetch::Options();
	options.should_stop = &AIWebRequester::_should_stop;
	options.should_stop_userdata = this;
	options.headers.push_back("User-Agent: GodotAI/1.0");
	options.proxy = AIProxy::get_configured();
	if (is_fetch) {
		url = web_search->sanitize_url(subject);
		options.headers.push_back("Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
	} else {
		url = web_search->build_search_url(subject);
	}

	// What can be refused without asking the network is refused right away;
	// the worker checks again, along with what the host resolves to and every
	// redirect on the way.
	String reason;
	if (!AIUrlPolicy::is_public_web_url(url, reason)) {
		AI_WARN("Web request rejected (" + reason + "): " + _loggable(url));
		last_error = "the address is not allowed (" + reason + ")";
		_notify(AI_NOTICE_WARNING, AI_L("Web request skipped: ", "已跳过网络请求：") + reason + ".");
		return false;
	}
	if (is_fetch) {
		_notify(AI_NOTICE_INFO, String(U"🌐 ") + AI_L("Fetching: ", "正在抓取：") + url + "...");
	} else {
		_notify(AI_NOTICE_INFO, String(U"🔍 ") + AI_L("Searching: ", "正在搜索：") + subject + "...");
	}

	result = AIWebFetch::Result();
	stop_requested.clear();
	worker_done.clear();
	thread.start(_thread_func, this);
	poll_timer->start(POLL_INTERVAL_SEC);
	return true;
}

void AIWebRequester::cancel() {
	_join_worker();
}

void AIWebRequester::_on_poll() {
	if (!worker_done.is_set()) {
		return;
	}
	_join_worker();

	String content;
	if (!result.ok) {
		AI_WARN("Web request failed (" + result.error + "): " + _loggable(url));
		last_error = result.error;
		_notify(AI_NOTICE_WARNING, AI_L("Web request failed: ", "网络请求失败：") + result.error + ".");
	} else if (result.response_code != 200) {
		AI_ERR("Web request failed: HTTP " + itos(result.response_code));
		last_error = "the server answered HTTP " + itos(result.response_code);
		_notify(AI_NOTICE_WARNING, AI_L("Web request failed (HTTP ", "网络请求失败 (HTTP ") + itos(result.response_code) + ").");
	} else {
		const String body = String::utf8((const char *)result.body.ptr(), result.body.size());
		if (!is_fetch) {
			content = web_search->parse_search_results(body);
		} else if (body.strip_edges().begins_with("<") || body.find("<html") >= 0) {
			content = web_search->extract_text_from_html(body);
		} else if (body.length() > TEXT_LIMIT_CHARS) {
			content = body.left(TEXT_LIMIT_CHARS) + "\n\n[Content truncated at " + itos(TEXT_LIMIT_CHARS) + " characters]";
		} else {
			content = body;
		}
		if (content.is_empty()) {
			last_error = "nothing useful was found in the response";
			_notify(AI_NOTICE_WARNING, AI_L("No useful content found.", "未找到有用内容。"));
		}
	}
	result = AIWebFetch::Result();
	emit_signal(SNAME("finished"), content);
}

AIWebRequester::AIWebRequester() {
	web_search.instantiate();
	poll_timer = memnew(Timer);
	poll_timer->set_one_shot(false);
	poll_timer->connect("timeout", callable_mp(this, &AIWebRequester::_on_poll));
	add_child(poll_timer);
}

AIWebRequester::~AIWebRequester() {
	if (thread.is_started()) {
		stop_requested.set();
		thread.wait_to_finish();
	}
}

#endif // TOOLS_ENABLED
