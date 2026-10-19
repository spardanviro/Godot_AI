#ifdef TOOLS_ENABLED

#include "ai_model_specs_updater.h"

#include "../common/ai_log.h"
#include "../common/ai_settings.h"
#include "../providers/ai_model_specs.h"
#include "ai_proxy.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/http_request.h"

static const double REQUEST_TIMEOUT_SEC = 60.0;
// models.dev's api.json is about 6 MB and grows with every model listed.
static const int BODY_LIMIT_BYTES = 24 * 1024 * 1024;

void AIModelSpecsUpdater::_bind_methods() {
	ADD_SIGNAL(MethodInfo("updated"));
}

bool AIModelSpecsUpdater::update_if_stale() {
	if (in_flight || !bool(AISettings::get(SETTING, true)) || AIModelSpecs::get_cache_path().is_empty() || !AIModelSpecs::is_cache_stale()) {
		return false;
	}
	const String url = AIModelSpecs::SOURCE_URL;
	AIProxy::apply(http_request, url);
	if (http_request->request(url, Vector<String>(), HTTPClient::METHOD_GET) != OK) {
		return false;
	}
	in_flight = true;
	return true;
}

void AIModelSpecsUpdater::_on_request_completed(int p_result, int p_response_code, const PackedStringArray & /*p_headers*/, const PackedByteArray &p_body) {
	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		in_flight = false;
		AI_LOG(vformat("Model facts not updated: %s answered %d (result %d). The copy in use stays.", AIModelSpecs::SOURCE_URL, p_response_code, p_result));
		return;
	}
	download = String::utf8((const char *)p_body.ptr(), p_body.size());
	kept = String();
	worker.start(_filter_on_worker, this);
}

void AIModelSpecsUpdater::_filter_on_worker(void *p_userdata) {
	AIModelSpecsUpdater *self = static_cast<AIModelSpecsUpdater *>(p_userdata);
	self->kept = AIModelSpecs::filter(self->download);
	callable_mp(self, &AIModelSpecsUpdater::_on_filtered).call_deferred();
}

void AIModelSpecsUpdater::_on_filtered() {
	if (worker.is_started()) {
		worker.wait_to_finish();
	}
	in_flight = false;
	download = String();
	const String result = kept;
	kept = String();
	if (result.is_empty() || !AIModelSpecs::store_cache(result)) {
		AI_LOG("Model facts not updated: the download was not in the expected form, or could not be stored.");
		return;
	}
	AI_LOG("Model facts updated from " + String(AIModelSpecs::SOURCE_URL) + ".");
	emit_signal(SNAME("updated"));
}

AIModelSpecsUpdater::AIModelSpecsUpdater() {
	http_request = memnew(HTTPRequest);
	http_request->set_use_threads(true);
	http_request->set_timeout(REQUEST_TIMEOUT_SEC);
	http_request->set_body_size_limit(BODY_LIMIT_BYTES);
	// No redirects: one to plain http would let anyone on the way rewrite the facts.
	http_request->set_max_redirects(0);
	http_request->connect("request_completed", callable_mp(this, &AIModelSpecsUpdater::_on_request_completed));
	add_child(http_request);
}

AIModelSpecsUpdater::~AIModelSpecsUpdater() {
	// A deferred _on_filtered() for a freed updater is dropped; the worker is not.
	if (worker.is_started()) {
		worker.wait_to_finish();
	}
}

#endif // TOOLS_ENABLED
