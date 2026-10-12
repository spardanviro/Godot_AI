#include "ai_api_doc_loader.h"

#include "ai_text_match.h"

String AIAPIDocLoader::select(const Vector<AIKnowledge::ApiDoc> &p_docs, const String &p_message) {
	String result;
	for (int i = 0; i < p_docs.size(); i++) {
		// Class names are case-sensitive: "Node" the class, not "node" the word.
		if (ai_contains_word(p_message, p_docs[i].class_name)) {
			result += p_docs[i].text;
		}
	}
	if (result.is_empty()) {
		return String();
	}
	return "\n## API Reference (auto-injected for classes mentioned in your message)\n" + result + "\n";
}

String AIAPIDocLoader::get_docs_for_message(const String &p_message) {
	return select(AIKnowledge::get_singleton()->get_api_docs(), p_message);
}
