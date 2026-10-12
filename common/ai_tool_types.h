#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

// A tool as the model is told about it. Parameters are a JSON Schema object.
struct AIToolSpec {
	String name;
	String description;
	Dictionary parameters;

	enum Kind {
		KIND_READ, // Looks, changes nothing: needs no permission and nothing to undo.
		KIND_EDIT, // Changes the scene or a file; subject to the user's permissions.
		KIND_HOST, // Takes time or the user (running the game, a script): carried out by the agent loop.
	};
	Kind kind = KIND_READ;

	bool is_read_only() const { return kind == KIND_READ; }
};

// One request by the model to run a tool, as it arrived.
struct AIToolCall {
	String id; // The provider's id for the call; the result is sent back under it.
	String name;
	String arguments; // JSON object, as text.
	// Something the provider attached to the call and wants back with it,
	// untouched (Gemini's thought signature). Empty for most.
	String provider_data;
};

// Puts a tool call together from the fragments a stream delivers it in: the
// id and the name come once, the arguments a few characters at a time, and
// several calls can be interleaved, told apart by their index.
class AIToolCallAccumulator {
	Vector<AIToolCall> calls;

public:
	void add(int p_index, const String &p_id, const String &p_name, const String &p_arguments) {
		static const int MAX_CALLS = 32; // An index far beyond what a reply holds is not a call.
		if (p_index < 0 || p_index >= MAX_CALLS) {
			return;
		}
		if (calls.size() <= p_index) {
			calls.resize(p_index + 1);
		}
		AIToolCall &call = calls.write[p_index];
		if (!p_id.is_empty()) {
			call.id = p_id;
		}
		if (!p_name.is_empty()) {
			call.name = p_name;
		}
		call.arguments += p_arguments;
	}

	// A call that arrives in one piece (Gemini). It gets an id if it has none.
	void add_whole(const String &p_id, const String &p_name, const String &p_arguments, const String &p_provider_data) {
		AIToolCall call;
		call.id = p_id.is_empty() ? "call_" + itos(calls.size()) : p_id;
		call.name = p_name;
		call.arguments = p_arguments;
		call.provider_data = p_provider_data;
		calls.push_back(call);
	}

	// The calls that were named. A fragment without a name is not one.
	Vector<AIToolCall> get_calls() const {
		Vector<AIToolCall> complete;
		for (const AIToolCall &call : calls) {
			if (!call.name.is_empty()) {
				complete.push_back(call);
			}
		}
		return complete;
	}

	void clear() { calls.clear(); }
};
