#include "openai_provider.h"

bool OpenAIProvider::_accepts_listed_model(const String &p_id) const {
	// Flagship and codex series only.
	return p_id.begins_with("gpt-5");
}

Vector<String> OpenAIProvider::_get_preferred_models() const {
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
