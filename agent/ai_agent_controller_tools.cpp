#ifdef TOOLS_ENABLED

// What AIAgentController does when a reply asks for tools: run them one by
// one — asking the user where their permissions say so, waiting where a tool
// takes time — and hand the results back for the model to carry on.

#include "ai_agent_controller.h"

#include "../common/ai_log.h"
#include "../common/ai_settings.h"
#include "../prompt/ai_knowledge.h"
#include "../providers/ai_provider_setup.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "editor/editor_interface.h"
#include "scene/main/timer.h"
#include "scene/main/viewport.h"

static const int RUN_SCENE_DEFAULT_SEC = 3;
static const int RUN_SCENE_MAX_SEC = 10;
static const int RUN_SCENE_ERROR_COUNT = 20;
static const char *TOOL_SCREENSHOT_PATH = "user://ai_screenshots/tool_screenshot.png";

// Whether this request offers tools: the provider speaks them, the user has
// not turned them off, and this model has not refused them before.
bool AIAgentController::_offers_tools() const {
	return tools_enabled && !run.tools_off && provider.is_valid() && provider->supports_tools() &&
			!models_without_tools.has(AIProviderSetup::get_model_key(provider));
}

// Outside AGENT mode the model may look but not touch.
bool AIAgentController::_may_edit_with_tools() const {
	return run.mode == MODE_AGENT || run.kind != RUN_USER_TURN;
}

bool AIAgentController::_edits_with_tools() const {
	return _offers_tools() && _may_edit_with_tools();
}

Vector<AIToolSpec> AIAgentController::_tool_specs() const {
	Vector<AIToolSpec> specs;
	for (const AIToolSpec &tool : toolbox.get_specs(!_may_edit_with_tools())) {
		if (tool.name == "take_screenshot" && !provider->supports_image_input()) {
			continue; // It could not look at the picture.
		}
		specs.push_back(tool);
	}
	return specs;
}

// A server that does not speak tool calls answers the first request that
// offers them with a client error. The request goes out again without them,
// and the model is not offered tools again.
bool AIAgentController::_try_retry_without_tools(const String &p_error) {
	if (provider.is_null() || !provider->has_tools()) {
		return false;
	}
	const String lower = p_error.to_lower();
	const bool client_error = lower.find("http 400") >= 0 || lower.find("http 404") >= 0 || lower.find("http 422") >= 0 || lower.find("http 501") >= 0;
	if (!client_error || (lower.find("tool") < 0 && lower.find("function") < 0)) {
		return false;
	}
	String failed_message;
	if (!session.pop_last_user(failed_message)) {
		return false; // In the middle of a tool round there is no request to resend.
	}
	models_without_tools.insert(AIProviderSetup::get_model_key(provider));
	AI_LOG("Model or endpoint rejected tool calls; retrying without them: " + AIProviderSetup::get_model_key(provider));
	post_notice(NOTICE_WARNING, AI_L("This model or endpoint does not accept tool calls; continuing without them.", "该模型或端点不支持工具调用，已改为不使用工具继续。"));
	return _send_request(failed_message, true);
}

void AIAgentController::_handle_tool_calls(const String &p_content, const String &p_thinking, const Vector<AIToolCall> &p_calls) {
	const bool over_limit = run.tool_rounds >= MAX_TOOL_ROUNDS_PER_RUN;
	run.tool_rounds++;
	session.push_assistant_tool_calls(p_content, p_thinking, p_calls);
	run.tool_queue = p_calls;
	run.tool_index = 0;

	if (over_limit) {
		const uint64_t serial = run_serial;
		while (run.tool_index < run.tool_queue.size()) {
			_finish_tool("Error: no more tool calls are available for this message. Answer with what you have.", false);
			if (serial != run_serial) {
				return;
			}
		}
		// Without tools on offer the model can only answer.
		run.tools_off = true;
		post_notice(NOTICE_WARNING, AI_L("The model kept calling tools; it was asked to answer with what it has.", "模型反复调用工具，已要求它根据现有信息作答。"));
		_send_request(AIKnowledge::get_singleton()->format_prompt("tool_limit"));
		return;
	}
	_run_tool_queue();
}

