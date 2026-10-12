#ifdef TOOLS_ENABLED

// What AIAgentController does with a reply once it has arrived: run its code,
// ask for a correction, record a plan, or follow up with web data. The state
// machine itself (runs, requests, stopping) is in ai_agent_controller.cpp.

#include "ai_agent_controller.h"
#include "../common/ai_settings.h"
#include "../common/ai_code_text.h"

#include "../assets/ai_asset_service.h"
#include "../prompt/ai_knowledge.h"
#include "../common/ai_log.h"
#include "ai_project_memory.h"
#include "../providers/ai_provider_setup.h"
#include "../network/ai_web_requester.h"

#include "core/object/class_db.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/timer.h"

// Errors raised by deferred calls and signal callbacks arrive after the code returns.
static const double VERIFY_DELAY_SEC = 0.5;
static const int STEP_LABEL_CHARS = 60;

Dictionary AIAgentController::_prompt_values(const String &p_key, const Variant &p_value) {
	Dictionary values;
	values[p_key] = p_value;
	return values;
}

// =============================================================================
// Replies
// =============================================================================

// Splits a reply into prose and executable code blocks.
Vector<String> AIAgentController::_parse_reply(const String &p_response, PackedStringArray &r_text_segments) const {
	const Dictionary parsed = response_parser->parse(p_response);
	r_text_segments = PackedStringArray(parsed["text_segments"]);
	Vector<String> code_blocks;
	const Array raw_blocks = parsed["code_blocks"];
	for (int i = 0; i < raw_blocks.size(); i++) {
		code_blocks.push_back(raw_blocks[i]);
	}
	return AICodeText::dedupe_consecutive(code_blocks);
}

void AIAgentController::_emit_response(const PackedStringArray &p_text_segments, const Vector<String> &p_code_blocks, bool p_show_text, bool p_stopped) {
	PackedStringArray blocks;
	for (int i = 0; i < p_code_blocks.size(); i++) {
		blocks.push_back(p_code_blocks[i]);
	}
	Dictionary response;
	response["text_segments"] = p_text_segments;
	response["code_blocks"] = blocks;
	response["streamed"] = run.mode == MODE_ASK && run.kind == RUN_USER_TURN;
	response["show_text"] = p_show_text;
	response["stopped"] = p_stopped;
	emit_signal(SNAME("response_received"), response);
}

void AIAgentController::_handle_response(const String &p_response, const String &p_thinking, bool p_trusted) {
	session.push_assistant(p_response, p_thinking);
	run.response = p_response;
	run.overflow_retry_used = false;
	_compress_if_needed();

	PackedStringArray text_segments;
	const Vector<String> code_blocks = _parse_reply(p_response, text_segments);
	run.markers = AIResponseAnalysis::parse_markers(p_response);
	AI_LOG("Reply: " + itos(text_segments.size()) + " text segments, " + itos(code_blocks.size()) + " code blocks");

	if (code_blocks.is_empty()) {
		_handle_text_only(text_segments);
		return;
	}

	// A plan is a list of steps. Code the model put into it anyway is not
	// what the user asked for in this mode, and is not run.
	if (run.kind == RUN_USER_TURN && run.mode == MODE_PLAN) {
		_handle_plan(text_segments);
		return;
	}

	_emit_response(text_segments, code_blocks, false, false);
	if (!p_trusted) {
		// Shown, kept in the history, never executed.
		post_notice(NOTICE_WARNING, String(U"⚠ ") + run.untrusted_reason);
		_fail_run(run.untrusted_reason);
		return;
	}
	// ASK mode answers in text; a runtime fix always applies its code.
	if (run.mode == MODE_ASK && run.kind != RUN_RUNTIME_FIX) {
		AI_LOG("ASK mode: code is shown, not executed.");
		_dispatch_side_effects();
		_settle();
		return;
	}
	_handle_code(code_blocks[code_blocks.size() - 1]);
}

