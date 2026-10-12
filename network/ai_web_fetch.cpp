#include "ai_web_fetch.h"

#include "../agent/ai_response_analysis.h"

#include "core/crypto/crypto.h"
#include "core/io/http_client.h"
#include "core/io/ip.h"
#include "core/os/os.h"

static const int WAIT_INTERVAL_USEC = 10000;
static const int MAX_PORT = 65535;

static bool _is_redirect(int p_code) {
	return p_code == 301 || p_code == 302 || p_code == 303 || p_code == 307 || p_code == 308;
}

static String _origin(const AIWebFetch::Target &p_target) {
	const int default_port = p_target.use_tls ? 443 : 80;
	return String(p_target.use_tls ? "https://" : "http://") + p_target.host +
			(p_target.port == default_port ? String() : ":" + itos(p_target.port));
}

// Returns true when the fetch must be abandoned; r_result then says why.
static bool _interrupted(const AIWebFetch::Options &p_options, uint64_t p_deadline_msec, AIWebFetch::Result &r_result) {
	if (p_options.should_stop && p_options.should_stop(p_options.should_stop_userdata)) {
		r_result.stopped = true;
		return true;
	}
	if (OS::get_singleton()->get_ticks_msec() > p_deadline_msec) {
		r_result.error = "the request timed out";
		return true;
	}
	return false;
}

bool AIWebFetch::parse_url(const String &p_url, Target &r_target) {
	Target target;
	String rest;
	if (p_url.begins_with("https://")) {
		target.use_tls = true;
		target.port = 443;
		rest = p_url.substr(8);
	} else if (p_url.begins_with("http://")) {
		rest = p_url.substr(7);
	} else {
		return false;
	}
	// The path goes into the request line: no whitespace or control characters.
	for (int i = 0; i < rest.length(); i++) {
		if (rest[i] <= 0x20 || rest[i] == 0x7f) {
			return false;
		}
	}

	int authority_end = rest.length();
	for (const char *delimiter = "/?#"; *delimiter; delimiter++) {
		const int position = rest.find_char(*delimiter);
		if (position >= 0 && position < authority_end) {
			authority_end = position;
		}
	}
	const String authority = rest.left(authority_end).to_lower();
	// Anything two parsers could read differently is refused outright.
	if (authority.is_empty() || authority.find_char('@') >= 0 || authority.find_char('\\') >= 0 || authority.find_char('[') >= 0) {
		return false;
	}
	const Vector<String> host_and_port = authority.split(":");
	if (host_and_port.size() > 2) {
		return false;
	}
	if (host_and_port.size() == 2) {
		if (!host_and_port[1].is_valid_int()) {
			return false;
		}
		target.port = host_and_port[1].to_int();
		if (target.port < 1 || target.port > MAX_PORT) {
			return false;
		}
	}
	target.host = host_and_port[0];
	while (target.host.ends_with(".")) {
		target.host = target.host.left(target.host.length() - 1);
	}
	// The address that gets checked must be the address that gets used.
	if (target.host.is_empty() || target.host != AIResponseAnalysis::get_url_host(p_url)) {
		return false;
	}

	String path = rest.substr(authority_end).get_slice("#", 0);
	if (!path.begins_with("/")) {
		path = "/" + path; // Empty, or a query right after the host.
	}
	target.path = path;
	r_target = target;
	return true;
}

String AIWebFetch::resolve_redirect(const String &p_base_url, const String &p_location) {
	String location = p_location.strip_edges();
	if (location.is_empty()) {
		return String();
	}
	// Schemes are case-insensitive; the rest of this module expects lowercase.
	const int scheme_end = location.find("://");
	if (scheme_end > 0) {
		location = location.left(scheme_end).to_lower() + location.substr(scheme_end);
	}
	if (location.begins_with("http://") || location.begins_with("https://")) {
		return location;
	}
	Target base;
	if (!parse_url(p_base_url, base)) {
		return String();
	}
	if (location.begins_with("//")) {
		return String(base.use_tls ? "https:" : "http:") + location;
	}
	if (location.begins_with("/")) {
		return _origin(base) + location;
	}
	// Another scheme (ftp:, javascript:, data:, ...).
	const int colon = location.find_char(':');
	const int slash = location.find_char('/');
	if (colon >= 0 && (slash < 0 || colon < slash)) {
		return String();
	}
	if (location.begins_with("?")) {
		return _origin(base) + base.path.get_slice("?", 0) + location;
	}
	const String directory = base.path.get_slice("?", 0);
	return _origin(base) + directory.left(directory.rfind("/") + 1) + location;
}