// Carries on with the calls of the round until one has to wait (for the user,
// or for the game to run), then hands the results back.
void AIAgentController::_run_tool_queue() {
	const uint64_t serial = run_serial;
	while (run.tool_index < run.tool_queue.size()) {
		if (_stopped_meanwhile()) {
			return;
		}
		const bool finished = _start_tool(run.tool_queue[run.tool_index]);
		if (serial != run_serial || !finished) {
			return; // The run ended, or the tool is waiting; it resumes the queue itself.
		}
	}
	_send_request(String(), false, true);
}

// Records the result of the call at the head of the queue and moves on.
void AIAgentController::_finish_tool(const String &p_result, bool p_ok) {
	const AIToolCall call = run.tool_queue[run.tool_index];
	run.tool_index++;
	run.tool_calls++;
	session.push_tool_result(call, p_result);
	AI_LOG("Tool: " + AIToolbox::describe_call(call) + (p_ok ? " -> " + itos(p_result.length()) + " chars" : " -> " + p_result.left(300)));
	emit_signal(SNAME("tool_called"), AIToolbox::describe_call(call), p_ok, (int)toolbox.get_kind(call.name));
}

// Before the first change of a run: a checkpoint, so that everything the
// tools do for this message can be reverted in one go.
void AIAgentController::_ensure_tool_checkpoint() {
	if (run.tool_checkpoint_taken) {
		return;
	}
	run.tool_checkpoint_taken = true;
	const Ref<AICheckpointManager> checkpoints = code_runner.get_checkpoint_manager();
	checkpoints->begin_file_snapshot();
	int id = -1;
	if (checkpoints->create_checkpoint("Before the assistant's changes")) {
		id = checkpoints->get_latest_id();
	} else {
		id = checkpoints->create_files_checkpoint("Before the assistant's changes");
	}
	if (id >= 0) {
		emit_signal(SNAME("tools_checkpoint"), id);
	}
}

// Returns true when the call is done (its result recorded), false when it is
// waiting for something and will resume the queue later.
bool AIAgentController::_start_tool(const AIToolCall &p_call) {
	const AIToolSpec::Kind kind = toolbox.get_kind(p_call.name);
	bool ok = false;

	if (kind == AIToolSpec::KIND_READ) {
		const String result = toolbox.call(p_call.name, p_call.arguments, ok); // Also the answer for a name that is no tool.
		_finish_tool(result, ok);
		return true;
	}
	if (!_may_edit_with_tools()) {
		_finish_tool("Error: in this mode you can look but not change anything. Describe what should be done instead.", false);
		return true;
	}

	if (p_call.name == "take_screenshot") {
		const String result = _take_tool_screenshot(ok);
		_finish_tool(result, ok);
		return true;
	}
	if (p_call.name == "run_editor_script") {
		return _start_script_tool(p_call);
	}

	// Everything else is subject to the user's permissions.
	const bool autorun = AISettings::get("autorun", true);
	const int category = toolbox.get_permission_category(p_call);
	const Ref<AIPermissionManager> permissions = code_runner.get_permission_manager();
	AIPermissionManager::Level level = AIPermissionManager::LEVEL_ALLOW;
	String what;
	if (category >= 0) {
		level = permissions->get_permission((AIPermissionManager::Category)category);
		Vector<AIPermissionManager::Category> categories;
		categories.push_back((AIPermissionManager::Category)category);
		// The summary is worded for code; what matters here is the category.
		what = AI_L("Permission: ", "权限类别：") + permissions->get_code_action_summary(categories).trim_prefix("This code will: ");
	}
	if (level == AIPermissionManager::LEVEL_DENY) {
		_finish_tool("Error: the user's permission settings do not allow this (" + what + "). Do not try another way to do it; tell the user it is turned off in Settings > Permissions.", false);
		return true;
	}
	if ((level == AIPermissionManager::LEVEL_ASK || !autorun) && !auto_approve) {
		run.tool_awaiting_permission = true;
		_set_state(STATE_AWAITING_PERMISSION);
		emit_signal(SNAME("permission_requested"),
				AI_L("The assistant wants to:\n\n", "助手想要执行：\n\n") + AIToolbox::describe_call(p_call) + (what.is_empty() ? String() : "\n\n" + what));
		return false;
	}
	return _execute_tool(p_call);
}

