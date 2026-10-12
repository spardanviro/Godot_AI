#ifdef TOOLS_ENABLED

#include "ai_agent_controller.h"

#include "ai_asset_service.h"
#include "ai_chat_store.h"
#include "ai_history_summarizer.h"
#include "ai_knowledge.h"
#include "ai_log.h"
#include "ai_project_memory.h"
#include "ai_provider_setup.h"
#include "ai_web_requester.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/main/timer.h"

static const double STREAM_POLL_INTERVAL_SEC = 0.05;
static const int FALLBACK_CONTEXT_LENGTH = 8192;
static const int FALLBACK_MAX_OUTPUT_TOKENS = 4096;

// Gemini carries the API key in the query string; never print it.
static String _redact_url_key(const String &p_url) {
	const int key_pos = p_url.find("key=");
	if (key_pos < 0) {
		return p_url;
	}
	int key_end = p_url.find("&", key_pos);
	if (key_end < 0) {
		key_end = p_url.length();
	}
	return p_url.left(key_pos + 4) + "****" + p_url.substr(key_end);
}

void AIAgentController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("submit_user_message", "message", "raw_input", "mode"), &AIAgentController::submit_user_message);
	ClassDB::bind_method(D_METHOD("stop"), &AIAgentController::stop);
	ClassDB::bind_method(D_METHOD("execute_plan"), &AIAgentController::execute_plan);
	ClassDB::bind_method(D_METHOD("confirm_permission", "allowed"), &AIAgentController::confirm_permission);
	ClassDB::bind_method(D_METHOD("get_state"), &AIAgentController::get_state);
	ClassDB::bind_method(D_METHOD("is_busy"), &AIAgentController::is_busy);
	ClassDB::bind_method(D_METHOD("has_pending_plan"), &AIAgentController::has_pending_plan);
	ClassDB::bind_method(D_METHOD("get_last_run_report"), &AIAgentController::get_last_run_report);
	ClassDB::bind_method(D_METHOD("new_session"), &AIAgentController::new_session);
	ClassDB::bind_method(D_METHOD("reload_settings"), &AIAgentController::reload_settings);
	ClassDB::bind_method(D_METHOD("configure_provider", "provider_id", "model", "endpoint", "api_key"), &AIAgentController::configure_provider);
	ClassDB::bind_method(D_METHOD("set_auto_approve", "enabled"), &AIAgentController::set_auto_approve);
	ClassDB::bind_method(D_METHOD("set_pending_screenshot", "image_path"), &AIAgentController::set_pending_screenshot);

	ADD_SIGNAL(MethodInfo("state_changed", PropertyInfo(Variant::INT, "state")));
	ADD_SIGNAL(MethodInfo("notice", PropertyInfo(Variant::INT, "level"), PropertyInfo(Variant::STRING, "text")));
	// A request went out. show_content: the reply is meant to be read as it streams.
	ADD_SIGNAL(MethodInfo("request_started", PropertyInfo(Variant::BOOL, "show_content")));
	ADD_SIGNAL(MethodInfo("stream_delta", PropertyInfo(Variant::STRING, "text"), PropertyInfo(Variant::BOOL, "is_thinking")));
	// The request ended; outcome is an AIStreamClient::Outcome.
	ADD_SIGNAL(MethodInfo("request_finished", PropertyInfo(Variant::INT, "outcome")));
	// { text_segments, code_blocks, streamed, show_text, stopped }
	ADD_SIGNAL(MethodInfo("response_received", PropertyInfo(Variant::DICTIONARY, "response")));
	// { success, output, error, summary, code, response, checkpoint_id, compact }
	ADD_SIGNAL(MethodInfo("execution_finished", PropertyInfo(Variant::DICTIONARY, "report")));
	// { text_segments, steps, file }
	ADD_SIGNAL(MethodInfo("plan_ready", PropertyInfo(Variant::DICTIONARY, "plan")));
	ADD_SIGNAL(MethodInfo("permission_requested", PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("session_changed"));
	// { kind, mode, outcome, requests, retries, executions, executed_ok, error, response }
	ADD_SIGNAL(MethodInfo("run_finished", PropertyInfo(Variant::DICTIONARY, "report")));

	BIND_ENUM_CONSTANT(STATE_IDLE);
	BIND_ENUM_CONSTANT(STATE_STREAMING);
	BIND_ENUM_CONSTANT(STATE_AWAITING_PERMISSION);
	BIND_ENUM_CONSTANT(STATE_VERIFYING);
	BIND_ENUM_CONSTANT(STATE_WEB_REQUEST);
	BIND_ENUM_CONSTANT(MODE_ASK);
	BIND_ENUM_CONSTANT(MODE_AGENT);
	BIND_ENUM_CONSTANT(MODE_PLAN);
	BIND_ENUM_CONSTANT(NOTICE_INFO);
	BIND_ENUM_CONSTANT(NOTICE_SUCCESS);
	BIND_ENUM_CONSTANT(NOTICE_WARNING);
	BIND_ENUM_CONSTANT(NOTICE_ERROR);
}

