#include "ai_plan.h"

#include "../common/ai_log.h"
#include "ai_response_analysis.h"

#include "core/config/project_settings.h"
#include "core/string/char_utils.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/time.h"

static const char *PLANS_DIR_NAME = "ai_plans";

void AIPlan::clear() {
	steps.clear();
	step_index = 0;
	running = false;
	markdown_path = String();
}

String AIPlan::set_from_text(const String &p_plan_text) {
	return set_steps(AIPlan::parse_plan_steps(p_plan_text));
}

String AIPlan::set_steps(const Vector<String> &p_steps) {
	clear();
	steps = p_steps;
	if (steps.is_empty()) {
		return String();
	}

	const String project_dir = ProjectSettings::get_singleton()->get_resource_path();
	if (project_dir.is_empty()) {
		return String();
	}
	const String plans_dir = project_dir.path_join(PLANS_DIR_NAME);
	const String generated_at = Time::get_singleton()->get_datetime_string_from_system();
	const String file_name = "plan_" + generated_at.replace("-", "").replace(":", "").replace("T", "_") + ".md";
	if (DirAccess::make_dir_recursive_absolute(plans_dir) != OK) {
		AI_WARN("[Plan] Could not create " + plans_dir + "; steps will not be ticked off.");
		return String();
	}
	Ref<FileAccess> file = FileAccess::open(plans_dir.path_join(file_name), FileAccess::WRITE);
	if (file.is_null()) {
		AI_WARN("[Plan] Could not write the plan file; steps will not be ticked off.");
		return String();
	}
	file->store_string(AIPlan::build_plan_markdown(steps, generated_at));
	markdown_path = plans_dir.path_join(file_name);
	return "res://" + String(PLANS_DIR_NAME) + "/" + file_name;
}

String AIPlan::get_current_step() const {
	return step_index < steps.size() ? steps[step_index] : String();
}

void AIPlan::start() {
	// Steps already ticked off are not run again.
	if (step_index >= steps.size()) {
		step_index = 0;
	}
	running = !steps.is_empty();
}

bool AIPlan::advance() {
	step_index++;
	if (step_index >= steps.size()) {
		running = false;
		steps.clear(); // Carried out; nothing left to execute again.
		step_index = 0;
		return false;
	}
	return true;
}

bool AIPlan::mark_current_step_done(String &r_step_label) {
	if (markdown_path.is_empty()) {
		return false;
	}
	Ref<FileAccess> reader = FileAccess::open(markdown_path, FileAccess::READ);
	if (reader.is_null()) {
		AI_WARN("[Plan] Cannot open plan file: " + markdown_path);
		return false;
	}
	String markdown = reader->get_as_text();
	reader.unref();

	if (!AIPlan::mark_first_unchecked_step(markdown, r_step_label)) {
		return false;
	}
	Ref<FileAccess> writer = FileAccess::open(markdown_path, FileAccess::WRITE);
	if (writer.is_null()) {
		AI_WARN("[Plan] Cannot write plan file: " + markdown_path);
		return false;
	}
	writer->store_string(markdown);
	return true;
}

// --- The checklist as text ---

Vector<String> AIPlan::parse_plan_steps(const String &p_plan_text) {
	Vector<String> steps;
	const Vector<String> lines = p_plan_text.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		int digits = 0;
		while (digits < line.length() && is_digit(line[digits])) {
			digits++;
		}
		if (digits > 0 && digits < line.length() && (line[digits] == '.' || line[digits] == ')')) {
			steps.push_back(line);
		}
	}
	return steps;
}

String AIPlan::build_plan_markdown(const Vector<String> &p_steps, const String &p_generated_at) {
	String md = "# AI Plan\n\nGenerated: " + p_generated_at + "\n\n## Steps\n\n";
	for (int i = 0; i < p_steps.size(); i++) {
		md += "- [ ] " + p_steps[i] + "\n";
	}
	return md;
}

bool AIPlan::mark_first_unchecked_step(String &r_markdown, String &r_step_label) {
	static const String UNCHECKED = "- [ ] ";
	// Steps run in order, so the first unchecked box is always the current one.
	Vector<String> lines = r_markdown.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		if (lines[i].begins_with(UNCHECKED)) {
			const String label = lines[i].substr(UNCHECKED.length());
			r_step_label = label.strip_edges();
			lines.write[i] = "- [x] " + label;
			r_markdown = String("\n").join(lines);
			return true;
		}
	}
	return false;
}
