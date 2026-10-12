#pragma once

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class AIWebSearch : public RefCounted {
	GDCLASS(AIWebSearch, RefCounted);

protected:
	static void _bind_methods();

public:
	// Build DuckDuckGo HTML search URL.
	String build_search_url(const String &p_query) const;

	// Parse DuckDuckGo HTML search results page into readable text.
	String parse_search_results(const String &p_response_body) const;

	// Extract readable text from HTML (strip tags).
	String extract_text_from_html(const String &p_html) const;

	// Build a URL for fetching a web page.
	String sanitize_url(const String &p_url) const;

	AIWebSearch();
};
