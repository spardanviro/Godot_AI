#include "ai_knowledge.h"
#include "../common/ai_settings.h"

#include "ai_knowledge_data.gen.h"

#include "core/io/compression.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_paths.h"
#endif

static const char *SYSTEM_PROMPT_FILE = "system_prompt.md";
static const char *GOTCHAS_FILE = "gotchas.md";
static const char *API_DOCS_FILE = "api_docs.md";
static const char *GLOSSARY_FILE = "glossary.md";
static const char *PROMPTS_DIR = "prompts/";
static const char *DOMAINS_DIR = "domains/";
static const char *GOTCHAS_DIR = "gotchas/";
static const char *KNOWLEDGE_EXTENSION = "md";
static const char *GOTCHA_MARKER = "<!-- keywords:";
static const char *FRONT_MATTER_FENCE = "---";

AIKnowledge *AIKnowledge::singleton = nullptr;

AIKnowledge *AIKnowledge::get_singleton() {
	if (!singleton) {
		singleton = memnew(AIKnowledge);
		singleton->override_dir = resolve_override_dir();
		singleton->reload();
	}
	return singleton;
}

void AIKnowledge::free_singleton() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

String AIKnowledge::resolve_override_dir() {
	const String from_env = OS::get_singleton()->get_environment("GODOT_AI_KNOWLEDGE_DIR");
	if (!from_env.is_empty()) {
		return from_env;
	}
#ifdef TOOLS_ENABLED
	const String from_setting = String(AISettings::get("knowledge_dir", "")).strip_edges();
	if (!from_setting.is_empty()) {
		return from_setting;
	}
	if (EditorPaths::get_singleton()) {
		const String default_dir = EditorPaths::get_singleton()->get_config_dir().path_join("ai_assistant").path_join("knowledge");
		if (DirAccess::dir_exists_absolute(default_dir)) {
			return default_dir;
		}
	}
#endif
	return String();
}

void AIKnowledge::set_override_dir(const String &p_dir) {
	override_dir = p_dir;
	reload();
}

String AIKnowledge::normalize_text(const String &p_text) {
	return p_text.replace("\r\n", "\n");
}

HashMap<String, String> AIKnowledge::_read_builtin_files() {
	HashMap<String, String> files;
	if (_ai_knowledge_uncompressed_size <= 0) {
		return files;
	}
	Vector<uint8_t> blob;
	blob.resize(_ai_knowledge_uncompressed_size);
	const int64_t size = Compression::decompress(blob.ptrw(), blob.size(), _ai_knowledge_compressed, _ai_knowledge_compressed_size, Compression::MODE_DEFLATE);
	ERR_FAIL_COND_V_MSG(size != _ai_knowledge_uncompressed_size, files, "[Godot AI] Built-in knowledge data is corrupt.");

	for (int i = 0; _ai_knowledge_files[i].path; i++) {
		const AIKnowledgeBuiltinFile &entry = _ai_knowledge_files[i];
		files[entry.path] = String::utf8((const char *)blob.ptr() + entry.offset, entry.size);
	}
	return files;
}

void AIKnowledge::_read_dir_files(const String &p_root, const String &p_relative, HashMap<String, String> &r_files) {
	Ref<DirAccess> dir = DirAccess::open(p_root.path_join(p_relative));
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		const String relative = p_relative.is_empty() ? name : p_relative + "/" + name;
		if (dir->current_is_dir()) {
			_read_dir_files(p_root, relative, r_files);
		} else if (name.get_extension().to_lower() == KNOWLEDGE_EXTENSION) {
			Ref<FileAccess> f = FileAccess::open(p_root.path_join(relative), FileAccess::READ);
			if (f.is_valid()) {
				r_files[relative] = normalize_text(f->get_as_utf8_string());
			}
		}
	}
	dir->list_dir_end();
}