bool AIWebFetch::pick_addresses(const Vector<String> &p_resolved, const Options &p_options, Vector<String> &r_addresses, String &r_reason) {
	Vector<String> ipv4;
	Vector<String> ipv6;
	for (int i = 0; i < p_resolved.size(); i++) {
		const String &address = p_resolved[i];
		const bool allowed = p_options.is_address_allowed ? p_options.is_address_allowed(address) : !AIResponseAnalysis::is_private_address(address);
		if (!allowed) {
			r_reason = "the host resolves to a local or private address";
			return false;
		}
		if (address.find_char(':') >= 0) {
			ipv6.push_back(address);
		} else {
			ipv4.push_back(address);
		}
	}
	if (ipv4.is_empty() && ipv6.is_empty()) {
		r_reason = "the host could not be resolved";
		return false;
	}
	r_addresses = ipv4;
	r_addresses.append_array(ipv6);
	return true;
}

bool AIWebFetch::_resolve(const String &p_host, const Options &p_options, uint64_t p_deadline_msec, Vector<String> &r_addresses, Result &r_result) {
	Vector<String> resolved;
	if (p_options.resolve) {
		resolved = p_options.resolve(p_host);
	} else if (p_host.is_valid_ip_address()) {
		resolved.push_back(p_host);
	} else {
		IP *ip = IP::get_singleton();
		const IP::ResolverID id = ip->resolve_hostname_queue_item(p_host, IP::TYPE_ANY);
		if (id == IP::RESOLVER_INVALID_ID) {
			r_result.error = "the host could not be resolved";
			return false;
		}
		while (ip->get_resolve_item_status(id) == IP::RESOLVER_STATUS_WAITING) {
			if (_interrupted(p_options, p_deadline_msec, r_result)) {
				ip->erase_resolve_item(id);
				return false;
			}
			OS::get_singleton()->delay_usec(WAIT_INTERVAL_USEC);
		}
		if (ip->get_resolve_item_status(id) == IP::RESOLVER_STATUS_DONE) {
			const Array found = ip->get_resolve_item_addresses(id);
			for (int i = 0; i < found.size(); i++) {
				resolved.push_back(String(found[i]));
			}
		}
		ip->erase_resolve_item(id);
	}
	return pick_addresses(resolved, p_options, r_addresses, r_result.error);
}