// =============================================================================
// State machine
// =============================================================================

void AIAgentController::_set_state(State p_state) {
	if (state == p_state) {
		return;
	}
	state = p_state;
	emit_signal(SNAME("state_changed"), (int)state);
}

void AIAgentController::post_notice(NoticeLevel p_level, const String &p_text) {
	emit_signal(SNAME("notice"), (int)p_level, p_text);
}

void AIAgentController::_on_child_notice(int p_level, const String &p_text) {
	post_notice((NoticeLevel)p_level, p_text);
}

void AIAgentController::_begin_run(RunKind p_kind, Mode p_mode, const String &p_user_input) {
	run = Run();
	run_serial++;
	run.kind = p_kind;
	run.mode = p_mode;
	run.user_input = p_user_input;
}

void AIAgentController::_report_run(RunOutcome p_outcome) {
	Dictionary report;
	report["kind"] = (int)run.kind;
	report["mode"] = (int)run.mode;
	report["outcome"] = (int)p_outcome;
	report["requests"] = run.requests;
	report["retries"] = run.retries;
	report["executions"] = run.executions;
	report["executed_ok"] = run.executions > 0 && run.last_execution_ok;
	report["error"] = run.last_error;
	report["response"] = run.response;
	last_run_report = report;
	run = Run();
	run_serial++;
	emit_signal(SNAME("run_finished"), report);
}

void AIAgentController::_end_run(RunOutcome p_outcome) {
	// A plan cannot continue past a step that was stopped or failed.
	if (p_outcome != OUTCOME_COMPLETED && plan.is_running()) {
		plan.stop();
		post_notice(NOTICE_ERROR, AI_L("⛔ Plan execution stopped at step ", "⛔ 计划在第 ") + itos(plan.get_step_index() + 1) + AI_L(".", " 步中止。"));
	}
	save_session();
	_report_run(p_outcome);
	_set_state(STATE_IDLE);
	AI_LOG("========== RUN FINISHED ==========");
}

bool AIAgentController::_stopped_meanwhile() {
	if (!run.stop_requested) {
		return false;
	}
	_end_run(OUTCOME_STOPPED);
	return true;
}

void AIAgentController::_fail_run(const String &p_error) {
	if (!p_error.is_empty()) {
		run.last_error = p_error;
	}
	_end_run(OUTCOME_FAILED);
}

bool AIAgentController::_retry_or_fail(const String &p_prompt_id, const Dictionary &p_values, const String &p_retry_notice, const String &p_failure_notice) {
	if (run.retries >= MAX_AUTO_RETRIES) {
		AI_WARN("Retry budget spent (" + p_prompt_id + ").");
		post_notice(NOTICE_ERROR, p_failure_notice);
		_fail_run(p_failure_notice);
		return false;
	}
	run.retries++;
	AI_LOG("Automatic follow-up " + itos(run.retries) + "/" + itos(MAX_AUTO_RETRIES) + ": " + p_prompt_id);
	post_notice(NOTICE_WARNING, p_retry_notice + " (" + itos(run.retries) + "/" + itos(MAX_AUTO_RETRIES) + ")");
	save_session();
	return _send_request(AIKnowledge::get_singleton()->format_prompt(p_prompt_id, p_values));
}

