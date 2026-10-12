#include "ai_proxy.h"

#include "../agent/ai_response_analysis.h"

#include "core/os/os.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"
#endif

const char *AIProxy::MODE_SYSTEM = "system";
const char *AIProxy::MODE_OFF = "off";
const char *AIProxy::MODE_CUSTOM = "custom";

static const int MAX_PORT = 65535;
static const int DEFAULT_PROXY_PORT = 8080;

bool AIProxy::parse(const String &p_text, AIProxyConfig &r_config) {
	String text = p_text.strip_edges();
	if (text.is_empty()) {
		return false;
	}
	// Windows: "http=host:port;https=host:port;ftp=...".
	if (text.find_char('=') >= 0) {
		String chosen;
		const Vector<String> entries = text.split(";", false);
		for (int i = 0; i < entries.size(); i++) {
			const String protocol = entries[i].get_slice("=", 0).strip_edges().to_lower();
			const String address = entries[i].get_slice("=", 1).strip_edges();
			if (protocol == "https" || (protocol == "http" && chosen.is_empty())) {
				chosen = address;
			}
		}
		text = chosen;
		if (text.is_empty()) {
			return false;
		}
	}

	const int scheme_end = text.find("://");
	if (scheme_end >= 0) {
		const String scheme = text.left(scheme_end).to_lower();
		if (scheme != "http" && scheme != "https") {
			return false; // SOCKS and the like.
		}
		text = text.substr(scheme_end + 3);
	}
	text = text.get_slice("/", 0);
	if (text.find_char('@') >= 0) {
		return false; // Proxy credentials are not supported.
	}

	AIProxyConfig config;
	const int colon = text.rfind(":");
	if (colon < 0) {
		config.host = text;
		config.port = DEFAULT_PROXY_PORT;
	} else {
		config.host = text.left(colon);
		const String port = text.substr(colon + 1);
		if (!port.is_valid_int()) {
			return false;
		}
		config.port = port.to_int();
	}
	if (config.host.is_empty() || config.host.find_char(':') >= 0 || config.host.find_char(' ') >= 0 || config.port < 1 || config.port > MAX_PORT) {
		return false;
	}
	r_config = config;
	return true;
}

bool AIProxy::is_local_host(const String &p_host) {
	const String host = p_host.to_lower();
	// A name without a dot is a machine on the local network ("gpu-box"),
	// which a proxy elsewhere could not find anyway.
	if (host.is_empty() || host.find_char('.') < 0 || host.ends_with(".localhost") || host.ends_with(".local") ||
			host.ends_with(".internal") || host.ends_with(".lan") || host.begins_with("[")) {
		return true;
	}
	// An address literal in a local range. Names are never resolved here.
	return host.is_valid_ip_address() && AIResponseAnalysis::is_private_address(host);
}

AIProxyConfig AIProxy::choose(const String &p_mode, const String &p_custom_address, const String &p_editor_proxy, const String &p_environment_proxy, const String &p_os_proxy) {
	AIProxyConfig config;
	if (p_mode == MODE_OFF) {
		return config;
	}
	if (p_mode == MODE_CUSTOM) {
		parse(p_custom_address, config);
		return config;
	}
	if (!parse(p_editor_proxy, config) && !parse(p_environment_proxy, config)) {
		parse(p_os_proxy, config);
	}
	return config;
}

#ifdef TOOLS_ENABLED

static String _editor_proxy() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es || !es->has_setting("network/http_proxy/host")) {
		return String();
	}
	const String host = es->get_setting("network/http_proxy/host");
	const int port = es->has_setting("network/http_proxy/port") ? int(es->get_setting("network/http_proxy/port")) : -1;
	return host.is_empty() || port <= 0 ? String() : host + ":" + itos(port);
}

static String _environment_proxy() {
	static const char *variables[] = { "HTTPS_PROXY", "https_proxy", "HTTP_PROXY", "http_proxy", "ALL_PROXY", "all_proxy", nullptr };
	for (int i = 0; variables[i]; i++) {
		const String value = OS::get_singleton()->get_environment(variables[i]);
		AIProxyConfig config;
		if (AIProxy::parse(value, config)) {
			return value;
		}
	}
	return String();
}

AIProxyConfig AIProxy::preview(const String &p_mode, const String &p_custom_address) {
	return choose(p_mode, p_custom_address, _editor_proxy(), _environment_proxy(), get_os_proxy());
}

AIProxyConfig AIProxy::get_configured() {
	EditorSettings *es = EditorSettings::get_singleton();
	String mode = MODE_SYSTEM;
	String address;
	if (es && es->has_setting("ai_assistant/proxy_mode")) {
		mode = es->get_setting("ai_assistant/proxy_mode");
	}
	if (es && es->has_setting("ai_assistant/proxy_address")) {
		address = es->get_setting("ai_assistant/proxy_address");
	}
	return preview(mode, address);
}

AIProxyConfig AIProxy::for_url(const String &p_url) {
	if (is_local_host(AIResponseAnalysis::get_url_host(p_url))) {
		return AIProxyConfig();
	}
	return get_configured();
}

void AIProxy::apply(HTTPRequest *p_request, const String &p_url) {
	const AIProxyConfig config = for_url(p_url);
	// An empty host turns the proxy off again.
	p_request->set_http_proxy(config.host, config.is_set() ? config.port : -1);
	p_request->set_https_proxy(config.host, config.is_set() ? config.port : -1);
}

#endif // TOOLS_ENABLED
