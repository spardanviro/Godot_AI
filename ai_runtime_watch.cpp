#ifdef TOOLS_ENABLED

#include "ai_runtime_watch.h"

#include "ai_provider_setup.h"

#include "ai_agent_controller.h"
#include "ai_log.h"

#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/editor_interface.h"
#include "scene/main/timer.h"

static const double POLL_INTERVAL_SEC = 0.2;
static const double COLLECT_WINDOW_SEC = 2.0;
static const int MAX_DEBUGGER_ERRORS = 20;

String AIRuntimeWatch::captured_errors;
Mutex AIRuntimeWatch::captured_errors_mutex;

void AIRuntimeWatch::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		poll_timer->start();
	}
}

void AIRuntimeWatch::on_game_session_started() {
	{
		MutexLock lock(captured_errors_mutex);
		captured_errors = String();
	}
	collect_timer->stop();
	if (controller) {
		controller->get_error_monitor()->clear_debugger_errors();
	}
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	last_play_path = scene_root ? scene_root->get_scene_file_path() : String();
	AI_LOG("[Watch] Game session started. Scene: " + last_play_path);
}

void AIRuntimeWatch::_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	if (p_type != ERR_HANDLER_SCRIPT && p_type != ERR_HANDLER_ERROR) {
		return; // Warnings and shader errors are not worth stopping the game for.
	}
	String message = p_error ? String(p_error) : String();
	if (p_message && p_message[0]) {
		message += ": " + String(p_message);
	}
	if (message.is_empty()) {
		return;
	}

	const String file = p_file ? String(p_file) : String();
	bool from_user_script = file.begins_with("res://");

	String stack;
	const Vector<Ref<ScriptBacktrace>> backtraces = ScriptServer::capture_script_backtraces(false);
	for (int b = 0; b < backtraces.size(); b++) {
		const Ref<ScriptBacktrace> &trace = backtraces[b];
		if (trace.is_null() || trace->is_empty()) {
			continue;
		}
		for (int f = 0; f < trace->get_frame_count(); f++) {
			const String frame_file = trace->get_frame_file(f);
			from_user_script = from_user_script || frame_file.begins_with("res://");
			if (!stack.is_empty()) {
				stack += "\n    ";
			}
			stack += frame_file + ":" + itos(trace->get_frame_line(f)) + " @ " + trace->get_frame_function(f) + "()";
		}
	}

	// Engine errors only count when they trace back to the project's scripts.
	if (p_type == ERR_HANDLER_ERROR && !from_user_script) {
		const String lower = message.to_lower();
		if (!lower.contains("res://") && !lower.contains(".gd")) {
			return;
		}
	}

	MutexLock lock(captured_errors_mutex);
	if (!captured_errors.is_empty()) {
		captured_errors += "\n---\n";
	}
	captured_errors += message;
	if (!stack.is_empty()) {
		captured_errors += "\n    at: " + stack;
	} else if (from_user_script && !file.is_empty()) {
		captured_errors += "\n    at: " + file + ":" + itos(p_line);
	}
}

void AIRuntimeWatch::_install_error_handler() {
	if (handler_installed) {
		return;
	}
	error_handler.errfunc = _error_handler;
	error_handler.userdata = nullptr;
	add_error_handler(&error_handler);
	handler_installed = true;
}

void AIRuntimeWatch::_remove_error_handler() {
	if (!handler_installed) {
		return;
	}
	remove_error_handler(&error_handler);
	handler_installed = false;
}

bool AIRuntimeWatch::_has_errors() const {
	bool has_captured = false;
	{
		MutexLock lock(captured_errors_mutex);
		has_captured = !captured_errors.is_empty();
	}
	return has_captured || (controller && controller->get_error_monitor()->get_debugger_error_count() > 0);
}

// Errors from both sources — the in-process handler and the remote debugger —
// leaving both buffers empty.
String AIRuntimeWatch::_take_errors() {
	String errors;
	{
		MutexLock lock(captured_errors_mutex);
		errors = captured_errors;
		captured_errors = String();
	}
	if (controller) {
		const String debugger_errors = controller->get_error_monitor()->get_recent_debugger_errors(MAX_DEBUGGER_ERRORS);
		if (!debugger_errors.is_empty()) {
			if (!errors.is_empty()) {
				errors += "\n---\n";
			}
			errors += debugger_errors;
		}
		controller->get_error_monitor()->clear_debugger_errors();
	}
	return errors;
}

