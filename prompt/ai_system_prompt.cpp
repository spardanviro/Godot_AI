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

String AISystemPrompt::get_base_prompt() {
	String project_instructions;
	Ref<FileAccess> f = FileAccess::open(PROJECT_INSTRUCTIONS_FILE, FileAccess::READ);
	if (f.is_valid()) {
		project_instructions = f->get_as_text();
	}
	return build(AIKnowledge::get_singleton()->get_system_prompt(), project_instructions);
}
