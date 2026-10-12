#include "glm_provider.h"

#include "ai_model_names.h"

bool GLMProvider::_accepts_listed_model(const String &p_id) const {
	// Chat models of the GLM-5 generation and later. The list also carries
	// image, video and embedding models under other names.
	static const int OLDEST_GENERATION = 5000;
	if (AIModelNames::version_after(p_id, "glm-") < OLDEST_GENERATION) {
		return false;
	}
	// Skip image, audio, video and vision variants.
	if (p_id.contains("v") || p_id.contains("image") || p_id.contains("audio") ||
			p_id.contains("video") || p_id.contains("voice") || p_id.contains("ocr")) {
		return false;
	}
	return !AIModelNames::is_dated_snapshot(p_id);
}