bool AIWebFetch::_get_once(const Target &p_target, const Vector<String> &p_addresses, const Options &p_options, uint64_t p_deadline_msec, Hop &r_hop, Result &r_result) {
	// Directly: connect to an address that was checked, never to the name
	// (the TLS certificate and the Host header still carry the name).
	// Through a proxy: p_addresses is the name, for the proxy to resolve.
	Ref<HTTPClient> client;
	for (int i = 0; i < p_addresses.size(); i++) {
		Ref<HTTPClient> candidate = Ref<HTTPClient>(HTTPClient::create());
		Ref<TLSOptions> tls_options;
		if (p_target.use_tls) {
			tls_options = TLSOptions::client(Ref<X509Certificate>(), p_target.host);
		}
		if (candidate.is_valid() && p_options.proxy.is_set()) {
			candidate->set_http_proxy(p_options.proxy.host, p_options.proxy.port);
			candidate->set_https_proxy(p_options.proxy.host, p_options.proxy.port);
		}
		if (candidate.is_null() || candidate->connect_to_host(p_addresses[i], p_target.port, tls_options) != OK) {
			continue;
		}
		const uint64_t connect_deadline_msec = OS::get_singleton()->get_ticks_msec() + p_options.connect_timeout_msec;
		while (candidate->get_status() == HTTPClient::STATUS_CONNECTING || candidate->get_status() == HTTPClient::STATUS_RESOLVING) {
			if (_interrupted(p_options, p_deadline_msec, r_result)) {
				return false;
			}
			if (OS::get_singleton()->get_ticks_msec() > connect_deadline_msec) {
				break; // Try the next address.
			}
			candidate->poll();
			OS::get_singleton()->delay_usec(WAIT_INTERVAL_USEC);
		}
		if (candidate->get_status() == HTTPClient::STATUS_CONNECTED) {
			client = candidate;
			break;
		}
	}
	if (client.is_null()) {
		r_result.error = "could not connect to the host";
		return false;
	}

	Vector<String> headers = p_options.headers;
	headers.push_back("Host: " + _origin(p_target).get_slice("://", 1));
	if (client->request(HTTPClient::METHOD_GET, p_target.path, headers, nullptr, 0) != OK) {
		r_result.error = "the request could not be sent";
		return false;
	}
	const uint64_t header_deadline_msec = MIN(p_deadline_msec, OS::get_singleton()->get_ticks_msec() + p_options.header_timeout_msec);
	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		if (_interrupted(p_options, header_deadline_msec, r_result)) {
			return false;
		}
		client->poll();
		OS::get_singleton()->delay_usec(WAIT_INTERVAL_USEC);
	}
	if (!client->has_response()) {
		r_result.error = "the host did not answer";
		return false;
	}

	r_hop.response_code = client->get_response_code();
	List<String> response_headers;
	client->get_response_headers(&response_headers);
	for (const String &header : response_headers) {
		if (header.to_lower().begins_with("location:")) {
			r_hop.location = header.substr(9).strip_edges();
		}
	}
	if (_is_redirect(r_hop.response_code) && !r_hop.location.is_empty()) {
		return true; // The body of a redirect is of no interest.
	}

	// Known up front when the response says how long it is; a response that
	// does neither simply ends when the host closes the connection.
	const bool is_chunked = client->is_response_chunked();
	const int64_t expected_bytes = client->get_response_body_length();
	while (client->get_status() == HTTPClient::STATUS_BODY) {
		if (_interrupted(p_options, p_deadline_msec, r_result)) {
			return false;
		}
		client->poll();
		if (client->get_status() != HTTPClient::STATUS_BODY) {
			break; // poll() noticed the host closing the connection.
		}
		const PackedByteArray chunk = client->read_response_body_chunk();
		if (chunk.is_empty()) {
			OS::get_singleton()->delay_usec(WAIT_INTERVAL_USEC);
			continue;
		}
		r_hop.body.append_array(chunk);
		if (r_hop.body.size() > p_options.body_limit_bytes) {
			r_result.error = "the response is too large";
			return false;
		}
	}
	// Half a page must not pass for the page.
	const HTTPClient::Status end_status = client->get_status();
	// A chunked body is complete when its last chunk was read, which leaves
	// the client connected; a sized one when all its bytes are here.
	const bool is_cut = is_chunked ? end_status != HTTPClient::STATUS_CONNECTED : (expected_bytes > 0 && r_hop.body.size() < expected_bytes);
	if (is_cut) {
		r_result.error = "the connection was cut before the response was complete";
		return false;
	}
	return true;
}

AIWebFetch::Result AIWebFetch::get(const String &p_url, const Options &p_options) {
	Result result;
	const uint64_t deadline_msec = OS::get_singleton()->get_ticks_msec() + p_options.timeout_msec;
	String url = p_url;

	while (true) {
		Target target;
		if (!AIResponseAnalysis::is_public_web_url(url, result.error)) {
			return result;
		}
		if (!parse_url(url, target)) {
			result.error = "the URL is not well-formed";
			return result;
		}
		Vector<String> addresses;
		Hop hop;
		if (p_options.proxy.is_set()) {
			addresses.push_back(target.host);
		} else if (!_resolve(target.host, p_options, deadline_msec, addresses, result)) {
			return result;
		}
		if (!_get_once(target, addresses, p_options, deadline_msec, hop, result)) {
			return result;
		}
		if (!_is_redirect(hop.response_code) || hop.location.is_empty()) {
			result.ok = true;
			result.response_code = hop.response_code;
			result.body = hop.body;
			result.final_url = url;
			return result;
		}
		if (result.redirects >= p_options.max_redirects) {
			result.error = "too many redirects";
			return result;
		}
		url = resolve_redirect(url, hop.location);
		if (url.is_empty()) {
			result.error = "redirect to an unsupported address";
			return result;
		}
		result.redirects++;
	}
}