void AIAgentController::_handle_text_only(const PackedStringArray &p_text_segments) {
	if (run.kind == RUN_USER_TURN && run.mode == MODE_PLAN) {
		_handle_plan(p_text_segments);
		return;
	}

	// Outside ASK mode a reply without code usually means the model described
	// the work instead of doing it. Exceptions: the user was just chatting, the
	// reply asks for web data first, or we invited an explanation ourselves.
	if (!run.untrusted_reason.is_empty()) {
		// Asking again would only get cut off or withheld again.
		_emit_response(p_text_segments, Vector<String>(), run.mode != MODE_ASK, false);
		post_notice(NOTICE_WARNING, String(U"⚠ ") + run.untrusted_reason);
		if (run.mode == MODE_ASK && run.kind == RUN_USER_TURN) {
			_settle(); // A cut-off explanation is still an explanation.
		} else {
			_fail_run(run.untrusted_reason);
		}
		return;
	}

	const bool expects_code = run.mode != MODE_ASK && run.tool_calls == 0 && !run.response.strip_edges().is_empty() &&
			!run.markers.has_web_request() && !run.explanation_invited &&
			(run.kind != RUN_USER_TURN || !AIResponseAnalysis::is_conversational_input(run.user_input));

	// Asked once. A second answer in text is the model's considered reply
	// (a refusal, a question back), not an oversight to keep pushing on.
	if (expects_code && !run.no_code_nudged && run.retries < MAX_AUTO_RETRIES) {
		run.no_code_nudged = true;
		_emit_response(p_text_segments, Vector<String>(), false, false);
		_retry_or_fail("retry_no_code", Dictionary(), String(U"⚠ ") + TR(STR_NO_CODE_BLOCK_RETRY), String());
		return;
	}

	_emit_response(p_text_segments, Vector<String>(), run.mode != MODE_ASK, false);
	if (expects_code && run.kind != RUN_USER_TURN) {
		// An automatic run that never produced code has not done its job.
		const String message = AI_L("The model did not produce code for this step.", "模型没有为该步骤生成代码。");
		post_notice(NOTICE_ERROR, message);
		_fail_run(message);
		return;
	}
	_dispatch_side_effects();
	// A fix made with tools went in like one made with code: run the scene again.
	run.settled_clean = run.settled_clean || run.tool_edits > 0;
	_settle();
}

void AIAgentController::_handle_plan(const PackedStringArray &p_text_segments) {
	const String plan_file = plan.set_from_text(String("\n").join(p_text_segments));
	if (!plan_file.is_empty()) {
		EditorInterface::get_singleton()->get_resource_filesystem()->scan();
	}

	PackedStringArray steps;
	for (int i = 0; i < plan.get_steps().size(); i++) {
		steps.push_back(plan.get_steps()[i]);
	}
	Dictionary report;
	report["text_segments"] = p_text_segments;
	report["steps"] = steps;
	report["file"] = plan_file;
	emit_signal(SNAME("plan_ready"), report);
	_end_run(OUTCOME_COMPLETED);
}

void AIAgentController::_handle_code(const String &p_code) {
	// "Ask" permissions always ask. With auto-execute off everything asks,
	// a runtime fix included: what it reacts to is text printed by the
	// running project, which is not the user speaking.
	const bool autorun = AISettings::get("autorun", true);
	const AICodeRunner::Review review = code_runner.review(p_code, !autorun);

	switch (review.action) {
		case AICodeRunner::ACTION_FIX_SYNTAX: {
			AI_WARN("Compile check failed:\n" + review.message);
			run.last_error = review.message;
			_retry_or_fail("retry_compile_error", _prompt_values("error", review.message),
					AI_L("⚠ GDScript syntax error - auto-fixing...", "⚠ GDScript 语法错误，自动修复中..."),
					AI_L("GDScript parse errors (auto-fix limit reached):\n", "GDScript 解析错误（已达自动修复上限）：\n") + review.message);
		} break;
		case AICodeRunner::ACTION_FIX_SAFETY: {
			AI_WARN("Safety check blocked the code: " + review.message);
			post_notice(NOTICE_ERROR, review.message);
			run.last_error = review.message;
			// The retry prompt offers a way out: explain why it cannot be done.
			run.explanation_invited = true;
			_retry_or_fail("retry_safety", _prompt_values("error", review.message),
					AI_L("Asking for a version that stays within the safety policy...", "正在请求符合安全策略的版本..."),
					AI_L("The code was not executed: it violates the safety policy.", "代码未执行：违反安全策略。"));
		} break;
		case AICodeRunner::ACTION_REFUSE: {
			AI_WARN("Code refused: " + review.message);
			post_notice(NOTICE_ERROR, review.message);
			_fail_run(review.message);
		} break;
		case AICodeRunner::ACTION_CONFIRM: {
			if (auto_approve) {
				_execute(review.code);
				break;
			}
			run.code = review.code;
			_set_state(STATE_AWAITING_PERMISSION);
			emit_signal(SNAME("permission_requested"), review.message);
		} break;
		case AICodeRunner::ACTION_RUN: {
			_execute(review.code);
		} break;
	}
}

