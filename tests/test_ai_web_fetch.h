#pragma once

#include "../network/ai_web_fetch.h"

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "tests/test_macros.h"

namespace TestAIWebFetch {

// --- URL handling ---

TEST_CASE("[AIAssistant][WebFetch] a URL is split into scheme, host, port and path") {
	AIWebFetch::Target target;

	REQUIRE(AIWebFetch::parse_url("https://Docs.Example.com/en/stable/?q=1#top", target));

	CHECK(target.use_tls);
	CHECK(target.host == "docs.example.com");
	CHECK(target.port == 443);
	CHECK(target.path == "/en/stable/?q=1");
}

TEST_CASE("[AIAssistant][WebFetch] port and missing path are handled") {
	AIWebFetch::Target target;

	REQUIRE(AIWebFetch::parse_url("http://example.com:8080", target));
	CHECK_FALSE(target.use_tls);
	CHECK(target.port == 8080);
	CHECK(target.path == "/");

	REQUIRE(AIWebFetch::parse_url("http://example.com?q=1", target));
	CHECK(target.port == 80);
	CHECK(target.path == "/?q=1");
}

TEST_CASE("[AIAssistant][WebFetch] URLs that two parsers could read differently are refused") {
	static const char *refused[] = {
		"ftp://example.com/",
		"http://user@example.com/", // User info.
		"http://example.com@10.0.0.1/",
		"http://example.com\\@10.0.0.1/",
		"http://[::1]/",
		"http://example.com:99999/",
		"http://example.com:80:80/",
		"http://example.com:abc/",
		"http://example.com/a b", // Would break the request line.
		"http://example.com/a\r\nX-Injected: 1",
		"http:///path",
		nullptr,
	};
	for (int i = 0; refused[i]; i++) {
		AIWebFetch::Target target;
		CHECK_MESSAGE(!AIWebFetch::parse_url(refused[i], target), refused[i]);
	}
}

TEST_CASE("[AIAssistant][WebFetch] a Location header is resolved against the page it came from") {
	const String base = "https://example.com:8443/docs/page.html?x=1";

	CHECK(AIWebFetch::resolve_redirect(base, "https://other.org/a") == "https://other.org/a");
	CHECK(AIWebFetch::resolve_redirect(base, "//cdn.example.com/a") == "https://cdn.example.com/a");
	CHECK(AIWebFetch::resolve_redirect(base, "/root") == "https://example.com:8443/root");
	CHECK(AIWebFetch::resolve_redirect(base, "next.html") == "https://example.com:8443/docs/next.html");
	CHECK(AIWebFetch::resolve_redirect(base, "?y=2") == "https://example.com:8443/docs/page.html?y=2");
	CHECK(AIWebFetch::resolve_redirect("http://example.com", "/root") == "http://example.com/root");
	CHECK(AIWebFetch::resolve_redirect(base, "HTTPS://Other.org/a") == "https://Other.org/a");
}

TEST_CASE("[AIAssistant][WebFetch] a redirect to another scheme has no target") {
	const String base = "https://example.com/";

	CHECK(AIWebFetch::resolve_redirect(base, "ftp://example.com/file").is_empty());
	CHECK(AIWebFetch::resolve_redirect(base, "javascript:alert(1)").is_empty());
	CHECK(AIWebFetch::resolve_redirect(base, "file:///etc/passwd").is_empty());
	CHECK(AIWebFetch::resolve_redirect(base, "  ").is_empty());
}

// --- Resolved addresses ---

TEST_CASE("[AIAssistant][WebFetch] public addresses are accepted, IPv4 first") {
	Vector<String> addresses;
	String reason;

	REQUIRE(AIWebFetch::pick_addresses({ "2606:2800:220:1::1", "93.184.216.34" }, AIWebFetch::Options(), addresses, reason));

	REQUIRE(addresses.size() == 2);
	CHECK(addresses[0] == "93.184.216.34");
	CHECK(addresses[1] == "2606:2800:220:1::1");
}

TEST_CASE("[AIAssistant][WebFetch] a name that also resolves to a private address is refused entirely") {
	Vector<String> addresses;
	String reason;

	CHECK_FALSE(AIWebFetch::pick_addresses({ "93.184.216.34", "10.0.0.5" }, AIWebFetch::Options(), addresses, reason));
	CHECK(reason.contains("private"));
	CHECK_FALSE(AIWebFetch::pick_addresses({ "127.0.0.1" }, AIWebFetch::Options(), addresses, reason));
	CHECK_FALSE(AIWebFetch::pick_addresses({ "::1" }, AIWebFetch::Options(), addresses, reason));
	// As the engine's resolver spells loopback and an AAAA-only inward name.
	CHECK_FALSE(AIWebFetch::pick_addresses({ "0:0:0:0:0:0:0:1" }, AIWebFetch::Options(), addresses, reason));
	CHECK_FALSE(AIWebFetch::pick_addresses({ "93.184.216.34", "fe80:0:0:0:0:0:0:1" }, AIWebFetch::Options(), addresses, reason));
	CHECK_FALSE(AIWebFetch::pick_addresses({ "169.254.169.254" }, AIWebFetch::Options(), addresses, reason));
}

TEST_CASE("[AIAssistant][WebFetch] a name that does not resolve is refused") {
	Vector<String> addresses;
	String reason;

	CHECK_FALSE(AIWebFetch::pick_addresses(Vector<String>(), AIWebFetch::Options(), addresses, reason));
	CHECK(reason.contains("resolved"));
}

// --- The fetch itself, against a server on this machine ---

// A tiny HTTP server. Names are mapped to it (or elsewhere) by _resolve().
class LocalServer {
	Ref<TCPServer> server;
	Thread thread;
	SafeFlag quit;
	Mutex mutex;
	Vector<String> requests;
	int port = 0;

