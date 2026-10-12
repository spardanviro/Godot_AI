#include "openai_provider.h"

#include "ai_model_names.h"

// What is left out is what is known not to be a current chat model; a name
// of a kind not seen before is offered.
bool OpenAIProvider::_accepts_listed_model(const String &p_id) const {
	static const int OLDEST_GENERATION = 5000; // GPT-5.
	if (AIModelNames::is_dated_snapshot(p_id) || p_id.begins_with("ft:")) {
		return false;
	}
	if (AIModelNames::contains_any(p_id, { "embedding", "whisper", "tts", "dall-e", "image", "audio", "realtime", "transcribe",
								"moderation", "search", "sora", "davinci", "babbage", "instruct", "computer-use" })) {
		return false;
	}
	for (const char *family : { "gpt-", "chatgpt-" }) {
		const int version = AIModelNames::version_after(p_id, family);
		if (version >= 0) {
			return version >= OLDEST_GENERATION;
		}
	}
	// The o1 / o3 / o4 reasoning series preceded GPT-5.
	return AIModelNames::version_after(p_id, "o") < 0;
}

Vector<String> OpenAIProvider::_get_preferred_models() const {
	// Only reached when no "gpt-<version>" model is listed.
	return { "gpt-5.4", "gpt-5.3-codex", "gpt-5.4-mini", "gpt-5.4-pro", "gpt-5.4-nano" };
}

Vector<String> OpenAIProvider::get_headers() const {
	Vector<String> headers = OpenAICompatibleProvider::get_headers();
	headers.insert(1, "Accept: text/event-stream");
	return headers;
}

int OpenAIProvider::get_model_context_length() const {
	const String m = _get_effective_model();
	if (m.find("o3") >= 0 || m.find("o4") >= 0) {
		return 200000;
	}
	if (m.find("gpt-5") >= 0 || m.find("codex") >= 0) {
		return 400000; // GPT-5 family: 400K window.
	}
	if (m.find("gpt-4.1") >= 0) {
		return 1048576;
	}
	if (m.find("gpt-4o") >= 0 || m.find("gpt-4-turbo") >= 0) {
		return 128000;
	}
	if (m.find("gpt-4") >= 0) {
		return 8192;
	}
	if (m.find("gpt-3.5") >= 0) {
		return 16384;
	}
	return 128000;
}

int OpenAIProvider::get_recommended_max_tokens() const {
	const String m = _get_effective_model();
	if (m.find("o3-pro") >= 0) {
		return 32768;
	}
	if (m.find("o3") >= 0 || m.find("o4") >= 0 || m.find("gpt-5.4") >= 0) {
		return 16384;
	}
	// Codex reasoning models need room for their reasoning output.
	if (m.find("codex") >= 0) {
		return 32768;
	}
	if (m.find("gpt-5") >= 0) {
		return 16384;
	}
	if (m.find("gpt-4.1") >= 0) {
		return 32768;
	}
	if (m.find("gpt-4o") >= 0) {
		return 16384;
	}
	if (m.find("gpt-4") >= 0 || m.find("gpt-3.5") >= 0) {
		return 4096;
	}
	return 8192;
}
