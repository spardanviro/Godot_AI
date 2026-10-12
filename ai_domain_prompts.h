#pragma once

#include "ai_knowledge.h"

// Domain-specific guidance (animation, tilemaps, UI, ...) that is added to the
// prompt only when the user's message is about that domain. The domains
// themselves are data (knowledge/domains/*.md).
class AIDomainPrompts {
public:
	// Whether a lowercase message mentions one of the domain's keywords.
	static bool matches(const AIKnowledge::Domain &p_domain, const String &p_lower_message);
	// Ids of the matching domains, in prompt order.
	static Vector<String> detect_domains(const Vector<AIKnowledge::Domain> &p_domains, const String &p_message);
	// Concatenated bodies of the matching domains.
	static String build_contextual_prompt(const Vector<AIKnowledge::Domain> &p_domains, const String &p_message);
	// Same, from the loaded knowledge base.
	static String build_contextual_prompt(const String &p_message);
};
