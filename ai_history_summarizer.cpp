#ifdef TOOLS_ENABLED

#include "ai_history_summarizer.h"

#include "ai_proxy.h"

#include "ai_knowledge.h"
#include "ai_log.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "scene/main/http_request.h"

static const double REQUEST_TIMEOUT_SEC = 60.0;
static const int SUMMARY_MAX_TOKENS = 1024; // A summary needs far less than a normal reply.
static const int MESSAGE_LIMIT_CHARS = 800;

// Caps the output-token field of a request body, whichever name the provider uses.
static String _limit_output_tokens(const String &p_body) {
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(p_body) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return p_body;
	}
	Dictionary data = json->get_data();
	if (data.has("max_tokens")) {
		data["max_tokens"] = SUMMARY_MAX_TOKENS;
	}
	if (data.has("max_completion_tokens")) {
		data["max_completion_tokens"] = SUMMARY_MAX_TOKENS;
	}
	if (data.has("generationConfig")) {
		Dictionary generation_config = data["generationConfig"];
		generation_config["maxOutputTokens"] = SUMMARY_MAX_TOKENS;
		data["generationConfig"] = generation_config;
	}
	return JSON::stringify(data);
}

void AIHistorySummarizer::request(const Ref<AIProvider> &p_provider, const Array &p_messages, const String &p_block) {
	if (active || p_provider.is_null() || !session) {
		return;
	}

	String history;
	for (int i = 0; i < p_messages.size(); i++) {
		const Dictionary message = p_messages[i];
		const String content = message.get("content", "");
		history += String(String(message.get("role", "")) == "user" ? "User" : "AI") + ": " +
				(content.length() > MESSAGE_LIMIT_CHARS ? content.left(MESSAGE_LIMIT_CHARS) + "..." : content) + "\n---\n";
	}

	const AIKnowledge *knowledge = AIKnowledge::get_singleton();
	Dictionary values;
	values["history"] = history;
	const String body = _limit_output_tokens(p_provider->build_request_body(
			knowledge->get_prompt("summary_system"), Array(), knowledge->format_prompt("summary_user", values)));

	AIProxy::apply(http_request, p_provider->get_request_url());
	if (http_request->request(p_provider->get_request_url(), p_provider->get_headers(), HTTPClient::METHOD_POST, body) != OK) {
		AI_WARN("Failed to start AI summary request — heuristic summary will be kept.");
		return;
	}
	active = true;
	provider = p_provider;
	block = p_block;
	AI_LOG("AI summary requested for " + itos(p_messages.size()) + " messages.");
}

void AIHistorySummarizer::cancel() {
	if (active) {
		http_request->cancel_request();
	}
	active = false;
	provider.unref();
	block = String();
}

void AIHistorySummarizer::_on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	const Ref<AIProvider> request_provider = provider;
	const String request_block = block;
	active = false;
	provider.unref();
	block = String();

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200 || request_provider.is_null() || !session) {
		AI_WARN(vformat("AI summary request failed (result=%d, code=%d) — heuristic summary kept.", p_result, p_response_code));
		return;
	}
	const String text = request_provider->parse_response(String::utf8((const char *)p_body.ptr(), p_body.size()));
	// parse_response() reports failures as "Error: ..." / "API Error: ..." strings.
	if (text.is_empty() || text.begins_with("Error: ") || text.begins_with("API Error: ")) {
		AI_WARN("AI summary response was empty or an error — heuristic summary kept.");
		return;
	}
	// Swap exactly the block this request was made for; if it is gone, keep
	// what is there rather than guess where the text belongs.
	if (session->replace_summary_block(request_block, "--- AI Summary ---\n" + text + "\n")) {
		AI_LOG("AI summary applied (" + itos(text.length()) + " chars).");
	} else {
		AI_WARN("AI summary arrived but its block is gone — discarded.");
	}
}

AIHistorySummarizer::AIHistorySummarizer() {
	http_request = memnew(HTTPRequest);
	http_request->set_use_threads(true);
	http_request->set_timeout(REQUEST_TIMEOUT_SEC);
	http_request->connect("request_completed", callable_mp(this, &AIHistorySummarizer::_on_request_completed));
	add_child(http_request);
}

#endif // TOOLS_ENABLED
