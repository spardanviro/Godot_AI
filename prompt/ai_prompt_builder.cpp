#ifdef TOOLS_ENABLED

#include "ai_prompt_builder.h"

#include "ai_api_doc_loader.h"
#include "ai_domain_prompts.h"
#include "ai_gotchas_index.h"
#include "ai_knowledge.h"
#include "ai_system_prompt.h"

#include "core/crypto/crypto_core.h"
#include "core/io/image.h"

static const int SCREENSHOT_MAX_SIDE = 1568; // Vision models downscale beyond this anyway.
static const int RECENT_ERROR_COUNT = 10;
static const int SNAPSHOT_SCRIPT_COUNT = 3;
static const int SNAPSHOT_SCRIPT_CHARS = 4000;

static bool _mentions_any(const String &p_lower_message, const char *const *p_words) {
	for (int i = 0; p_words[i]; i++) {
		if (p_lower_message.find(String::utf8(p_words[i])) >= 0) {
			return true;
		}
	}
	return false;
}

AIPromptBuilder::AIPromptBuilder() {
	context_collector.instantiate();
	profiler_collector.instantiate();
}

String AIPromptBuilder::build_knowledge_prompt(const String &p_mode_prompt_id, const String &p_message) {
	// The base prompt is the same for every request and comes first, so that
	// providers can cache it; what changes (mode, topic, project) follows.
	String prompt = AISystemPrompt::get_base_prompt();
	if (!p_mode_prompt_id.is_empty()) {
		prompt += "\n\n" + AIKnowledge::get_singleton()->get_prompt(p_mode_prompt_id) + "\n";
	}

	// Selected by what the message talks about, so the token cost stays
	// proportional to the topic.
	prompt += AIDomainPrompts::build_contextual_prompt(p_message);
	// API notes and gotchas are indexed by engine names; the glossary adds
	// the names a message refers to without spelling them ("跳跃", "补间").
	const String expanded = AIKnowledge::get_singleton()->expand_message(p_message);
	prompt += AIAPIDocLoader::get_docs_for_message(expanded);
	const String gotchas = AIGotchasIndex::get_for_message(expanded);
	if (!gotchas.is_empty()) {
		prompt += "\n" + gotchas;
	}
	return prompt;
}

String AIPromptBuilder::build_system_prompt(const String &p_mode_prompt_id, const String &p_message) const {
	static const char *performance_words[] = { "performance", "optimize", "fps", "性能", "优化", nullptr };
	static const char *error_words[] = { "error", "fix", "bug", "warning", "debug", "错误", "修复", "警告", nullptr };

	String prompt = build_knowledge_prompt(p_mode_prompt_id, p_message);
	prompt += context_collector->build_context_prompt();

	const String performance = context_collector->get_performance_snapshot();
	if (!performance.is_empty()) {
		prompt += "\n--- " + performance + "---\n";
	}

	const String lower = p_message.to_lower();
	if (_mentions_any(lower, performance_words)) {
		// These are the editor's own numbers: the game runs in another process.
		prompt += "\n--- EDITOR PERFORMANCE SNAPSHOT (the editor process itself, NOT the running game; do not base advice about the game's frame rate on it) ---\n" + profiler_collector->collect_performance_snapshot() + "--- END EDITOR PERFORMANCE SNAPSHOT ---\n";
	}

	// Errors are always included when the user asks about them, and otherwise
	// only when there are some.
	const bool asks_about_errors = _mentions_any(lower, error_words);
	if (asks_about_errors || error_monitor->get_recent_error_count() > 0) {
		prompt += "\n--- RECENT CONSOLE ERRORS (editor, not caused by AI) ---\n" + error_monitor->get_recent_console_errors(RECENT_ERROR_COUNT) + "\n--- END CONSOLE ERRORS ---\n";
	}
	if (asks_about_errors || error_monitor->get_debugger_error_count() > 0) {
		const String debugger_errors = error_monitor->get_recent_debugger_errors(RECENT_ERROR_COUNT);
		if (!debugger_errors.is_empty()) {
			prompt += "\n--- GAME RUNTIME ERRORS (from running the game, shown in debugger panel) ---\n" + debugger_errors + "\n--- END GAME RUNTIME ERRORS ---\n";
		}
	}
	return prompt;
}

String AIPromptBuilder::get_open_scripts_snapshot() const {
	return context_collector->get_open_scripts_snapshot(SNAPSHOT_SCRIPT_COUNT, SNAPSHOT_SCRIPT_CHARS);
}

String AIPromptBuilder::build_user_message(const String &p_message, const Vector<String> &p_attachments, const String &p_screenshot_path,
		bool p_supports_images, String &r_png_base64, bool &r_screenshot_dropped) {
	String message = p_message;
	if (!p_attachments.is_empty()) {
		message += "\n\n--- ATTACHED CONTEXT ---\n";
		for (int i = 0; i < p_attachments.size(); i++) {
			message += p_attachments[i] + "\n";
		}
		message += "--- END ATTACHED CONTEXT ---\n";
	}

	r_screenshot_dropped = false;
	if (!p_screenshot_path.is_empty()) {
		const String note = prepare_screenshot(p_screenshot_path, p_supports_images, r_png_base64);
		message += note;
		r_screenshot_dropped = !note.is_empty() && !p_supports_images;
	}
	return message;
}

String AIPromptBuilder::prepare_screenshot(const String &p_image_path, bool p_supports_images, String &r_png_base64) {
	Ref<Image> image;
	image.instantiate();
	if (image->load(p_image_path) != OK || image->is_empty()) {
		return String();
	}
	if (!p_supports_images) {
		return "\n\n[The user captured a screenshot of the editor viewport, but the current provider does not accept images, so you cannot see it. Say so if the answer depends on it.]\n";
	}
	const int longest = MAX(image->get_width(), image->get_height());
	if (longest > SCREENSHOT_MAX_SIDE) {
		const float scale = (float)SCREENSHOT_MAX_SIDE / (float)longest;
		image->resize(MAX(1, (int)(image->get_width() * scale)), MAX(1, (int)(image->get_height() * scale)), Image::INTERPOLATE_LANCZOS);
	}
	const PackedByteArray png = image->save_png_to_buffer();
	r_png_base64 = CryptoCore::b64_encode_str(png.ptr(), png.size());
	return "\n\n[A screenshot of the editor viewport (" + itos(image->get_width()) + "x" + itos(image->get_height()) + ") is attached to this message.]\n";
}

#endif // TOOLS_ENABLED
