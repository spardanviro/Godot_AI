#include "ai_url_policy.h"

#include "core/io/ip_address.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"

String AIUrlPolicy::get_url_host(const String &p_url) {
	String rest;
	if (p_url.begins_with("https://")) {
		rest = p_url.substr(8);
	} else if (p_url.begins_with("http://")) {
		rest = p_url.substr(7);
	} else {
		return String();
	}
	String host = rest.get_slice("/", 0).get_slice("?", 0).get_slice("#", 0).to_lower();
	if (host.find("@") >= 0) {
		host = host.get_slice("@", host.get_slice_count("@") - 1);
	}
	if (host.begins_with("[")) {
		return host.get_slice("]", 0) + "]"; // IPv6 literal, kept bracketed.
	}
	host = host.get_slice(":", 0);
	while (host.ends_with(".")) {
		host = host.left(host.length() - 1); // "localhost." is localhost.
	}
	return host;
}

static bool _is_private_ipv4(int p_a, int p_b, int p_c) {
	return p_a == 0 || p_a == 10 || p_a == 127 || p_a >= 224 ||
			(p_a == 100 && p_b >= 64 && p_b <= 127) || // Carrier-grade NAT.
			(p_a == 169 && p_b == 254) || // Link-local, cloud metadata.
			(p_a == 172 && p_b >= 16 && p_b <= 31) ||
			(p_a == 192 && p_b == 168) ||
			(p_a == 192 && p_b == 0 && (p_c == 0 || p_c == 2)) || // Protocol assignments, documentation.
			(p_a == 192 && p_b == 88 && p_c == 99) || // 6to4 relay anycast.
			(p_a == 198 && (p_b == 18 || p_b == 19)) || // Benchmarking.
			(p_a == 198 && p_b == 51 && p_c == 100) || // Documentation.
			(p_a == 203 && p_b == 0 && p_c == 113); // Documentation.
}

bool AIUrlPolicy::is_private_address(const String &p_ip) {
	if (p_ip.find_char(':') < 0) {
		const Vector<String> parts = p_ip.split(".");
		if (parts.size() != 4) {
			return true; // Not a plain dotted quad: do not guess.
		}
		int octets[4];
		for (int i = 0; i < 4; i++) {
			if (!parts[i].is_valid_int() || parts[i].length() > 3) {
				return true;
			}
			octets[i] = parts[i].to_int();
			if (octets[i] < 0 || octets[i] > 255) {
				return true;
			}
		}
		return _is_private_ipv4(octets[0], octets[1], octets[2]);
	}

	// IPv6 is judged by its bytes: the same address has many spellings
	// ("::1", "0:0:0:0:0:0:0:1"), and the engine's resolver uses the long one.
	if (!p_ip.is_valid_ip_address()) {
		return true;
	}
	const IPAddress address(p_ip);
	if (!address.is_valid() || address.is_wildcard()) {
		return true;
	}
	const uint8_t *bytes = address.get_ipv6();
	if (address.is_ipv4()) { // ::ffff:a.b.c.d
		return _is_private_ipv4(bytes[12], bytes[13], bytes[14]);
	}
	// NAT64 (64:ff9b::/96) leads to the IPv4 address in its last four bytes.
	static const uint8_t nat64_prefix[12] = { 0x00, 0x64, 0xff, 0x9b, 0, 0, 0, 0, 0, 0, 0, 0 };
	if (memcmp(bytes, nat64_prefix, sizeof(nat64_prefix)) == 0) {
		return _is_private_ipv4(bytes[12], bytes[13], bytes[14]);
	}
	// Everything public is global unicast, 2000::/3. That leaves out loopback,
	// unspecified, IPv4-compatible, unique-local, link-local, site-local and
	// multicast addresses in one test.
	if ((bytes[0] & 0xe0) != 0x20) {
		return true;
	}
	// Inside it: Teredo (2001::/32) and 6to4 (2002::/16) tunnel to an embedded
	// IPv4 address, and 2001:db8::/32 is documentation space.
	const bool is_teredo = bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x00 && bytes[3] == 0x00;
	const bool is_documentation = bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0d && bytes[3] == 0xb8;
	const bool is_6to4 = bytes[0] == 0x20 && bytes[1] == 0x02;
	return is_teredo || is_documentation || is_6to4;
}

bool AIUrlPolicy::is_public_web_url(const String &p_url, String &r_reason) {
	if (p_url.length() > MAX_URL_LENGTH) {
		r_reason = "URL is too long";
		return false;
	}
	if (!p_url.begins_with("https://") && !p_url.begins_with("http://")) {
		r_reason = "only http(s) URLs are allowed";
		return false;
	}
	const String host = get_url_host(p_url);
	if (host.begins_with("[")) {
		r_reason = "IPv6 literal hosts are not allowed";
		return false;
	}

	// A host made only of digits, dots and hex letters is an address in some
	// spelling (127.1, 0x7f.0.0.1, 2130706433): accept only a public dotted quad.
	bool numeric = !host.is_empty() && is_digit(host[0]);
	for (int i = 0; i < host.length() && numeric; i++) {
		numeric = is_hex_digit(host[i]) || host[i] == '.' || host[i] == 'x';
	}
	const bool is_private = host.is_empty() || host.find(".") < 0 || host == "localhost" ||
			host.ends_with(".localhost") || host.ends_with(".local") || host.ends_with(".internal") ||
			(numeric && is_private_address(host));
	if (is_private) {
		r_reason = "local and private-network addresses are not allowed";
		return false;
	}
	return true;
}
