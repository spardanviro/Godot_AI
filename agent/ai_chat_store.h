#pragma once

#include "ai_session.h"

// Saved conversations: one JSON file per chat under user://, named by chat id
// (a timestamp, so file order is chronological order).
class AIChatStore {
public:
	struct Entry {
		String path;
		String title;
		int message_count = 0;
	};

	static String get_directory();
	// Does nothing for an empty or unnamed session. Returns false on I/O failure.
	static bool save(const AISession &p_session);
	// Leaves r_session untouched when the file is missing or not a chat.
	static bool load(const String &p_path, AISession &r_session);
	static void remove(const String &p_path);
	// Newest first.
	static Vector<Entry> list();
	static String get_latest_path();
};