static void _collect_signature_entries(const String &p_root, const String &p_relative, Vector<String> &r_entries) {
	Ref<DirAccess> dir = DirAccess::open(p_root.path_join(p_relative));
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		const String relative = p_relative.is_empty() ? name : p_relative + "/" + name;
		if (dir->current_is_dir()) {
			_collect_signature_entries(p_root, relative, r_entries);
		} else {
			const String full_path = p_root.path_join(relative);
			r_entries.push_back(relative + "|" + itos(FileAccess::get_modified_time(full_path)) + "|" + itos(FileAccess::get_size(full_path)));
		}
	}
	dir->list_dir_end();
}

uint64_t AIKnowledge::_compute_signature(const String &p_dir) {
	if (p_dir.is_empty()) {
		return 0;
	}
	Vector<String> entries;
	_collect_signature_entries(p_dir, String(), entries);
	entries.sort();
	uint64_t signature = p_dir.hash64();
	for (int i = 0; i < entries.size(); i++) {
		signature = signature * 1099511628211ULL ^ entries[i].hash64();
	}
	return signature;
}

void AIKnowledge::reload() {
	HashMap<String, String> files = _read_builtin_files();

	overridden_files.clear();
	warnings.clear();
	if (!override_dir.is_empty()) {
		HashMap<String, String> override_files;
		_read_dir_files(override_dir, String(), override_files);
		for (const KeyValue<String, String> &kv : override_files) {
			files[kv.key] = kv.value;
			overridden_files.push_back(kv.key);
		}
		overridden_files.sort();
	}
	override_signature = _compute_signature(override_dir);

	_build(files);
	generation++;

	for (int i = 0; i < warnings.size(); i++) {
		WARN_PRINT("[Godot AI] Knowledge: " + warnings[i]);
	}
}

bool AIKnowledge::reload_if_changed() {
	const String dir = resolve_override_dir();
	if (dir == override_dir && _compute_signature(dir) == override_signature) {
		return false;
	}
	override_dir = dir;
	reload();
	return true;
}

struct _DomainSorter {
	bool operator()(const AIKnowledge::Domain &p_a, const AIKnowledge::Domain &p_b) const {
		return p_a.order == p_b.order ? p_a.id < p_b.id : p_a.order < p_b.order;
	}
};

void AIKnowledge::_build(const HashMap<String, String> &p_files) {
	system_prompt = String();
	prompts.clear();
	gotchas.clear();
	domains.clear();
	api_docs.clear();
	glossary.clear();

	// Sorted so the result does not depend on directory listing order.
	Vector<String> paths;
	for (const KeyValue<String, String> &kv : p_files) {
		paths.push_back(kv.key);
	}
	paths.sort();

	for (int i = 0; i < paths.size(); i++) {
		const String &path = paths[i];
		const String &text = p_files[path];

		if (path == SYSTEM_PROMPT_FILE) {
			system_prompt = text;
		} else if (path.begins_with(PROMPTS_DIR)) {
			// Editors add a trailing newline; templates never want one.
			prompts[path.get_file().get_basename()] = text.strip_edges(false, true);
		} else if (path.begins_with(DOMAINS_DIR)) {
			Domain domain;
			String error;
			if (parse_domain(text, path.get_file().get_basename(), domain, error)) {
				domains.push_back(domain);
			} else {
				warnings.push_back(path + ": " + error);
			}
		} else if (path == GOTCHAS_FILE || path.begins_with(GOTCHAS_DIR)) {
			const Vector<Gotcha> parsed = parse_gotchas(text);
			if (parsed.is_empty()) {
				warnings.push_back(path + ": no gotcha entries found (each needs a '" + String(GOTCHA_MARKER) + " ... -->' line).");
			}
			gotchas.append_array(parsed);
		} else if (path == API_DOCS_FILE) {
			api_docs = parse_api_docs(text);
		} else if (path == GLOSSARY_FILE) {
			glossary = parse_glossary(text);
		}
	}
	domains.sort_custom<_DomainSorter>();

	if (system_prompt.is_empty()) {
		warnings.push_back(String(SYSTEM_PROMPT_FILE) + " is missing or empty.");
	}
}