// Runs a tool that is cleared to run.
bool AIAgentController::_execute_tool(const AIToolCall &p_call) {
	if (p_call.name == "run_scene") {
		return _start_run_scene(p_call);
	}
	_ensure_tool_checkpoint();
	bool ok = false;
	const String result = toolbox.call(p_call.name, p_call.arguments, ok);
	if (ok) {
		run.tool_edits++;
	}
	_finish_tool(result, ok);
	return true;
}

// The answer to a permission prompt that was about a tool.
void AIAgentController::_resume_tool_after_permission(bool p_allowed) {
	run.tool_awaiting_permission = false;
	_set_state(STATE_STREAMING);
	const AIToolCall call = run.tool_queue[run.tool_index];
	const String script = run.tool_script_code;
	run.tool_script_code = String();

	const uint64_t serial = run_serial;
	bool finished = true;
	if (!p_allowed) {
		_finish_tool("Error: the user declined this. Do not try another way to do the same thing; ask what they would like instead.", false);
	} else if (call.name == "run_editor_script") {
		_execute_script_tool(script);
	} else {
		finished = _execute_tool(call);
	}
	if (serial == run_serial && finished) {
		_run_tool_queue();
	}
}

// =============================================================================
// Host tools
// =============================================================================

String AIAgentController::_take_tool_screenshot(bool &r_ok) {
	r_ok = false;
	SubViewport *viewport = EditorInterface::get_singleton()->get_editor_viewport_3d(0);
	if (!viewport) {
		viewport = EditorInterface::get_singleton()->get_editor_viewport_2d();
	}
	Ref<Image> image;
	if (viewport && viewport->get_texture().is_valid()) {
		image = viewport->get_texture()->get_image();
	}
	if (image.is_null() || image->is_empty()) {
		return "Error: the editor viewport could not be captured.";
	}
	const String path = TOOL_SCREENSHOT_PATH;
	DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(path.get_base_dir()));
	String encoded;
	if (image->save_png(path) != OK || AIPromptBuilder::prepare_screenshot(path, true, encoded).is_empty() || encoded.is_empty()) {
		return "Error: the screenshot could not be prepared.";
	}
	run.tool_image_b64 = encoded;
	r_ok = true;
	return "A screenshot of the editor viewport (" + itos(image->get_width()) + "x" + itos(image->get_height()) + ") follows as the next message.";
}

bool AIAgentController::_start_run_scene(const AIToolCall &p_call) {
	Dictionary arguments;
	AIToolbox::parse_arguments(p_call.arguments, arguments);
	String scene = String(arguments.get("scene", "")).strip_edges();
	if (scene.is_empty()) {
		const Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
		scene = root ? root->get_scene_file_path() : String();
		if (scene.is_empty()) {
			_finish_tool("Error: the edited scene has no file yet. Call save_scene with a path first.", false);
			return true;
		}
	}
	if (!scene.begins_with("res://") || !FileAccess::exists(scene)) {
		_finish_tool("Error: '" + scene + "' is not a scene file of the project.", false);
		return true;
	}
	if (EditorInterface::get_singleton()->is_playing_scene()) {
		EditorInterface::get_singleton()->stop_playing_scene();
	}
	const Variant seconds = arguments.get("seconds", RUN_SCENE_DEFAULT_SEC);
	run.tool_game_errors_before = code_runner.get_error_monitor()->get_debugger_error_count();
	EditorInterface::get_singleton()->play_custom_scene(scene);
	run.tool_waiting_for_game = true;
	tool_wait_timer->start(CLAMP(seconds.is_num() ? int(seconds) : RUN_SCENE_DEFAULT_SEC, 1, RUN_SCENE_MAX_SEC));
	return false;
}

