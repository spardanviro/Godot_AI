#include "glm_provider.h"

bool GLMProvider::_accepts_listed_model(const String &p_id) const {
	// GLM-5 series chat models only.
	if (!p_id.begins_with("glm-5")) {
		return false;
	}
	// Skip image, audio, video and vision variants.
	if (p_id.contains("v") || p_id.contains("image") || p_id.contains("audio") ||
			p_id.contains("video") || p_id.contains("voice") || p_id.contains("ocr")) {
		return false;
	}
	// Skip dated snapshots.
	return p_id.find("-20") <= 3;
}
