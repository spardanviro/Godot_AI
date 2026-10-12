#pragma once

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

// Everything the assistant "knows" that is not code: the system prompt, the
// prompt templates for each situation, per-domain guidance, gotchas and API
// notes. All of it is plain Markdown under knowledge/.
//
// The files are embedded in the binary as defaults. A directory on disk can
// override them file by file (same relative path) and add new domain or gotcha
// files, and is re-read when it changes — so a prompt can be edited and tried
// without rebuilding the engine, and users can keep their own customisations.
//
//   system_prompt.md        Base prompt. {{project_instructions}} marks where
//                           the project's godot_ai.md goes.
//   prompts/<id>.md         Templates with {{name}} placeholders.
//   domains/<id>.md         Front matter (id, order, match, keywords) + body.
//   gotchas.md, gotchas/*   Entries introduced by <!-- keywords: a, b -->.
//   api_docs.md             One "## ClassName" section per class.
class AIKnowledge {
public:
	struct Gotcha {
		Vector<String> keywords;
		String content;
	};

	struct Domain {
		String id;
		int order = 100; // Lower comes first in the prompt.
		// false: keywords are stems, anchored on the left only ("tile" matches
		// "tiles", "tilemap"). true: whole words, with an optional plural "s".
		bool whole_word = false;
		Vector<String> keywords; // Lowercase.
		String body;
	};

	// Terms a message may use (in any language) and the engine names they
	// stand for. See knowledge/glossary.md.
	struct GlossaryEntry {
		Vector<String> terms; // Lowercase.
		String names; // Space-separated.
	};

	struct ApiDoc {
		String class_name;
		String text;
	};

	static AIKnowledge *get_singleton();
	static void free_singleton();

	// Directory whose files override the built-in ones ("" for none).
	void set_override_dir(const String &p_dir);
	const String &get_override_dir() const { return override_dir; }
	// GODOT_AI_KNOWLEDGE_DIR, else the editor setting, else the "knowledge"
	// folder in the editor config directory if it exists.
	static String resolve_override_dir();

	void reload();
	// Reloads if the override directory (or its choice) changed. Cheap enough
	// to call before every request.
	bool reload_if_changed();
	// Bumped on every reload, so dependants can invalidate what they derived.
	uint64_t get_generation() const { return generation; }

	const String &get_system_prompt() const { return system_prompt; }
	bool has_prompt(const String &p_id) const { return prompts.has(p_id); }
	// Template text, or "" (with an error logged) for an unknown id.
	String get_prompt(const String &p_id) const;
	String format_prompt(const String &p_id, const Dictionary &p_values = Dictionary()) const;
	const Vector<Gotcha> &get_gotchas() const { return gotchas; }
	const Vector<Domain> &get_domains() const { return domains; }
	const Vector<ApiDoc> &get_api_docs() const { return api_docs; }
	const Vector<GlossaryEntry> &get_glossary() const { return glossary; }
	// The message followed by the engine names its glossary terms stand
	// for. Only for selecting knowledge; never shown or sent as the message.
	String expand_message(const String &p_message) const { return expand_with_glossary(glossary, p_message); }

	// Relative paths that came from the override directory.
	const Vector<String> &get_overridden_files() const { return overridden_files; }
	const Vector<String> &get_warnings() const { return warnings; }

	// Writes the built-in files to a directory as a starting point for edits.
	// Existing files are left alone. Returns the number of files written.
	int export_builtin(const String &p_dir) const;

	// --- Format parsers (pure) ---
	static String normalize_text(const String &p_text);
	static Vector<Gotcha> parse_gotchas(const String &p_text);
	static bool parse_domain(const String &p_text, const String &p_default_id, Domain &r_domain, String &r_error);
	static Vector<ApiDoc> parse_api_docs(const String &p_text);
	static Vector<GlossaryEntry> parse_glossary(const String &p_text);
	static String expand_with_glossary(const Vector<GlossaryEntry> &p_glossary, const String &p_message);
	// Replaces every {{name}} that has a value; unknown placeholders are kept.
	static String format(const String &p_template, const Dictionary &p_values);

private:
	static AIKnowledge *singleton;

	String override_dir;
	uint64_t override_signature = 0;
	uint64_t generation = 0;

	String system_prompt;
	HashMap<String, String> prompts;
	Vector<Gotcha> gotchas;
	Vector<Domain> domains;
	Vector<ApiDoc> api_docs;
	Vector<GlossaryEntry> glossary;
	Vector<String> overridden_files;
	Vector<String> warnings;

	static HashMap<String, String> _read_builtin_files();
	static void _read_dir_files(const String &p_root, const String &p_relative, HashMap<String, String> &r_files);
	static uint64_t _compute_signature(const String &p_dir);
	void _build(const HashMap<String, String> &p_files);
};
