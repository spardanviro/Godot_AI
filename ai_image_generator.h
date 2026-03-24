#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class HTTPRequest;

// Generates images using AI APIs (DALL-E, etc.) and saves them as Godot textures.
class AIImageGenerator : public RefCounted {
	GDCLASS(AIImageGenerator, RefCounted);

protected:
	static void _bind_methods();

public:
	// Generate an image from a text prompt. Returns the GDScript code to call the API
	// and save the result. Since image generation is async HTTP, we provide helper code
	// that the system prompt teaches the AI to use as markers.
	//
	// The actual generation is handled by intercepting [GENERATE_IMAGE] markers in the
	// AI response and performing the HTTP call from C++.

	// Build DALL-E API request body.
	static String build_dalle_request(const String &p_prompt, const String &p_size = "1024x1024", const String &p_quality = "standard");

	// Parse DALL-E response and return base64 image data.
	static String parse_dalle_response(const String &p_response_body);

	// Save base64 PNG data as a texture file in the project.
	static bool save_base64_as_texture(const String &p_base64_data, const String &p_save_path);

	AIImageGenerator();
};

#endif // TOOLS_ENABLED
