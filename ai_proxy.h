#pragma once

#include "core/string/ustring.h"

class HTTPRequest;

// An HTTP proxy to send a request through. Not set: connect directly.
struct AIProxyConfig {
	String host;
	int port = -1;

	bool is_set() const { return !host.is_empty() && port > 0; }
	String to_text() const { return is_set() ? host + ":" + itos(port) : String(); }
};

// Which proxy the assistant's own requests use — the model API, the model
// list, asset generation, web search and page fetch. HTTP proxies only (the
// engine's HTTP client speaks CONNECT, not SOCKS).
class AIProxy {
public:
	// Values of the "ai_assistant/proxy_mode" setting.
	static const char *MODE_SYSTEM; // What the system or the editor is configured to use.
	static const char *MODE_OFF;
	static const char *MODE_CUSTOM; // "ai_assistant/proxy_address".

	// Reads "host:port", "http://host:port/", and the Windows per-protocol
	// form "http=host:port;https=host:port" (the https entry wins). Fails for
	// anything else, SOCKS included.
	static bool parse(const String &p_text, AIProxyConfig &r_config);
	// Hosts that are reached directly even when a proxy is set: this machine
	// and the local network (a local model server, a LAN gateway).
	static bool is_local_host(const String &p_host);
	// The proxy for a mode, given what each source says. In MODE_SYSTEM the
	// first source that parses wins, in the order of the parameters.
	static AIProxyConfig choose(const String &p_mode, const String &p_custom_address, const String &p_editor_proxy, const String &p_environment_proxy, const String &p_os_proxy);

	// The system's HTTP proxy as the OS reports it ("" if none or unknown).
	static String get_os_proxy();

#ifdef TOOLS_ENABLED
	// From the editor settings, the environment and the OS. Main thread.
	static AIProxyConfig get_configured();
	// The same, as it would be for a mode and address not saved yet.
	static AIProxyConfig preview(const String &p_mode, const String &p_custom_address);
	// get_configured(), or nothing if the URL's host is local.
	static AIProxyConfig for_url(const String &p_url);
	// Points an HTTPRequest at the proxy for p_url, or back to direct.
	static void apply(HTTPRequest *p_request, const String &p_url);
#endif
};
