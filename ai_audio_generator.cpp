#ifdef TOOLS_ENABLED

#include "ai_audio_generator.h"
#include "core/io/json.h"
#include "core/io/file_access.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"

void AIAudioGenerator::_bind_methods() {
}

String AIAudioGenerator::build_tts_request(const String &p_text, const String &p_voice, const String &p_model) {
	Dictionary body;
	body["model"] = p_model;
	body["input"] = p_text;
	body["voice"] = p_voice;
	body["response_format"] = "mp3";

	return JSON::stringify(body, "", false);
}

bool AIAudioGenerator::save_audio_bytes(const PackedByteArray &p_data, const String &p_save_path) {
	if (p_data.is_empty()) {
		return false;
	}

	// Ensure directory exists.
	String dir_path = p_save_path.get_base_dir();
	{
		Ref<DirAccess> dir = DirAccess::open("res://");
		if (dir.is_valid() && !dir->dir_exists(dir_path)) {
			dir->make_dir_recursive(dir_path);
		}
	}

	Ref<FileAccess> f = FileAccess::open(p_save_path, FileAccess::WRITE);
	if (!f.is_valid()) {
		ERR_PRINT("[Godot AI] Failed to open file for writing: " + p_save_path);
		return false;
	}
	f->store_buffer(p_data.ptr(), p_data.size());
	f.unref();

	// Trigger reimport.
	EditorInterface::get_singleton()->get_resource_file_system()->scan();

	print_line("[Godot AI] Audio saved: " + p_save_path);
	return true;
}

AIAudioGenerator::AIAudioGenerator() {
}

#endif // TOOLS_ENABLED