void AIAgentController::_settle() {
	save_session();

	// The model asked for web data: fetch it and hand it back as a new turn.
	if (run.markers.has_web_request() && _start_web_request()) {
		return;
	}

	if (run.kind == RUN_PLAN_STEP) {
		if (!run.plan_step_marked) {
			// Settled without running code (the model explained instead): the
			// step is still behind us, and the checklist must not fall out of step.
			run.plan_step_marked = true;
			_mark_plan_step_done();
		}
		if (plan.advance()) {
			_report_run(OUTCOME_COMPLETED);
			_start_plan_step();
			return;
		}
		post_notice(NOTICE_SUCCESS, AI_L("✅ All plan steps completed!", "✅ 计划的全部步骤已完成！"));
	} else if (run.kind == RUN_RUNTIME_FIX && run.settled_clean) {
		// The fix went in without complaints: run the same scene again.
		post_notice(NOTICE_INFO, String(U"🔄 ") + TR(STR_WATCH_RESTARTING));
		AI_LOG("[Watch] Fix applied — re-running scene: " + run.restart_scene_path);
		if (run.restart_scene_path.is_empty()) {
			EditorInterface::get_singleton()->play_current_scene();
		} else {
			EditorInterface::get_singleton()->play_custom_scene(run.restart_scene_path);
		}
	}
	_end_run(OUTCOME_COMPLETED);
}

// =============================================================================
// Actions
// =============================================================================

bool AIAgentController::submit_user_message(const String &p_message, const String &p_raw_input, Mode p_mode) {
	if (is_busy() || p_message.strip_edges().is_empty()) {
		return false;
	}
	AI_LOG("========== NEW MESSAGE ==========");
	AI_LOG("User input: " + p_raw_input);

	if (session.get_chat_id().is_empty()) {
		session.set_chat_id(AISession::make_chat_id(Time::get_singleton()->get_datetime_dict_from_system()));
	}
	_begin_run(RUN_USER_TURN, p_mode, p_raw_input);
	_send_request(p_message);
	return true;
}

void AIAgentController::stop() {
	switch (state) {
		case STATE_IDLE:
			break;
		case STATE_STREAMING:
			// Recorded here, not inferred from how the request ends: the reply
			// may already be complete, or the server may answer with an error,
			// and neither may lead to code running or another request going out.
			run.stop_requested = true;
			if (stream.is_busy()) {
				stream.request_stop();
				// Poll now so the UI reacts without waiting for the next tick.
				_on_stream_poll();
			}
			// Otherwise a reply is being handled right now (we were called from
			// one of its signals); the handling stops at its next step.
			break;
		case STATE_AWAITING_PERMISSION:
			confirm_permission(false);
			break;
		case STATE_VERIFYING:
			verify_timer->stop();
			code_runner.take_late_errors(); // Ends error monitoring.
			_end_run(OUTCOME_STOPPED);
			break;
		case STATE_WEB_REQUEST:
			web_requester->cancel();
			_end_run(OUTCOME_STOPPED);
			break;
	}
}

bool AIAgentController::execute_plan() {
	if (is_busy()) {
		return false;
	}
	if (plan.is_empty()) {
		post_notice(NOTICE_WARNING, AI_L("No plan steps to execute. Please generate a plan first.", "没有可执行的计划步骤，请先生成计划。"));
		return false;
	}
	plan.start();
	post_notice(NOTICE_SUCCESS, AI_L("▶ Starting plan execution — ", "▶ 开始执行计划 — ") + itos(plan.get_steps().size()) + AI_L(" steps.", " 个步骤。"));
	_start_plan_step();
	return true;
}

void AIAgentController::confirm_permission(bool p_allowed) {
	if (state != STATE_AWAITING_PERMISSION) {
		return;
	}
	const String code = run.code;
	run.code = String();
	if (!p_allowed) {
		post_notice(NOTICE_WARNING, TR(STR_SYS_EXEC_CANCELLED));
		_end_run(OUTCOME_STOPPED);
		return;
	}
	_execute(code);
}

