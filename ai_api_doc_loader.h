#pragma once

#include "ai_knowledge.h"

// Compact API references for the Godot classes a message mentions by name.
// The references themselves are data (knowledge/api_docs.md).
class AIAPIDocLoader {
public:
	// A reference block for every class named in the message, or "".
	static String select(const Vector<AIKnowledge::ApiDoc> &p_docs, const String &p_message);
	// Same, from the loaded knowledge base.
	static String get_docs_for_message(const String &p_message);
};