void AIAgentController::_on_tool_wait_timeout() {
	if (!run.tool_waiting_for_game) {
		return;
	}
	run.tool_waiting_for_game = false;
	const bool still_running = EditorInterface::get_singleton()->is_playing_scene();
	if (still_running) {
		EditorInterface::get_singleton()->stop_playing_scene();
	}
	const Ref<AIErrorMonitor> monitor = code_runner.get_error_monitor();
	const int new_errors = monitor->get_debugger_error_count() - run.tool_game_errors_before;
	String result = still_running ? "The scene ran and was stopped." : "The scene started but was no longer running when checked: it quit or crashed.";
	if (new_errors > 0) {
		result += "\nErrors and warnings it raised:\n" + monitor->get_recent_debugger_errors(MIN(new_errors, RUN_SCENE_ERROR_COUNT));
	} else {
		result += "\nIt raised no errors.";
	}
	const uint64_t serial = run_serial;
	_finish_tool(result, still_running && new_errors <= 0);
	if (serial == run_serial) {
		_run_tool_queue();
	}
}

// The fallback: arbitrary editor code, through the same gate as a code block
// in a reply — syntax, safety policy, permissions — with the outcome handed
// back as the tool's result instead of driving a retry.
bool AIAgentController::_start_script_tool(const AIToolCall &p_call) {
	Dictionary arguments;
	AIToolbox::parse_arguments(p_call.arguments, arguments);
	const String code = arguments.get("code", "");
	if (code.strip_edges().is_empty()) {
		_finish_tool("Error: 'code' is required.", false);
		return true;
	}
	const bool autorun = AISettings::get("autorun", true);
	const AICodeRunner::Review review = code_runner.review(code, !autorun);
	switch (review.action) {
		case AICodeRunner::ACTION_FIX_SYNTAX:
			_finish_tool("Error: the code does not parse and was not run:\n" + review.message, false);
			return true;
		case AICodeRunner::ACTION_FIX_SAFETY:
			_finish_tool("Error: the code was not run because it violates the safety policy: " + review.message + "\nDo it another way that stays within the policy, or explain why it cannot be done.", false);
			return true;
		case AICodeRunner::ACTION_REFUSE:
			_finish_tool("Error: the code was not run: " + review.message + "\nDo not try again.", false);
			return true;
		case AICodeRunner::ACTION_CONFIRM:
			if (!auto_approve) {
				run.tool_script_code = review.code;
				run.tool_awaiting_permission = true;
				_set_state(STATE_AWAITING_PERMISSION);
				const String description = String(arguments.get("description", "")).strip_edges();
				emit_signal(SNAME("permission_requested"), (description.is_empty() ? String() : description + "\n\n") + review.message);
				return false;
			}
			break;
		case AICodeRunner::ACTION_RUN:
			break;
	}
	_execute_script_tool(review.code);
	return true;
}

void AIAgentController::_execute_script_tool(const String &p_code) {
	AI_LOG("---------- CODE EXECUTION (tool) ----------");
	AICodeRunner::Result result = code_runner.execute(p_code);
	const String late_errors = code_runner.take_late_errors();
	run.executions++;
	run.last_execution_ok = result.success;
	run.last_error = result.error;

	String tool_result;
	if (result.success) {
		run.tool_edits++;
		tool_result = "The code ran.\nOutput:\n" + (result.output.strip_edges().is_empty() ? String("(nothing was printed)") : result.output);
		if (!late_errors.is_empty()) {
			tool_result += "\nErrors raised while it ran:\n" + late_errors;
		}
	} else {
		tool_result = "Error: " + result.error + "\n";
		if (result.checkpoint_id >= 0 && code_runner.get_checkpoint_manager()->restore_checkpoint(result.checkpoint_id)) {
			result.checkpoint_id = -1; // Used up.
			tool_result += "The scene and the project files were restored to how they were before this code ran.";
		} else {
			tool_result += "Part of the code may have run before the error, and its changes are still there.";
		}
	}

	Dictionary report;
	report["success"] = result.success;
	report["output"] = result.output;
	report["error"] = result.error;
	report["summary"] = result.summary;
	report["code"] = p_code;
	report["response"] = "```gdscript\n" + p_code + "\n```";
	report["checkpoint_id"] = result.checkpoint_id;
	report["compact"] = true;
	const uint64_t serial = run_serial;
	emit_signal(SNAME("execution_finished"), report);
	if (serial != run_serial) {
		return;
	}
	_finish_tool(tool_result, result.success);
}

#endif // TOOLS_ENABLED
