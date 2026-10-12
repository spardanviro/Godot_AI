#include "ai_asset_request.h"

#include "core/io/file_access.h"

bool AIAssetRequest::is_safe_path(const String &p_path) {
	static const char *asset_extensions[] = { "png", "jpg", "jpeg", "webp", "mp3", "wav", "ogg", nullptr };
	if (p_path.is_empty()) {
		return true;
	}
	if (!p_path.begins_with("res://") || p_path.find("..") >= 0) {
		return false;
	}
	const String extension = p_path.get_extension().to_lower();
	for (int i = 0; asset_extensions[i]; i++) {
		if (extension == asset_extensions[i]) {
			return true;
		}
	}
	return false;
}
