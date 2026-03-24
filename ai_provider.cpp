#include "ai_provider.h"

void AIProvider::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_api_key", "key"), &AIProvider::set_api_key);
	ClassDB::bind_method(D_METHOD("get_api_key"), &AIProvider::get_api_key);
	ClassDB::bind_method(D_METHOD("set_model", "model"), &AIProvider::set_model);
	ClassDB::bind_method(D_METHOD("get_model"), &AIProvider::get_model);
	ClassDB::bind_method(D_METHOD("set_api_endpoint", "endpoint"), &AIProvider::set_api_endpoint);
	ClassDB::bind_method(D_METHOD("get_api_endpoint"), &AIProvider::get_api_endpoint);
	ClassDB::bind_method(D_METHOD("set_max_tokens", "max"), &AIProvider::set_max_tokens);
	ClassDB::bind_method(D_METHOD("get_max_tokens"), &AIProvider::get_max_tokens);
	ClassDB::bind_method(D_METHOD("set_temperature", "temp"), &AIProvider::set_temperature);
	ClassDB::bind_method(D_METHOD("get_temperature"), &AIProvider::get_temperature);
	ClassDB::bind_method(D_METHOD("get_provider_name"), &AIProvider::get_provider_name);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "api_key"), "set_api_key", "get_api_key");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "model"), "set_model", "get_model");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "api_endpoint"), "set_api_endpoint", "get_api_endpoint");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_tokens"), "set_max_tokens", "get_max_tokens");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature"), "set_temperature", "get_temperature");
}

void AIProvider::set_api_key(const String &p_key) { api_key = p_key; }
String AIProvider::get_api_key() const { return api_key; }

void AIProvider::set_model(const String &p_model) { model = p_model; }
String AIProvider::get_model() const { return model; }

void AIProvider::set_api_endpoint(const String &p_endpoint) { api_endpoint = p_endpoint; }
String AIProvider::get_api_endpoint() const { return api_endpoint; }

void AIProvider::set_max_tokens(int p_max) { max_tokens = p_max; }
int AIProvider::get_max_tokens() const { return max_tokens; }

void AIProvider::set_temperature(float p_temp) { temperature = p_temp; }
float AIProvider::get_temperature() const { return temperature; }

String AIProvider::get_provider_name() const { return "base"; }
String AIProvider::get_default_endpoint() const { return ""; }
String AIProvider::get_default_model() const { return ""; }

String AIProvider::build_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	return "{}";
}

String AIProvider::parse_response(const String &p_response_body) const {
	return "";
}

Vector<String> AIProvider::get_headers() const {
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	return headers;
}

String AIProvider::get_models_list_url() const {
	return "";
}

Vector<String> AIProvider::get_models_list_headers() const {
	return get_headers();
}

PackedStringArray AIProvider::parse_models_list(const String &p_response_body) const {
	return PackedStringArray();
}

String AIProvider::select_best_model(const PackedStringArray &p_models) const {
	if (p_models.is_empty()) {
		return get_default_model();
	}
	return p_models[0];
}

// --- Model metadata defaults ---

int AIProvider::get_model_context_length() const {
	return 8192; // Conservative default.
}

int AIProvider::get_recommended_max_tokens() const {
	return 4096;
}

// --- Streaming defaults ---

String AIProvider::build_stream_request_body(const String &p_system_prompt, const Array &p_messages, const String &p_user_message) const {
	return build_request_body(p_system_prompt, p_messages, p_user_message);
}

String AIProvider::parse_stream_delta(const String &p_data) const {
	return "";
}

Dictionary AIProvider::parse_stream_delta_ex(const String &p_data) const {
	Dictionary result;
	result["content"] = parse_stream_delta(p_data);
	result["thinking"] = String();
	return result;
}

bool AIProvider::is_stream_done_marker(const String &p_data) const {
	return false;
}

String AIProvider::get_stream_url() const {
	return api_endpoint.is_empty() ? get_default_endpoint() : api_endpoint;
}

AIProvider::AIProvider() {}
AIProvider::~AIProvider() {}