void AIAgentController::_execute(const String &p_code) {
	if (_stopped_meanwhile()) {
		return;
	}
	AI_LOG("---------- CODE EXECUTION ----------");
	AICodeRunner::Result result = code_runner.execute(p_code);
	run.executions++;
	run.last_execution_ok = result.success;
	run.last_error = result.error;

	// Code that failed half-way has already changed things. Put them back,
	// so that the corrected version does not add everything a second time.
	String state_note;
	if (!result.success) {
		if (result.checkpoint_id >= 0 && code_runner.get_checkpoint_manager()->restore_checkpoint(result.checkpoint_id)) {
			result.checkpoint_id = -1; // Used up.
			state_note = "The scene and the project files were restored to how they were before this code ran. Write the complete corrected code.";
		} else {
			state_note = "Part of the code may have run before the error, and its changes are still there. Check what already exists (get_node_or_null(), FileAccess.file_exists()) instead of adding it a second time.";
		}
	}

	Dictionary report;
	report["success"] = result.success;
	report["output"] = result.output;
	report["error"] = result.error;
	report["summary"] = result.summary;
	report["code"] = p_code;
	report["response"] = run.response;
	report["checkpoint_id"] = result.checkpoint_id;
	report["compact"] = run.mode != MODE_ASK;
	const uint64_t serial = run_serial;
	emit_signal(SNAME("execution_finished"), report);
	if (serial != run_serial) {
		code_runner.take_late_errors(); // A listener ended the run; stop monitoring.
		return;
	}

	if (!result.success) {
		AI_ERR("Execution failed: " + result.error);
		Dictionary values = _prompt_values("error", result.error);
		values["state_note"] = state_note;
		_retry_or_fail("retry_exec_error", values,
				AI_L("Auto-retrying...", "自动重试中..."),
				AI_L("Auto-recovery failed after ", "自动恢复失败，已尝试 ") + itos(MAX_AUTO_RETRIES) + AI_L(" attempts.", " 次。"));
		return;
	}
	AI_LOG("Execution succeeded: " + result.output);

	if (run.kind == RUN_PLAN_STEP && !run.plan_step_marked) {
		// Once per step, even if a later fix runs more code for it.
		run.plan_step_marked = true;
		_mark_plan_step_done();
	}
	_dispatch_side_effects();

	// Errors raised by deferred calls and signal callbacks arrive after the
	// code returns; give them a moment before calling the result clean.
	_set_state(STATE_VERIFYING);
	verify_timer->start(VERIFY_DELAY_SEC);
}

void AIAgentController::_on_verify_timeout() {
	if (state != STATE_VERIFYING) {
		return;
	}
	const String errors = code_runner.take_late_errors();
	if (errors.is_empty()) {
		run.settled_clean = true;
		_settle();
		return;
	}

	AI_WARN("Errors raised after execution:\n" + errors);
	post_notice(NOTICE_WARNING, AI_L("Post-execution warnings/errors detected:\n", "执行后检测到警告或错误：\n") + errors);
	run.last_error = errors;
	if (run.retries < MAX_AUTO_RETRIES) {
		_retry_or_fail("retry_deferred_errors", _prompt_values("errors", errors), AI_L("Auto-fixing...", "自动修复中..."), String());
		return;
	}
	// Out of retries: the code did run, so carry on, but not as a clean result.
	_settle();
}

