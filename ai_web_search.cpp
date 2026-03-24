#include "ai_web_search.h"

void AIWebSearch::_bind_methods() {
}

String AIWebSearch::build_search_url(const String &p_query) const {
	// Use DuckDuckGo HTML search (returns full web search results).
	String encoded = p_query.uri_encode();
	return "https://html.duckduckgo.com/html/?q=" + encoded;
}

// Helper: extract text between two positions, stripping HTML tags.
static String _strip_tags(const String &p_html) {
	String result;
	bool in_tag = false;
	for (int i = 0; i < p_html.length(); i++) {
		char32_t c = p_html[i];
		if (c == '<') {
			in_tag = true;
		} else if (c == '>') {
			in_tag = false;
		} else if (!in_tag) {
			result += c;
		}
	}
	// Decode common HTML entities.
	result = result.replace("&amp;", "&");
	result = result.replace("&lt;", "<");
	result = result.replace("&gt;", ">");
	result = result.replace("&quot;", "\"");
	result = result.replace("&#x27;", "'");
	result = result.replace("&apos;", "'");
	result = result.replace("&nbsp;", " ");
	return result.strip_edges();
}

String AIWebSearch::parse_search_results(const String &p_response_body) const {
	// Parse DuckDuckGo HTML search results.
	// Results are in elements with class="result__body" or similar.
	// Titles are in <a class="result__a"> tags.
	// Snippets are in <a class="result__snippet"> tags.
	// URLs are in <span class="result__url"> tags.

	String result;
	int count = 0;
	const int MAX_RESULTS = 8;

	int search_pos = 0;
	while (count < MAX_RESULTS) {
		// Find result title link: <a class="result__a"
		int title_start = p_response_body.find("class=\"result__a\"", search_pos);
		if (title_start < 0) break;

		// Find href in this tag.
		// Go backwards to find the <a tag start.
		int tag_start = p_response_body.rfind("<a", title_start);
		String href;
		if (tag_start >= 0) {
			int href_pos = p_response_body.find("href=\"", tag_start);
			if (href_pos >= 0 && href_pos < title_start + 50) {
				int href_end = p_response_body.find("\"", href_pos + 6);
				if (href_end > href_pos) {
					href = p_response_body.substr(href_pos + 6, href_end - href_pos - 6);
					// DuckDuckGo redirects through their URL, extract actual URL.
					int uddg_pos = href.find("uddg=");
					if (uddg_pos >= 0) {
						href = href.substr(uddg_pos + 5).uri_decode();
						// Remove any trailing parameters after &
						int amp_pos = href.find("&");
						if (amp_pos >= 0) {
							href = href.left(amp_pos);
						}
					}
				}
			}
		}

		// Extract title text (between > and </a>).
		int title_tag_end = p_response_body.find(">", title_start);
		String title_text;
		if (title_tag_end >= 0) {
			int title_close = p_response_body.find("</a>", title_tag_end);
			if (title_close > title_tag_end) {
				title_text = _strip_tags(p_response_body.substr(title_tag_end + 1, title_close - title_tag_end - 1));
			}
		}

		// Find snippet: <a class="result__snippet"
		String snippet_text;
		int snippet_start = p_response_body.find("class=\"result__snippet\"", title_start);
		if (snippet_start >= 0 && snippet_start < title_start + 2000) {
			int snippet_tag_end = p_response_body.find(">", snippet_start);
			if (snippet_tag_end >= 0) {
				int snippet_close = p_response_body.find("</a>", snippet_tag_end);
				if (snippet_close > snippet_tag_end) {
					snippet_text = _strip_tags(p_response_body.substr(snippet_tag_end + 1, snippet_close - snippet_tag_end - 1));
				}
			}
		}

		if (!title_text.is_empty()) {
			count++;
			result += itos(count) + ". **" + title_text + "**\n";
			if (!href.is_empty()) {
				result += "   URL: " + href + "\n";
			}
			if (!snippet_text.is_empty()) {
				result += "   " + snippet_text + "\n";
			}
			result += "\n";
		}

		search_pos = title_start + 20;
	}

	if (result.is_empty()) {
		return "No search results found. Try a different query or use [FETCH_URL: <url>] to visit a specific page directly.\n";
	}

	return "## Search Results (" + itos(count) + " found)\n\n" + result;
}

String AIWebSearch::extract_text_from_html(const String &p_html) const {
	String text;
	bool in_tag = false;
	bool in_script = false;
	bool in_style = false;
	String tag_name;
	int consecutive_whitespace = 0;

	for (int i = 0; i < p_html.length(); i++) {
		char32_t c = p_html[i];

		if (c == '<') {
			in_tag = true;
			tag_name = "";
			continue;
		}

		if (in_tag) {
			if (c == '>') {
				in_tag = false;
				String lower_tag = tag_name.to_lower().strip_edges();
				if (lower_tag == "script" || lower_tag.begins_with("script ")) {
					in_script = true;
				} else if (lower_tag == "/script") {
					in_script = false;
				} else if (lower_tag == "style" || lower_tag.begins_with("style ")) {
					in_style = true;
				} else if (lower_tag == "/style") {
					in_style = false;
				} else if (lower_tag == "br" || lower_tag == "br/" || lower_tag == "br /" ||
						   lower_tag == "p" || lower_tag == "/p" ||
						   lower_tag.begins_with("h1") || lower_tag.begins_with("h2") ||
						   lower_tag.begins_with("h3") || lower_tag.begins_with("h4") ||
						   lower_tag == "/h1" || lower_tag == "/h2" ||
						   lower_tag == "/h3" || lower_tag == "/h4" ||
						   lower_tag == "li" || lower_tag == "/li" ||
						   lower_tag == "tr" || lower_tag == "/tr" ||
						   lower_tag == "div" || lower_tag == "/div") {
					text += "\n";
					consecutive_whitespace = 0;
				}
			} else {
				tag_name += c;
			}
			continue;
		}

		if (in_script || in_style) continue;

		// Handle HTML entities.
		if (c == '&') {
			String entity;
			int j = i + 1;
			while (j < p_html.length() && j < i + 10 && p_html[j] != ';') {
				entity += p_html[j];
				j++;
			}
			if (j < p_html.length() && p_html[j] == ';') {
				if (entity == "amp") { text += "&"; }
				else if (entity == "lt") { text += "<"; }
				else if (entity == "gt") { text += ">"; }
				else if (entity == "quot") { text += "\""; }
				else if (entity == "nbsp") { text += " "; }
				else if (entity == "apos") { text += "'"; }
				else { text += "?"; }
				i = j;
				consecutive_whitespace = 0;
				continue;
			}
		}

		// Collapse whitespace.
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			consecutive_whitespace++;
			if (consecutive_whitespace <= 1) {
				text += " ";
			}
		} else {
			consecutive_whitespace = 0;
			text += c;
		}
	}

	// Collapse multiple blank lines.
	while (text.find("\n\n\n") >= 0) {
		text = text.replace("\n\n\n", "\n\n");
	}

	// Trim to reasonable length (max ~8000 chars for context).
	if (text.length() > 8000) {
		text = text.left(8000) + "\n\n[Content truncated at 8000 characters]";
	}

	return text.strip_edges();
}

String AIWebSearch::sanitize_url(const String &p_url) const {
	String url = p_url.strip_edges();
	if (!url.begins_with("http://") && !url.begins_with("https://")) {
		url = "https://" + url;
	}
	return url;
}

AIWebSearch::AIWebSearch() {
}