void AIRuntimeWatch::_on_poll() {
	if (!controller) {
		return;
	}
	if (!bool(AIProviderSetup::get_editor_setting("ai_assistant/runtime_fix", true))) {
		// Switched off: leave the running game alone and forget what was seen.
		if (was_playing || handler_installed) {
			_remove_error_handler();
			collect_timer->stop();
			_take_errors();
		}
		was_playing = false;
		fix_attempts = 0;
		return;
	}
	const bool playing = EditorInterface::get_singleton()->is_playing_scene();

	if (playing && !was_playing) {
		// Buffers were already cleared in on_game_session_started(), which fires
		// before any _ready() error can arrive.
		_install_error_handler();
	} else if (!playing && was_playing) {
		_remove_error_handler();
		collect_timer->stop();

		if (_has_errors()) {
			_request_fix();
		} else if (fix_attempts > 0 && !controller->is_busy()) {
			controller->post_notice(AIAgentController::NOTICE_SUCCESS, String(U"✅ ") + TR(STR_WATCH_SUCCESS));
			AI_LOG("[Watch] Scene ran without errors — runtime errors fixed.");
			fix_attempts = 0;
		}
	}
	was_playing = playing;

	if (playing && _has_errors() && collect_timer->is_stopped()) {
		controller->post_notice(AIAgentController::NOTICE_WARNING, String(U"⚠ ") + TR(STR_WATCH_DETECTING));
		collect_timer->start();
	}
}

void AIRuntimeWatch::_on_collect_timeout() {
	if (EditorInterface::get_singleton()->is_playing_scene() && _has_errors()) {
		AI_LOG("[Watch] Stopping the scene to apply a fix.");
		// The poll notices the stop and requests the fix.
		EditorInterface::get_singleton()->stop_playing_scene();
	}
}

void AIRuntimeWatch::_request_fix() {
	// The controller is doing something else: leave the errors in the buffers.
	// They are dropped when the next game session starts.
	if (controller->is_busy()) {
		AI_LOG("[Watch] The assistant is busy — skipping this runtime fix.");
		return;
	}
	if (fix_attempts >= MAX_FIX_ATTEMPTS) {
		controller->post_notice(AIAgentController::NOTICE_ERROR, String(U"❌ ") + TR(STR_WATCH_FAILED));
		AI_WARN("[Watch] Max fix attempts reached. Giving up.");
		fix_attempts = 0;
		_take_errors();
		return;
	}

	const String errors = _take_errors();
	if (errors.is_empty()) {
		return;
	}
	fix_attempts++;
	const String attempt = itos(fix_attempts) + "/" + itos(MAX_FIX_ATTEMPTS);
	AI_LOG("[Watch] Fix attempt " + attempt);
	controller->post_notice(AIAgentController::NOTICE_WARNING, String(U"🔄 ") + TR(STR_WATCH_FIXING) + " (" + attempt + ")");
	controller->start_runtime_fix(errors, last_play_path);
}

AIRuntimeWatch::AIRuntimeWatch() {
	poll_timer = memnew(Timer);
	poll_timer->set_wait_time(POLL_INTERVAL_SEC);
	poll_timer->set_one_shot(false);
	poll_timer->connect("timeout", callable_mp(this, &AIRuntimeWatch::_on_poll));
	add_child(poll_timer);

	collect_timer = memnew(Timer);
	collect_timer->set_wait_time(COLLECT_WINDOW_SEC);
	collect_timer->set_one_shot(true);
	collect_timer->connect("timeout", callable_mp(this, &AIRuntimeWatch::_on_collect_timeout));
	add_child(collect_timer);
}

AIRuntimeWatch::~AIRuntimeWatch() {
	// The handler must not outlive the object it points into.
	_remove_error_handler();
}

#endif // TOOLS_ENABLED
