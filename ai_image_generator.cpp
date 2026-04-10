#ifdef TOOLS_ENABLED

#include "ai_image_generator.h"
#include "core/object/class_db.h"
#include "core/io/json.h"
#include "core/io/file_access.h"
#include "core/crypto/crypto_core.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"

void AIImageGenerator::_bind_methods() {
}

String AIImageGenerator::build_dalle_request(const String &p_prompt, const String &p_size, const String &p_quality) {
	Dictionary body;
	body["model"] = "dall-e-3";
	body["prompt"] = p_prompt;
	body["n"] = 1;
	body["size"] = p_size;
	body["response_format"] = "b64_json";
	body["quality"] = p_quality;

	return JSON::stringify(body, "", false);
}

String AIImageGenerator::parse_dalle_response(const String &p_response_body) {
	Ref<JSON> json;
	json.instantiate();
	Error err = json->parse(p_response_body);
	if (err != OK) {
		return "";
	}

	Dictionary data = json->get_data();
	if (data.has("error")) {
		Dictionary error = data["error"];
		ERR_PRINT("[Godot AI] DALL-E error: " + String(error.get("message", "Unknown")));
		return "";
	}

	if (!data.has("data")) {
		return "";
	}

	Array items = data["data"];
	if (items.is_empty()) {
		return "";
	}

	Dictionary item = items[0];
	return item.get("b64_json", "");
}

bool AIImageGenerator::save_base64_as_texture(const String &p_base64_data, const String &p_save_path) {
	if (p_base64_data.is_empty()) {
		return false;
	}

	// Decode base64 to raw bytes.
	CharString b64_utf8 = p_base64_data.utf8();
	int decoded_len = b64_utf8.length() / 4 * 3 + 4;
	PackedByteArray png_data;
	png_data.resize(decoded_len);
	size_t out_len = 0;
	Error err = CryptoCore::b64_decode(png_data.ptrw(), png_data.size(), &out_len,
			(const uint8_t *)b64_utf8.get_data(), b64_utf8.length());
	if (err != OK) {
		ERR_PRINT("[Godot AI] Failed to decode base64 image data.");
		return false;
	}
	png_data.resize(out_len);

	// Ensure directory exists.
	String dir_path = p_save_path.get_base_dir();
	{
		Ref<DirAccess> dir = DirAccess::open("res://");
		if (dir.is_valid() && !dir->dir_exists(dir_path)) {
			dir->make_dir_recursive(dir_path);
		}
	}

	// Save PNG file.
	Ref<FileAccess> f = FileAccess::open(p_save_path, FileAccess::WRITE);
	if (!f.is_valid()) {
		ERR_PRINT("[Godot AI] Failed to open file for writing: " + p_save_path);
		return false;
	}
	f->store_buffer(png_data.ptr(), png_data.size());
	f.unref();

	// Trigger reimport.
	EditorInterface::get_singleton()->get_resource_filesystem()->scan();

	print_line("[Godot AI] Image saved: " + p_save_path);
	return true;
}

AIImageGenerator::AIImageGenerator() {
}

#endif // TOOLS_ENABLED