	static String _respond(const String &p_request_line, int p_port) {
		String path = p_request_line.get_slice(" ", 1);
		// A client that takes this server for a proxy asks for the whole URL.
		if (path.begins_with("http://")) {
			path = path.substr(path.find("/", 7));
		}
		if (path == "/start") {
			return "HTTP/1.1 302 Found\r\nLocation: /final?x=1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		}
		if (path == "/final?x=1") {
			return "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
		}
		if (path == "/inward") {
			return "HTTP/1.1 302 Found\r\nLocation: http://internal.test:" + itos(p_port) + "/secret\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		}
		if (path == "/loop") {
			return "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		}
		if (path == "/cut") {
			// Promises 5000 bytes, sends 100, hangs up.
			return "HTTP/1.1 200 OK\r\nContent-Length: 5000\r\nConnection: close\r\n\r\n" + String("x").repeat(100);
		}
		if (path == "/big") {
			return "HTTP/1.1 200 OK\r\nContent-Length: 5000\r\nConnection: close\r\n\r\n" + String("x").repeat(5000);
		}
		return "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	}

	void _serve(const Ref<StreamPeerTCP> &p_peer) {
		String request;
		const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 2000;
		while (!request.contains("\r\n\r\n") && OS::get_singleton()->get_ticks_msec() < deadline) {
			p_peer->poll();
			if (p_peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
				return; // The client went away without asking for anything.
			}
			const int available = p_peer->get_available_bytes();
			if (available <= 0) {
				OS::get_singleton()->delay_usec(1000);
				continue;
			}
			PackedByteArray bytes;
			bytes.resize(available);
			int received = 0;
			p_peer->get_partial_data(bytes.ptrw(), available, received);
			request += String::utf8((const char *)bytes.ptr(), received);
		}
		{
			MutexLock lock(mutex);
			requests.push_back(request);
		}
		const CharString response = _respond(request.get_slice("\r\n", 0), port).utf8();
		p_peer->put_data((const uint8_t *)response.get_data(), response.length());
		p_peer->disconnect_from_host();
	}

	static void _run(void *p_userdata) {
		LocalServer *self = static_cast<LocalServer *>(p_userdata);
		while (!self->quit.is_set()) {
			if (self->server->is_connection_available()) {
				self->_serve(self->server->take_connection());
			} else {
				OS::get_singleton()->delay_usec(1000);
			}
		}
	}

public:
	int get_port() const { return port; }
	String url(const String &p_host, const String &p_path) const { return "http://" + p_host + ":" + itos(port) + p_path; }

	Vector<String> get_requests() {
		MutexLock lock(mutex);
		return requests;
	}

	bool was_asked_for(const String &p_path) {
		const Vector<String> seen = get_requests();
		for (int i = 0; i < seen.size(); i++) {
			if (seen[i].begins_with("GET " + p_path + " ")) {
				return true;
			}
		}
		return false;
	}

	LocalServer() {
		server.instantiate();
		REQUIRE(server->listen(0, IPAddress("127.0.0.1")) == OK);
		port = server->get_local_port();
		thread.start(_run, this);
	}

	~LocalServer() {
		quit.set();
		thread.wait_to_finish();
		server->stop();
	}
};

static Vector<String> _resolve(const String &p_host) {
	if (p_host == "site.test") {
		return { "127.0.0.1" };
	}
	if (p_host == "internal.test") {
		return { "10.0.0.5" };
	}
	if (p_host == "mixed.test") {
		return { "127.0.0.1", "10.0.0.5" };
	}
	return Vector<String>();
}

// The test server stands in for "the public internet"; everything else that
// is private stays private.
static bool _only_the_test_server(const String &p_address) {
	return p_address == "127.0.0.1";
}

static bool _always_stop(void *) {
	return true;
}

static AIWebFetch::Options _options() {
	AIWebFetch::Options options;
	options.resolve = &_resolve;
	options.is_address_allowed = &_only_the_test_server;
	options.timeout_msec = 5000;
	return options;
}

TEST_CASE("[AIAssistant][WebFetch] the connection goes to the checked address and still names the host") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/final?x=1"), _options());

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.response_code == 200);
	CHECK(String::utf8((const char *)result.body.ptr(), result.body.size()) == "hello");
	// Only reachable because the fetch connected to what _resolve() returned:
	// nothing on this machine answers to the name "site.test".
	const Vector<String> requests = server.get_requests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].contains("Host: site.test:" + itos(server.get_port()) + "\r\n"));
}

TEST_CASE("[AIAssistant][WebFetch] a relative redirect is followed on the same host") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/start"), _options());

	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.response_code == 200);
	CHECK(result.redirects == 1);
	CHECK(result.final_url == server.url("site.test", "/final?x=1"));
}

