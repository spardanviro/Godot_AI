#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

// Specialized agent for UI/Control node creation and layout.
class AIUIAgent : public RefCounted {
	GDCLASS(AIUIAgent, RefCounted);

protected:
	static void _bind_methods();

public:
	// Get the UI-specialized system prompt addition.
	static String get_ui_system_prompt();

	// Detect if a user message is UI-related.
	static bool is_ui_request(const String &p_message);

	AIUIAgent();
};

#endif // TOOLS_ENABLED
