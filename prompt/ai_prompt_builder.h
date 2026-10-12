#pragma once

#ifdef TOOLS_ENABLED

#include "ai_context_collector.h"
#include "../execution/ai_error_monitor.h"
#include "ai_profiler_collector.h"

// Assembles what goes to the model around the conversation itself: the system
// prompt (static knowledge + whatever the message calls for + the current
// state of the editor) and the note that accompanies a screenshot.
class AIPromptBuilder {
	Ref<AIContextCollector> context_collector;
	Ref<AIProfilerCollector> profiler_collector;
	Ref<AIErrorMonitor> error_monitor;

public:
	// The part that depends only on the knowledge base and the message: base
	// prompt, optional mode instructions (a prompts/ id, or ""), and the
	// domain guidance, API notes and gotchas the message calls for.
	static String build_knowledge_prompt(const String &p_mode_prompt_id, const String &p_message);

	// The full system prompt: the above plus scene, selection, open scripts,
	// performance and recent errors.
	String build_system_prompt(const String &p_mode_prompt_id, const String &p_message) const;

	// Source of the scripts being edited, re-injected after history compression.
	String get_open_scripts_snapshot() const;

	// Loads a captured screenshot. With p_supports_images the PNG comes back in
	// r_png_base64 (downscaled); either way the returned note tells the model
	// what it is, or is not, looking at. Empty if the image cannot be loaded.
	static String prepare_screenshot(const String &p_image_path, bool p_supports_images, String &r_png_base64);

	// The user's message with queued attachments and the screenshot note
	// appended. r_screenshot_dropped: there was a screenshot, but the provider
	// cannot take images.
	static String build_user_message(const String &p_message, const Vector<String> &p_attachments, const String &p_screenshot_path,
			bool p_supports_images, String &r_png_base64, bool &r_screenshot_dropped);

	// p_error_monitor supplies the recent console and game errors.
	void set_error_monitor(const Ref<AIErrorMonitor> &p_error_monitor) { error_monitor = p_error_monitor; }

	AIPromptBuilder();
};

#endif // TOOLS_ENABLED