TEST_CASE("[AIAssistant][WebFetch] a redirect to a host that resolves inward is not followed") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/inward"), _options());

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("private"));
	CHECK_FALSE(server.was_asked_for("/secret"));
}

TEST_CASE("[AIAssistant][WebFetch] a name with one inward address is refused before anything is sent") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("mixed.test", "/final?x=1"), _options());

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("private"));
	CHECK(server.get_requests().is_empty());
}

TEST_CASE("[AIAssistant][WebFetch] an address literal in the URL is refused by the URL policy") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("127.0.0.1", "/final?x=1"), _options());

	CHECK_FALSE(result.ok);
	CHECK(server.get_requests().is_empty());
}

TEST_CASE("[AIAssistant][WebFetch] through a proxy the request names the host and is not resolved here") {
	LocalServer proxy;
	AIWebFetch::Options options = _options();
	options.proxy.host = "127.0.0.1";
	options.proxy.port = proxy.get_port();

	const AIWebFetch::Result result = AIWebFetch::get("http://public.example/final?x=1", options);

	// _resolve() knows no public.example: a direct fetch would have failed.
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK(result.response_code == 200);
	const Vector<String> requests = proxy.get_requests();
	REQUIRE(requests.size() == 1);
	CHECK(requests[0].begins_with("GET http://public.example:80/final?x=1 "));
}

TEST_CASE("[AIAssistant][WebFetch] through a proxy the URL policy still refuses local names") {
	LocalServer proxy;
	AIWebFetch::Options options = _options();
	options.proxy.host = "127.0.0.1";
	options.proxy.port = proxy.get_port();

	const AIWebFetch::Result result = AIWebFetch::get("http://localhost/final?x=1", options);

	CHECK_FALSE(result.ok);
	CHECK(proxy.get_requests().is_empty());
}

TEST_CASE("[AIAssistant][WebFetch] a redirect loop ends at the limit") {
	LocalServer server;
	AIWebFetch::Options options = _options();
	options.max_redirects = 3;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/loop"), options);

	CHECK_FALSE(result.ok);
	CHECK(result.error == "too many redirects");
	CHECK(server.get_requests().size() == 4);
}

TEST_CASE("[AIAssistant][WebFetch] a response over the size limit is dropped") {
	LocalServer server;
	AIWebFetch::Options options = _options();
	options.body_limit_bytes = 1000;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/big"), options);

	CHECK_FALSE(result.ok);
	CHECK(result.error == "the response is too large");
	CHECK(result.body.is_empty());
}

TEST_CASE("[AIAssistant][WebFetch] a response that is cut short is an error, not a short page") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/cut"), _options());

	CHECK_FALSE(result.ok);
	CHECK(result.error.contains("cut"));
}

TEST_CASE("[AIAssistant][WebFetch] a status other than 200 is reported, not hidden") {
	LocalServer server;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/missing"), _options());

	CHECK(result.ok);
	CHECK(result.response_code == 404);
}

TEST_CASE("[AIAssistant][WebFetch] a fetch that is told to stop sends nothing") {
	LocalServer server;
	AIWebFetch::Options options = _options();
	options.should_stop = &_always_stop;

	const AIWebFetch::Result result = AIWebFetch::get(server.url("site.test", "/final?x=1"), options);

	CHECK_FALSE(result.ok);
	CHECK(result.stopped);
	CHECK(server.get_requests().is_empty());
}

} // namespace TestAIWebFetch
