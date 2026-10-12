#include "ai_project_memory.h"

#include "ai_log.h"
#include "ai_system_prompt.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/os/time.h"

static const char *QUIRKS_SECTION = "## AI-Discovered Quirks";
static const char *QUIRKS_INTRO = "_Quirks recorded automatically by Godot AI during your sessions._";

String AIProjectMemory::insert_note(const String &p_existing_text, const String &p_note, const String &p_date) {
	const String entry = "- [" + p_date + "] " + p_note + "\n";

	const int section_pos = p_existing_text.find(QUIRKS_SECTION);
	if (section_pos >= 0) {
		// Newest first, right under the heading.
		const int heading_end = p_existing_text.find("\n", section_pos);
		if (heading_end < 0) {
			return p_existing_text + "\n" + entry;
		}
		return p_existing_text.left(heading_end + 1) + entry + p_existing_text.substr(heading_end + 1);
	}

	String text = p_existing_text;
	if (!text.is_empty() && !text.ends_with("\n")) {
		text += "\n";
	}
	return text + "\n" + QUIRKS_SECTION + "\n" + QUIRKS_INTRO + "\n\n" + entry;
}

bool AIProjectMemory::append_note(const String &p_note) {
	if (p_note.is_empty() || !ProjectSettings::get_singleton()->is_project_loaded()) {
		return false;
	}
	const String path = AISystemPrompt::PROJECT_INSTRUCTIONS_FILE;

	String existing;
	if (FileAccess::exists(path)) {
		Ref<FileAccess> reader = FileAccess::open(path, FileAccess::READ);
		if (reader.is_null()) {
			AI_WARN("WRITE_MEMORY: " + path + " exists but could not be read; not overwriting it.");
			return false;
		}
		existing = reader->get_as_text();
	}

	Ref<FileAccess> writer = FileAccess::open(path, FileAccess::WRITE);
	if (writer.is_null()) {
		AI_WARN("WRITE_MEMORY: could not write to " + path);
		return false;
	}
	writer->store_string(insert_note(existing, p_note, Time::get_singleton()->get_date_string_from_system()));
	return true;
}