bool AIAgentController::start_runtime_fix(const String &p_errors, const String &p_scene_path) {
	if (is_busy() || p_errors.is_empty()) {
		return false;
	}

	// Give the model the scripts the errors point at.
	String scripts_section;
	const Vector<String> script_paths = AIResponseAnalysis::extract_script_paths(p_errors);
	for (int i = 0; i < script_paths.size(); i++) {
		Ref<FileAccess> file = FileAccess::open(script_paths[i], FileAccess::READ);
		if (file.is_valid()) {
			scripts_section += "\nFile: " + script_paths[i] + "\n```gdscript\n" + file->get_as_text() + "\n```\n";
		}
	}
	if (!scripts_section.is_empty()) {
		scripts_section = "ERRORED SCRIPTS:\n" + scripts_section + "\n\n";
	}
	Dictionary values;
	values["errors"] = p_errors;
	values["scripts_section"] = scripts_section;

	_begin_run(RUN_RUNTIME_FIX, MODE_AGENT, String());
	run.restart_scene_path = p_scene_path;
	_send_request(AIKnowledge::get_singleton()->format_prompt("runtime_fix", values));
	return true;
}

// =============================================================================
// Requests
// =============================================================================

String AIAgentController::_get_mode_prompt_id() const {
	if (run.kind == RUN_PLAN_STEP) {
		return "mode_plan_step";
	}
	if (run.kind == RUN_USER_TURN && run.mode == MODE_ASK) {
		return "mode_ask";
	}
	if (run.kind == RUN_USER_TURN && run.mode == MODE_PLAN) {
		return "mode_plan";
	}
	return String();
}

bool AIAgentController::_send_request(const String &p_message, bool p_resend) {
	// One request at a time, by construction: every caller is a transition of
	// the state machine, and none of them runs while the stream is busy.
	ERR_FAIL_COND_V(stream.is_busy(), false);
	if (_stopped_meanwhile()) {
		return false;
	}

	if (provider.is_null()) {
		post_notice(NOTICE_ERROR, TR(STR_SYS_NO_PROVIDER));
		_fail_run("No provider configured.");
		return false;
	}
	if (provider->get_api_key().is_empty()) {
		post_notice(NOTICE_ERROR, TR(STR_SYS_NO_API_KEY));
		_fail_run("API key not set.");
		return false;
	}

	// Pick up edits to the knowledge files without a restart.
	AIKnowledge::get_singleton()->reload_if_changed();

	const String system_prompt = prompt_builder.build_system_prompt(_get_mode_prompt_id(), p_message);
	last_system_prompt_tokens = AISession::estimate_tokens(system_prompt);
	AI_LOG("System prompt: " + itos(system_prompt.length()) + " chars (~" + itos(last_system_prompt_tokens) + " tokens)");

	String full_message = p_message;
	bool screenshot_dropped = false;
	if (!p_resend) {
		// The image is kept for the run: the text of the message says a
		// screenshot is attached, so a resend has to attach it again.
		run.request_image_b64 = String();
		// What the user queued goes out with the user's message. A screenshot
		// taken while the loop is working waits for their next one.
		const bool is_users_message = run.kind == RUN_USER_TURN && run.requests == 0;
		run.request_display = is_users_message ? run.user_input : String();
		run.request_automatic = !is_users_message;
		if (is_users_message) {
			full_message = AIPromptBuilder::build_user_message(p_message, pending_attachments, pending_screenshot_path,
					provider->supports_image_input(), run.request_image_b64, screenshot_dropped);
			pending_attachments.clear();
			pending_screenshot_path = String();
		}
	}
	if (screenshot_dropped) {
		post_notice(NOTICE_WARNING, AI_L("The current provider does not accept images; the screenshot was not sent.", "当前服务商不支持图像输入，截图未发送。"));
	}

	AIStreamClient::Request request;
	provider->set_pending_image(run.request_image_b64);
	request.body = provider->build_stream_request_body(system_prompt, session.build_api_messages(), full_message);
	provider->set_pending_image(String());
	request.url = provider->get_stream_url();
	request.headers = provider->get_headers();
	request.provider = provider;
	request.proxy = AIProxy::for_url(request.url);
	AI_LOG("Request: " + _redact_url_key(request.url) + " (" + itos(request.body.length()) + " chars)");

	session.push_user(full_message, run.request_display, run.request_automatic);
	run.requests++;

	stream.start(request);
	_set_state(STATE_STREAMING);
	stream_poll_timer->start(STREAM_POLL_INTERVAL_SEC);
	emit_signal(SNAME("request_started"), run.mode == MODE_ASK && run.kind == RUN_USER_TURN);
	return true;
}

