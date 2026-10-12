#include "ai_chat_store.h"

#include "ai_log.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/templates/hash_map.h"

static const char *CHATS_DIR = "user://ai_assistant_chats";
static const char *CHAT_EXTENSION = ".json";

static bool _read_chat_file(const String &p_path, Dictionary &r_data) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return false;
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(file->get_as_text()) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return false;
	}
	r_data = json->get_data();
	return true;
}

String AIChatStore::get_directory() {
	return CHATS_DIR;
}

bool AIChatStore::save(const AISession &p_session) {
	if (p_session.is_empty() || p_session.get_chat_id().is_empty()) {
		return true;
	}
	DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(CHATS_DIR));
	const String path = String(CHATS_DIR).path_join(p_session.get_chat_id() + CHAT_EXTENSION);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	if (file.is_null()) {
		AI_ERR("Failed to save chat: " + path);
		return false;
	}
	file->store_string(JSON::stringify(p_session.to_dict(), "\t"));
	return true;
}

bool AIChatStore::load(const String &p_path, AISession &r_session) {
	Dictionary data;
	if (!_read_chat_file(p_path, data)) {
		AI_ERR("Failed to read chat file: " + p_path);
		return false;
	}
	AISession loaded;
	if (!loaded.from_dict(data)) {
		AI_ERR("Not a chat file: " + p_path);
		return false;
	}
	r_session = loaded;
	return true;
}

void AIChatStore::remove(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::open(CHATS_DIR);
	if (dir.is_valid()) {
		// Only ever a file of the chats directory, whatever path was passed in.
		dir->remove(p_path.get_file());
	}
}

Vector<AIChatStore::Entry> AIChatStore::list() {
	Vector<Entry> entries;
	Ref<DirAccess> dir = DirAccess::open(CHATS_DIR);
	if (dir.is_null()) {
		return entries;
	}
	Vector<String> files;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (!dir->current_is_dir() && name.ends_with(CHAT_EXTENSION)) {
			files.push_back(name);
		}
	}
	dir->list_dir_end();
	files.sort();
	files.reverse();

	// A chat file holds the whole conversation; parsing every one of them
	// each time the list opens is wasted work for files that did not change.
	struct Cached {
		uint64_t modified = 0;
		Entry entry;
	};
	static HashMap<String, Cached> cache;

	for (int i = 0; i < files.size(); i++) {
		const String path = String(CHATS_DIR).path_join(files[i]);
		const uint64_t modified = FileAccess::get_modified_time(path);
		const Cached *known = cache.getptr(path);
		if (known && known->modified == modified && modified != 0) {
			entries.push_back(known->entry);
			continue;
		}
		Dictionary data;
		Entry entry;
		entry.path = path;
		if (!_read_chat_file(entry.path, data)) {
			continue;
		}
		entry.title = data.get("title", "Untitled");
		entry.message_count = data.get("message_count", 0);
		Cached fresh;
		fresh.modified = modified;
		fresh.entry = entry;
		cache[path] = fresh;
		entries.push_back(entry);
	}
	return entries;
}

String AIChatStore::get_latest_path() {
	Ref<DirAccess> dir = DirAccess::open(CHATS_DIR);
	if (dir.is_null()) {
		return String();
	}
	String latest;
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (!dir->current_is_dir() && name.ends_with(CHAT_EXTENSION) && name > latest) {
			latest = name;
		}
	}
	dir->list_dir_end();
	return latest.is_empty() ? String() : String(CHATS_DIR).path_join(latest);
}
