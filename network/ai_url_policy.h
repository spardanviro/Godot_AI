#pragma once

#include "core/string/ustring.h"

// Which addresses a model-supplied URL may point at: plain public web
// addresses only. No loopback or private-network hosts (the editor would act
// as a proxy into the local network) and no oversized URLs (the easy way to
// smuggle data out).
class AIUrlPolicy {
public:
	static bool is_public_web_url(const String &p_url, String &r_reason);
	// Host part of an http(s) URL, lowercased, without user info, port or a
	// trailing dot. Empty for anything else.
	static String get_url_host(const String &p_url);
	// Loopback, private, link-local and other non-public addresses (IPv4 or
	// IPv6 literal). Used on what a host name resolves to.
	static bool is_private_address(const String &p_ip);

	static const int MAX_URL_LENGTH = 500;
};