String AIKnowledge::get_prompt(const String &p_id) const {
	const String *found = prompts.getptr(p_id);
	ERR_FAIL_NULL_V_MSG(found, String(), "[Godot AI] Unknown prompt template: " + p_id);
	return *found;
}

String AIKnowledge::format_prompt(const String &p_id, const Dictionary &p_values) const {
	return format(get_prompt(p_id), p_values);
}

int AIKnowledge::export_builtin(const String &p_dir) const {
	int written = 0;
	const HashMap<String, String> files = _read_builtin_files();
	for (const KeyValue<String, String> &kv : files) {
		const String path = p_dir.path_join(kv.key);
		if (FileAccess::exists(path)) {
			continue;
		}
		DirAccess::make_dir_recursive_absolute(path.get_base_dir());
		Ref<FileAccess> f = FileAccess::open(path, FileAccess::WRITE);
		if (f.is_valid()) {
			f->store_string(kv.value);
			written++;
		}
	}
	return written;
}

// --- Parsers ---

Vector<AIKnowledge::Gotcha> AIKnowledge::parse_gotchas(const String &p_text) {
	Vector<Gotcha> entries;
	const String text = normalize_text(p_text);
	const String marker = GOTCHA_MARKER;

	// Markers only count at the start of a line.
	int pos = 0;
	if (!text.begins_with(marker)) {
		const int first = text.find("\n" + marker);
		pos = first < 0 ? -1 : first + 1;
	}
	while (pos >= 0 && pos < text.length()) {
		int line_end = text.find("\n", pos);
		if (line_end < 0) {
			line_end = text.length();
		}
		const String header = text.substr(pos + marker.length(), line_end - pos - marker.length());
		const int next_marker = text.find("\n" + marker, line_end);
		const int content_start = MIN(line_end + 1, text.length());
		const int content_end = next_marker < 0 ? text.length() : next_marker + 1;

		Gotcha entry;
		const Vector<String> keywords = header.replace("-->", "").split(",");
		for (int i = 0; i < keywords.size(); i++) {
			const String keyword = keywords[i].strip_edges();
			if (!keyword.is_empty()) {
				entry.keywords.push_back(keyword);
			}
		}
		entry.content = text.substr(content_start, content_end - content_start);
		if (!entry.content.strip_edges().is_empty() && !entry.keywords.is_empty()) {
			if (!entry.content.ends_with("\n")) {
				entry.content += "\n\n";
			}
			entries.push_back(entry);
		}
		pos = next_marker < 0 ? -1 : next_marker + 1;
	}
	return entries;
}

bool AIKnowledge::parse_domain(const String &p_text, const String &p_default_id, Domain &r_domain, String &r_error) {
	const String text = normalize_text(p_text);
	const String fence = String(FRONT_MATTER_FENCE) + "\n";
	if (!text.begins_with(fence)) {
		r_error = "missing front matter (the file must start with a '---' line).";
		return false;
	}
	const int close = text.find("\n" + fence, fence.length() - 1);
	if (close < 0) {
		r_error = "front matter is not closed with a '---' line.";
		return false;
	}

	Domain domain;
	domain.id = p_default_id;
	bool in_keyword_list = false;
	const Vector<String> lines = text.substr(fence.length(), close - fence.length() + 1).split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line.is_empty() || line.begins_with("#")) {
			continue;
		}
		if (in_keyword_list && line.begins_with("- ")) {
			const String keyword = line.substr(2).strip_edges().to_lower();
			if (!keyword.is_empty()) {
				domain.keywords.push_back(keyword);
			}
			continue;
		}
		in_keyword_list = false;

		const int colon = line.find(":");
		if (colon < 0) {
			r_error = "front matter line " + itos(i + 1) + " is not 'key: value'.";
			return false;
		}
		const String key = line.left(colon).strip_edges();
		const String value = line.substr(colon + 1).strip_edges();
		if (key == "id") {
			domain.id = value;
		} else if (key == "order") {
			domain.order = value.to_int();
		} else if (key == "match") {
			if (value != "stem" && value != "word") {
				r_error = "match must be 'stem' or 'word'.";
				return false;
			}
			domain.whole_word = value == "word";
		} else if (key == "keywords") {
			in_keyword_list = true;
			// Also accept a comma-separated list on the same line.
			const Vector<String> inline_keywords = value.split(",", false);
			for (int k = 0; k < inline_keywords.size(); k++) {
				const String keyword = inline_keywords[k].strip_edges().to_lower();
				if (!keyword.is_empty()) {
					domain.keywords.push_back(keyword);
				}
			}
		} else {
			r_error = "unknown front matter key '" + key + "'.";
			return false;
		}
	}

	if (domain.keywords.is_empty()) {
		r_error = "no keywords: the domain could never be selected.";
		return false;
	}
	domain.body = text.substr(close + 1 + fence.length());
	if (domain.body.strip_edges().is_empty()) {
		r_error = "empty body.";
		return false;
	}
	r_domain = domain;
	return true;
}

