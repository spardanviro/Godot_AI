#pragma once

#include "ai_knowledge.h"

// On-demand gotcha lookup: given a message, returns the entries whose keywords
// it mentions, most relevant first, within a fixed size budget. The entries
// themselves are data (knowledge/gotchas.md).
class AIGotchasIndex {
public:
	// Selects from the loaded knowledge base.
	static String get_for_message(const String &p_message);
	// Selects from the given entries (pure).
	static String select(const Vector<AIKnowledge::Gotcha> &p_entries, const String &p_message);

	// Whether a keyword is specific enough to take part in matching.
	static bool is_useful_keyword(const String &p_keyword);

	static const int BUDGET_CHARS = 3000;

private:
	// Per-entry lowercase keywords with an inverse-document-frequency weight: a
	// keyword only a handful of entries carry says far more about relevance
	// than one shared by fifty.
	struct Index {
		Vector<Vector<String>> keywords;
		Vector<Vector<float>> weights;
	};

	static Index _build_index(const Vector<AIKnowledge::Gotcha> &p_entries);
	static String _select(const Vector<AIKnowledge::Gotcha> &p_entries, const Index &p_index, const String &p_message);
};
