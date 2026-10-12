#ifdef TOOLS_ENABLED

#include "ai_model_list_fetcher.h"

#include "ai_log.h"
#include "ai_proxy.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/http_request.h"

static const double REQUEST_TIMEOUT_SEC = 15.0;
static const int BODY_LIMIT_BYTES = 4 * 1024 * 1024;

void AIModelListFetcher::_bind_methods() {
	// best: the model the provider recommends out of the list.
	ADD_SIGNAL(MethodInfo("fetched", PropertyInfo(Variant::PACKED_STRING_ARRAY, "models"), PropertyInfo(Variant::STRING, "best")));
	ADD_SIGNAL(MethodInfo("failed", PropertyInfo(Variant::STRING, "message")));
}

bool AIModelListFetcher::fetch(const Ref<AIProvider> &p_provider) {
	cancel();
	ERR_FAIL_COND_V(p_provider.is_null(), false);

	const String url = p_provider->get_models_list_url();
	if (url.is_empty()) {
		return false;
	}
	AIProxy::apply(http_request, url);
	if (http_request->request(url, p_provider->get_models_list_headers(), HTTPClient::METHOD_GET) != OK) {
		return false;
	}
	provider = p_provider;
	return true;
}

void AIModelListFetcher::cancel() {
	if (provider.is_valid()) {
		http_request->cancel_request();
		provider.unref();
	}
}

void AIModelListFetcher::_on_request_completed(int p_result, int p_response_code, const PackedStringArray & /*p_headers*/, const PackedByteArray &p_body) {
	const Ref<AIProvider> answered = provider;
	provider.unref();
	if (answered.is_null()) {
		return; // Cancelled.
	}

	if (p_result != HTTPRequest::RESULT_SUCCESS) {
		emit_signal(SNAME("failed"), AI_L("Could not reach the provider.", "无法连接到服务商。"));
		return;
	}
	if (p_response_code == 401) {
		emit_signal(SNAME("failed"), AI_L("The provider rejected the API key (HTTP 401).", "服务商拒绝了该 API 密钥 (HTTP 401)。"));
		return;
	}
	if (p_response_code == 403) {
		emit_signal(SNAME("failed"), AI_L("This key may not list models (HTTP 403).", "该密钥无权获取模型列表 (HTTP 403)。"));
		return;
	}
	if (p_response_code != 200) {
		emit_signal(SNAME("failed"), AI_L("Could not fetch the model list (HTTP ", "获取模型列表失败 (HTTP ") + itos(p_response_code) + ").");
		return;
	}

	const PackedStringArray models = answered->parse_models_list(String::utf8((const char *)p_body.ptr(), p_body.size()));
	if (models.is_empty()) {
		emit_signal(SNAME("failed"), TR(STR_FETCH_DEFAULT));
		return;
	}
	emit_signal(SNAME("fetched"), models, answered->select_best_model(models));
}

AIModelListFetcher::AIModelListFetcher() {
	http_request = memnew(HTTPRequest);
	http_request->set_use_threads(true);
	http_request->set_timeout(REQUEST_TIMEOUT_SEC);
	http_request->set_body_size_limit(BODY_LIMIT_BYTES);
	// The request carries the API key: it goes to the configured endpoint
	// and nowhere that endpoint points to.
	http_request->set_max_redirects(0);
	http_request->connect("request_completed", callable_mp(this, &AIModelListFetcher::_on_request_completed));
	add_child(http_request);
}

#endif // TOOLS_ENABLED