Vector<AIKnowledge::GlossaryEntry> AIKnowledge::parse_glossary(const String &p_text) {
	Vector<GlossaryEntry> entries;
	const Vector<String> lines = normalize_text(p_text).split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		const int arrow = line.find("=>");
		if (line.is_empty() || line.begins_with("#") || arrow < 0) {
			continue;
		}
		GlossaryEntry entry;
		const Vector<String> terms = line.left(arrow).split(",", false);
		for (int j = 0; j < terms.size(); j++) {
			const String term = terms[j].strip_edges().to_lower();
			if (!term.is_empty()) {
				entry.terms.push_back(term);
			}
		}
		const Vector<String> names = line.substr(arrow + 2).split(",", false);
		for (int j = 0; j < names.size(); j++) {
			const String name = names[j].strip_edges();
			if (!name.is_empty()) {
				entry.names += (entry.names.is_empty() ? "" : " ") + name;
			}
		}
		if (!entry.terms.is_empty() && !entry.names.is_empty()) {
			entries.push_back(entry);
		}
	}
	return entries;
}

String AIKnowledge::expand_with_glossary(const Vector<GlossaryEntry> &p_glossary, const String &p_message) {
	const String lower = p_message.to_lower();
	String names;
	for (int i = 0; i < p_glossary.size(); i++) {
		for (int j = 0; j < p_glossary[i].terms.size(); j++) {
			if (lower.find(p_glossary[i].terms[j]) >= 0) {
				names += " " + p_glossary[i].names;
				break;
			}
		}
	}
	return names.is_empty() ? p_message : p_message + "\n" + names;
}

Vector<AIKnowledge::ApiDoc> AIKnowledge::parse_api_docs(const String &p_text) {
	static const String HEADING = "## ";
	Vector<ApiDoc> docs;
	const Vector<String> lines = normalize_text(p_text).split("\n");
	bool in_section = false;
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].begins_with(HEADING)) {
			ApiDoc doc;
			doc.class_name = lines[i].substr(HEADING.length()).strip_edges();
			docs.push_back(doc);
			in_section = true;
		} else if (in_section && !(i == lines.size() - 1 && lines[i].is_empty())) {
			docs.write[docs.size() - 1].text += lines[i] + "\n";
		}
	}
	return docs;
}

String AIKnowledge::format(const String &p_template, const Dictionary &p_values) {
	// Single pass: text substituted in is never scanned for placeholders again,
	// so values that happen to contain "{{...}}" cannot trigger substitution.
	String result;
	int pos = 0;
	while (pos < p_template.length()) {
		const int open = p_template.find("{{", pos);
		const int close = open < 0 ? -1 : p_template.find("}}", open + 2);
		if (open < 0 || close < 0) {
			result += p_template.substr(pos);
			break;
		}
		const String name = p_template.substr(open + 2, close - open - 2).strip_edges();
		result += p_template.substr(pos, open - pos);
		if (p_values.has(name)) {
			result += String(p_values[name]);
		} else {
			result += p_template.substr(open, close + 2 - open);
		}
		pos = close + 2;
	}
	return result;
}