// Side effects a reply asked for through markers. Only honoured for a reply
// that was accepted — not for one that is being regenerated or was stopped.
void AIAgentController::_dispatch_side_effects() {
	for (int i = 0; i < run.markers.assets.size(); i++) {
		asset_service->enqueue(run.markers.assets[i]);
	}
	run.markers.assets.clear();

	// Notes go into a file that is loaded into every future system prompt, so
	// they follow the file-write permission, are reduced to one inert line and
	// are always shown to the user.
	const bool may_write = code_runner.may_write_files();
	for (int i = 0; i < run.markers.memory_notes.size() && may_write; i++) {
		const String note = AIResponseAnalysis::sanitize_memory_note(run.markers.memory_notes[i]);
		if (AIProjectMemory::append_note(note)) {
			post_notice(NOTICE_INFO, AI_L("Saved to godot_ai.md: ", "已写入 godot_ai.md：") + note);
		}
	}
	run.markers.memory_notes.clear();
}

// =============================================================================
// Plan and web follow-ups
// =============================================================================

void AIAgentController::_start_plan_step() {
	const String step_text = plan.get_current_step();
	const int step_count = plan.get_steps().size();
	post_notice(NOTICE_INFO, AI_L("🔧 Executing step ", "🔧 正在执行步骤 ") + itos(plan.get_step_index() + 1) + "/" + itos(step_count) + ": " + step_text);

	Dictionary values;
	values["step_num"] = plan.get_step_index() + 1;
	values["total_steps"] = step_count;
	values["step_text"] = step_text;

	_begin_run(RUN_PLAN_STEP, MODE_AGENT, String());
	_send_request(AIKnowledge::get_singleton()->format_prompt(_edits_with_tools() ? "plan_step_tools" : "plan_step", values));
}

void AIAgentController::_mark_plan_step_done() {
	String label;
	if (!plan.mark_current_step_done(label)) {
		return;
	}
	EditorInterface::get_singleton()->get_resource_filesystem()->scan();
	post_notice(NOTICE_SUCCESS, AI_L("✅ Step ", "✅ 步骤 ") + itos(plan.get_step_index() + 1) + AI_L(" done: ", " 完成：") +
					(label.length() > STEP_LABEL_CHARS ? label.left(STEP_LABEL_CHARS) + "..." : label));
}

// Returns false when no request was started (rejected, or over the limit);
// the caller then carries on settling.
bool AIAgentController::_start_web_request() {
	const AIResponseAnalysis::Markers markers = run.markers;
	run.markers.web_search_query = String();
	run.markers.fetch_url = String();

	// Each result is fed back to the model, whose reply may ask for another;
	// without a cap a page could keep the loop going indefinitely.
	if (run.web_requests >= MAX_WEB_REQUESTS_PER_RUN) {
		post_notice(NOTICE_WARNING, AI_L("Web request skipped: limit of ", "已跳过网络请求：已达每条消息 ") + itos(MAX_WEB_REQUESTS_PER_RUN) + AI_L(" per message reached.", " 次的上限。"));
		return false;
	}
	// The state is set first: start() reports through signals, and a listener
	// that stops the run from there must find it in the state it is stopping.
	const uint64_t serial = run_serial;
	run.web_requests++;
	_set_state(STATE_WEB_REQUEST);
	if (!web_requester->start(markers.web_search_query, markers.fetch_url)) {
		return serial != run_serial; // Not started; "handled" only if the run is gone.
	}
	if (serial != run_serial) {
		web_requester->cancel();
	}
	return true;
}

void AIAgentController::_on_web_request_finished(const String &p_content) {
	if (state != STATE_WEB_REQUEST) {
		return; // Stopped meanwhile.
	}
	if (p_content.is_empty()) {
		// Nothing to feed back; finish as if no web data had been asked for.
		_settle();
		return;
	}
	post_notice(NOTICE_INFO, AI_L("✅ Web results received. Analyzing...", "✅ 已收到网页结果，分析中..."));
	// The message that hands the fetched content back to the model.
	const bool was_fetch = web_requester->was_fetch();
	Dictionary values;
	values["action"] = was_fetch ? "FETCH" : "SEARCH";
	values["subject_label"] = was_fetch ? "URL" : "Query";
	values["subject"] = web_requester->get_subject();
	values["content"] = p_content;
	_send_request(AIKnowledge::get_singleton()->format_prompt("web_results", values));
}

#endif // TOOLS_ENABLED
