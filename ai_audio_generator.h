#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

// Generates audio using AI APIs and saves as Godot audio resources.
class AIAudioGenerator : public RefCounted {
	GDCLASS(AIAudioGenerator, RefCounted);

protected:
	static void _bind_methods();

public:
	// Build OpenAI TTS API request body.
	static String build_tts_request(const String &p_text, const String &p_voice = "alloy", const String &p_model = "tts-1");

	// Save raw audio bytes (mp3/wav) to a file in the project.
	static bool save_audio_bytes(const PackedByteArray &p_data, const String &p_save_path);

	AIAudioGenerator();
};

#endif // TOOLS_ENABLED
