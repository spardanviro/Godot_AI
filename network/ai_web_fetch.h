#pragma once

#include "ai_proxy.h"

#include "core/io/ip_address.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

// A blocking HTTP GET for pages the model asked for, built so that a page
// cannot steer the editor to a local or private address:
//  - the host is resolved here, every address it resolves to is checked, and
//    the connection goes to one of exactly those addresses (the name is not
//    resolved a second time, so the answer cannot change in between);
//  - redirects are followed by hand, each hop through the same checks.
// Through a proxy only the checks on the URL remain: the proxy resolves the
// name, out of this code's sight. That is the price of using one.
// Meant to run on a worker thread; see AIWebRequester.
class AIWebFetch {
public:
	struct Target {
		bool use_tls = false;
		String host; // Lowercase name or dotted quad, as checked by the URL policy.
		int port = 80;
		String path = "/"; // Path and query.
	};

	struct Options {
		Vector<String> headers;
		int max_redirects = 5;
		int body_limit_bytes = 2 * 1024 * 1024; // A page, not a download.
		uint64_t timeout_msec = 30000; // For the whole fetch, redirects included.
		// For the status line and headers of one response. The HTTP client
		// buffers headers without a size limit, so the time is what bounds them.
		uint64_t header_timeout_msec = 10000;
		// Per address tried, so one that never answers leaves time for the next.
		uint64_t connect_timeout_msec = 8000;

		// Set: send the request through this HTTP proxy, by name.
		AIProxyConfig proxy;

		// Polled while waiting; return true to abandon the fetch.
		bool (*should_stop)(void *p_userdata) = nullptr;
		void *should_stop_userdata = nullptr;

		// Test seams. Null: the system resolver, and the policy of
		// AIResponseAnalysis::is_private_address().
		Vector<String> (*resolve)(const String &p_host) = nullptr;
		bool (*is_address_allowed)(const String &p_address) = nullptr;
	};

	struct Result {
		bool ok = false; // A response arrived; see response_code.
		bool stopped = false;
		int response_code = 0;
		PackedByteArray body;
		String error; // Why there is no response. Short, safe to show.
		String final_url;
		int redirects = 0;
	};

	// Splits an http(s) URL. Fails on anything the URL policy and the HTTP
	// client could read differently (user info, backslashes, IPv6 literals).
	static bool parse_url(const String &p_url, Target &r_target);
	// The absolute URL a Location header points at; empty if it is not http(s).
	static String resolve_redirect(const String &p_base_url, const String &p_location);
	// The addresses to connect to, IPv4 first. Fails if the list is empty or
	// any of them is not allowed: a name that also resolves inward is not trusted.
	static bool pick_addresses(const Vector<String> &p_resolved, const Options &p_options, Vector<String> &r_addresses, String &r_reason);

	static Result get(const String &p_url, const Options &p_options);

private:
	struct Hop {
		int response_code = 0;
		String location;
		PackedByteArray body;
	};

	static bool _resolve(const String &p_host, const Options &p_options, uint64_t p_deadline_msec, Vector<String> &r_addresses, Result &r_result);
	static bool _get_once(const Target &p_target, const Vector<String> &p_addresses, const Options &p_options, uint64_t p_deadline_msec, Hop &r_hop, Result &r_result);
};
