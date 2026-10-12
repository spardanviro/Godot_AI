#include "ai_system_prompt.h"

#include "ai_knowledge.h"

#include "core/io/file_access.h"

String AISystemPrompt::build(const String &p_template, const String &p_project_instructions) {
	String block;
	if (!p_project_instructions.strip_edges().is_empty()) {
		block = "## Project Instructions (from godot_ai.md)\n" + p_project_instructions + "\n\n";
	}
	return p_template.replace(PROJECT_INSTRUCTIONS_PLACEHOLDER, block);
}

String AISystemPrompt::select_variant(const String &p_template, Variant p_variant) {
	static const char *SCRIPT_BEGIN = "<!-- SCRIPT-ONLY -->";
	static const char *TOOLS_BEGIN = "<!-- TOOLS-ONLY -->";
	static const char *MARKERS_BEGIN = "<!-- MARKERS-ONLY -->";
	static const char *END_PREFIX = "<!-- END ";
	static const char *END_SUFFIX = "-ONLY -->";

	String out;
	bool skipping = false;
	const PackedStringArray lines = p_template.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String marker = lines[i].strip_edges();
		if (marker == SCRIPT_BEGIN) {
			skipping = p_variant == VARIANT_TOOLS;
			continue;
		}
		if (marker == TOOLS_BEGIN) {
			skipping = p_variant != VARIANT_TOOLS;
			continue;
		}
		if (marker == MARKERS_BEGIN) {
			skipping = p_variant != VARIANT_SCRIPT;
			continue;
		}
		if (marker.begins_with(END_PREFIX) && marker.ends_with(END_SUFFIX)) {
			skipping = false;
			continue;
		}
		if (!skipping) {
			out += lines[i] + (i + 1 < lines.size() ? "\n" : "");
		}
	}
	return out;
}

String AISystemPrompt::get_base_prompt(Variant p_variant) {
	String project_instructions;
	Ref<FileAccess> f = FileAccess::open(PROJECT_INSTRUCTIONS_FILE, FileAccess::READ);
	if (f.is_valid()) {
		project_instructions = f->get_as_text();
	}
	return build(select_variant(AIKnowledge::get_singleton()->get_system_prompt(), p_variant), project_instructions);
}
