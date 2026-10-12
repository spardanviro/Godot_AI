#pragma once

#include "core/string/ustring.h"

// An image or a piece of speech a reply asked to have generated.
struct AIAssetRequest {
	bool is_audio = false;
	String prompt;
	String save_path; // May be empty; always validated with is_safe_path().

	// Model-supplied save paths must stay inside the project and name a new
	// image or audio file, so generation cannot overwrite project files.
	static bool is_safe_path(const String &p_path);
};