void AIAgentController::_on_stream_poll() {
	if (state != STATE_STREAMING) {
		stream_poll_timer->stop();
		return;
	}

	Vector<AIStreamPiece> pieces;
	AIStreamClient::Result result;
	const bool finished = stream.poll(pieces, result);
	for (int i = 0; i < pieces.size(); i++) {
		emit_signal(SNAME("stream_delta"), pieces[i].text, pieces[i].is_thinking);
	}
	if (!finished) {
		return;
	}
	stream_poll_timer->stop();
	if (run.stop_requested) {
		result.outcome = AIStreamClient::OUTCOME_STOPPED; // Stop is final, however the request ended.
	}
	const uint64_t serial = run_serial;
	emit_signal(SNAME("request_finished"), (int)result.outcome);
	if (serial != run_serial) {
		return; // A listener ended the run.
	}

	switch (result.outcome) {
		case AIStreamClient::OUTCOME_STOPPED: {
			// Keep what arrived so the conversation stays coherent, but a
			// truncated reply is never acted on: no code, no markers, no retry.
			if (result.content.is_empty()) {
				// Nothing came back: take the question back as well, so the
				// history does not end on a message that was never answered.
				String unanswered;
				session.pop_last_user(unanswered);
				session.forget_attachments();
			} else {
				session.push_assistant(result.content, result.thinking);
				run.response = result.content;
				PackedStringArray text_segments;
				const Vector<String> code_blocks = _parse_reply(result.content, text_segments);
				_emit_response(text_segments, code_blocks, run.mode != MODE_ASK, true);
				if (serial != run_serial) {
					return;
				}
			}
			_end_run(OUTCOME_STOPPED);
		} break;
		case AIStreamClient::OUTCOME_ERROR: {
			_handle_stream_error(result.error);
		} break;
		case AIStreamClient::OUTCOME_COMPLETED: {
			String response = result.content;
			String thinking = result.thinking;
			run.untrusted_reason = String();
			if (result.truncated) {
				run.untrusted_reason = AI_L("The reply was cut off at the output limit, so its code was not run. Raise \"Max tokens\" in Settings or ask for the work in smaller steps.",
						"回复在输出长度上限处被截断，其中的代码没有执行。可在设置里调高“最大 Token 数”，或把任务拆成更小的步骤。");
			} else if (result.blocked) {
				run.untrusted_reason = AI_L("The provider withheld part of the reply (content filter), so its code was not run.", "服务商拦截了部分回复（内容过滤），其中的代码没有执行。");
			}
			// Some proxies route all model output through the reasoning channel
			// and leave content empty. It is shown, so the user is not left with
			// nothing — but reasoning is a draft, and a draft is not executed.
			if (response.strip_edges().is_empty() && !thinking.strip_edges().is_empty()) {
				AI_WARN("Content field empty — showing the reasoning as the reply.");
				response = thinking;
				thinking = String();
				if (run.untrusted_reason.is_empty()) {
					run.untrusted_reason = AI_L("The model returned only its reasoning and no answer, so nothing was run.", "模型只返回了推理过程而没有给出回答，因此没有执行任何代码。");
				}
			}
			if (response.strip_edges().is_empty()) {
				// Nothing to keep: an empty assistant turn would poison the history.
				_handle_stream_error(result.blocked ? String("The provider withheld the reply (content filter).") : String("The model returned an empty reply."));
				break;
			}
			AI_LOG("Response complete: " + itos(response.length()) + " chars");
			_handle_response(response, thinking, run.untrusted_reason.is_empty());
		} break;
	}
}

void AIAgentController::_handle_stream_error(const String &p_error) {
	AI_ERR("Streaming error: " + p_error);
	if (_try_overflow_retry(p_error) || _try_retry_without_temperature(p_error)) {
		return;
	}
	// The request was never answered: take it back, or the next message would
	// follow it as a second user turn in a row.
	String unanswered;
	session.pop_last_user(unanswered);
	// Whatever was attached to it never reached the model.
	session.forget_attachments();
	post_notice(NOTICE_ERROR, AI_L("Error: ", "错误：") + p_error);
	_fail_run(p_error);
}

