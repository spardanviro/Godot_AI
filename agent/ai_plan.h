#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// A numbered plan produced in PLAN mode and the progress of carrying it out.
// The steps are also kept as a Markdown checklist in res://ai_plans/, so the
// plan can be read, committed and followed outside the chat.
class AIPlan {
	Vector<String> steps;
	int step_index = 0;
	bool running = false;
	String markdown_path;

public:
	void clear();
	// Replaces the plan with the numbered steps found in p_plan_text and writes
	// the checklist file. Returns the res:// path of the file, or "" if there
	// were no steps or it could not be written.
	String set_from_text(const String &p_plan_text);

	bool is_empty() const { return steps.is_empty(); }
	bool is_running() const { return running; }
	const Vector<String> &get_steps() const { return steps; }
	int get_step_index() const { return step_index; }
	String get_current_step() const;

	// Begins at the first step, or resumes at the step a previous attempt stopped at.
	void start();
	void stop() { running = false; }
	// Moves to the next step. Returns false, and stops, after the last one.
	bool advance();
	// Ticks the current step off in the checklist file. Returns false if there
	// is no file or no unchecked step left.
	bool mark_current_step_done(String &r_step_label);

	// --- The checklist as text ---
	// Lines of a plan that start with "N." or "N)".
	static Vector<String> parse_plan_steps(const String &p_plan_text);
	static String build_plan_markdown(const Vector<String> &p_steps, const String &p_generated_at);
	// Ticks the first unchecked "- [ ] " box. Returns false if there is none.
	static bool mark_first_unchecked_step(String &r_markdown, String &r_step_label);
};