bool AIAgentController::_try_overflow_retry(const String &p_error) {
	if (run.overflow_retry_used) {
		return false; // Never retry a retry.
	}
	const AISession::OverflowInfo info = AISession::parse_overflow_error(p_error);
	if (!info.is_overflow) {
		return false;
	}
	// Only when there is a request to resend; checked before anything is dropped.
	String failed_message;
	if (!session.pop_last_user(failed_message)) {
		return false;
	}
	session.push_user(failed_message);
	const AISession::DropResult dropped = session.drop_oldest_for_gap(session.get_overflow_gap(info));
	if (dropped.freed_tokens == 0) {
		return false;
	}
	session.pop_last_user(failed_message);
	AI_LOG(vformat("Context overflow: dropped %d messages (~%d tokens). Retrying...", dropped.dropped_messages, dropped.freed_tokens));
	post_notice(NOTICE_WARNING, vformat(AI_L("⚠ Context too long — dropped %d messages (~%d tokens) and retrying automatically.", "⚠ 上下文过长 — 已丢弃 %d 条消息（约 %d tokens）并自动重试。"), dropped.dropped_messages, dropped.freed_tokens));
	run.overflow_retry_used = true;
	return _send_request(failed_message, true);
}

// Some reasoning models answer HTTP 400 to any explicit temperature.
bool AIAgentController::_try_retry_without_temperature(const String &p_error) {
	if (provider.is_null() || !provider->is_sending_temperature()) {
		return false; // Already off: the error is about something else.
	}
	const String lower = p_error.to_lower();
	if (lower.find("temperature") < 0 || lower.find("http 400") < 0) {
		return false;
	}
	String failed_message;
	if (!session.pop_last_user(failed_message)) {
		return false;
	}
	models_without_temperature.insert(AIProviderSetup::get_model_key(provider));
	provider->set_send_temperature(false);
	AI_LOG("Model rejected the temperature parameter; retrying without it: " + AIProviderSetup::get_model_key(provider));
	post_notice(NOTICE_WARNING, AI_L("This model does not accept a custom temperature; retrying with its default.", "该模型不接受自定义温度，已改用默认值重试。"));
	return _send_request(failed_message, true);
}

// =============================================================================
// Conversation
// =============================================================================

void AIAgentController::_compress_if_needed() {
	const int context_length = provider.is_valid() ? provider->get_model_context_length() : FALLBACK_CONTEXT_LENGTH;
	const int max_output = provider.is_valid() ? provider->get_max_tokens() : FALLBACK_MAX_OUTPUT_TOKENS;
	const AISession::CompressionResult result = session.compress_if_needed(context_length, max_output, last_system_prompt_tokens, compression_config);
	if (!result.compressed) {
		return;
	}
	AI_LOG(vformat("Context compressed: ~%d tokens exceeded %d; %d messages folded into the summary.", result.tokens_before, result.threshold, result.removed.size()));

	if (compression_use_ai_summary) {
		history_summarizer->request(provider, result.removed, result.block);
	}
	// Re-inject the scripts being worked on, so the model does not forget them
	// along with the messages that mentioned them. Replaces the previous
	// snapshot rather than piling up.
	session.set_summary_attachment(prompt_builder.get_open_scripts_snapshot());
}

void AIAgentController::_reset_conversation() {
	session.clear();
	history_summarizer->cancel();
	plan.clear();
	pending_attachments.clear();
	pending_screenshot_path = String();
}

bool AIAgentController::new_session() {
	if (is_busy()) {
		return false;
	}
	save_session();
	_reset_conversation();
	emit_signal(SNAME("session_changed"));
	AI_LOG("New conversation started.");
	return true;
}

void AIAgentController::save_session() {
	AIChatStore::save(session);
}

bool AIAgentController::load_session(const String &p_file_path) {
	if (is_busy()) {
		return false;
	}
	AISession loaded;
	if (!AIChatStore::load(p_file_path, loaded)) {
		return false;
	}
	save_session();
	_reset_conversation();
	session = loaded;
	emit_signal(SNAME("session_changed"));
	AI_LOG("Loaded chat: " + session.get_chat_id() + " (" + itos(session.get_full_history().size()) + " messages)");
	return true;
}

bool AIAgentController::delete_chat(const String &p_file_path) {
	if (is_busy()) {
		return false;
	}
	const bool is_current = !session.get_chat_id().is_empty() && p_file_path.get_file().get_basename() == session.get_chat_id();
	AIChatStore::remove(p_file_path);
	if (is_current) {
		_reset_conversation();
		emit_signal(SNAME("session_changed"));
	}
	return true;
}

String AIAgentController::add_attachment(const String &p_path, const String &p_content) {
	const String attachment = session.build_attachment_content(p_path, p_content);
	pending_attachments.push_back(attachment);
	return attachment;
}

// =============================================================================
// Configuration
// =============================================================================

void AIAgentController::reload_settings() {
	compression_config.keep_recent = AIProviderSetup::get_editor_setting("ai_assistant/compression_keep_recent", compression_config.keep_recent);
	compression_config.threshold_pct = AIProviderSetup::get_editor_setting("ai_assistant/compression_threshold_pct", compression_config.threshold_pct);
	compression_use_ai_summary = AIProviderSetup::get_editor_setting("ai_assistant/compression_use_ai_summary", false);
	code_runner.reload_permissions();
	if (provider_overridden) {
		return;
	}

	// A summary in flight was built for, and must be parsed by, the old provider.
	history_summarizer->cancel();
	provider = AIProviderSetup::from_editor_settings();
	if (models_without_temperature.has(AIProviderSetup::get_model_key(provider))) {
		provider->set_send_temperature(false);
	}
	AI_LOG("Provider: " + AIProviderSetup::describe(provider));
}

bool AIAgentController::is_eval_run() {
	// Read once: generated code cannot change the environment, and even if
	// it could, the answer was fixed before any of it ran.
	static const bool eval_run = OS::get_singleton()->has_environment("GODOT_AI_EVAL_TASK");
	return eval_run;
}

void AIAgentController::set_auto_approve(bool p_enabled) {
	ERR_FAIL_COND_MSG(!is_eval_run(), "set_auto_approve() is only available to the eval runner.");
	auto_approve = p_enabled;
}

void AIAgentController::configure_provider(const String &p_provider_id, const String &p_model, const String &p_endpoint, const String &p_api_key) {
	ERR_FAIL_COND_MSG(!is_eval_run(), "configure_provider() is only available to the eval runner.");
	ERR_FAIL_COND_MSG(is_busy(), "Cannot change the provider while a run is in flight.");
	history_summarizer->cancel();
	provider = AIProviderSetup::from_values(p_provider_id, p_model, p_endpoint, p_api_key);
	provider_overridden = true;
	AI_LOG("Provider (override): " + AIProviderSetup::describe(provider));
}

AIAgentController::AIAgentController() {
	is_eval_run(); // Decide now, before any generated code exists.
	response_parser.instantiate();
	prompt_builder.set_error_monitor(code_runner.get_error_monitor());

	stream_poll_timer = memnew(Timer);
	stream_poll_timer->set_one_shot(false);
	stream_poll_timer->connect("timeout", callable_mp(this, &AIAgentController::_on_stream_poll));
	add_child(stream_poll_timer);

	verify_timer = memnew(Timer);
	verify_timer->set_one_shot(true);
	verify_timer->connect("timeout", callable_mp(this, &AIAgentController::_on_verify_timeout));
	add_child(verify_timer);

	web_requester = memnew(AIWebRequester);
	web_requester->connect("finished", callable_mp(this, &AIAgentController::_on_web_request_finished));
	web_requester->connect("notice", callable_mp(this, &AIAgentController::_on_child_notice));
	add_child(web_requester);

	history_summarizer = memnew(AIHistorySummarizer);
	history_summarizer->set_session(&session);
	add_child(history_summarizer);

	asset_service = memnew(AIAssetService);
	asset_service->connect("notice", callable_mp(this, &AIAgentController::_on_child_notice));
	add_child(asset_service);
}

AIAgentController::~AIAgentController() {
	save_session();
}

#endif // TOOLS_ENABLED
