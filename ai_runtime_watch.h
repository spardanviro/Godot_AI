#pragma once

#ifdef TOOLS_ENABLED

#include "core/error/error_macros.h"
#include "core/os/mutex.h"
#include "scene/main/node.h"

class AIAgentController;
class Timer;

// Watches the running game. When it raises script errors the scene is stopped
// and the controller is asked to fix them; a clean run afterwards is reported
// as success. Always on; the controller decides whether it is free to help.
class AIRuntimeWatch : public Node {
	GDCLASS(AIRuntimeWatch, Node);

	static const int MAX_FIX_ATTEMPTS = 5;

	AIAgentController *controller = nullptr;
	Timer *poll_timer = nullptr; // Detects play start / stop.
	Timer *collect_timer = nullptr; // Lets related errors accumulate before stopping the scene.

	bool was_playing = false;
	int fix_attempts = 0; // Consecutive fixes that still left errors.
	String last_play_path;

	bool handler_installed = false;
	ErrorHandlerList error_handler;
	// In-process script errors. The engine calls error handlers from whichever
	// thread raised the error, so access goes through captured_errors_mutex.
	static String captured_errors;
	static Mutex captured_errors_mutex;

	static void _error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type);
	void _install_error_handler();
	void _remove_error_handler();

	bool _has_errors() const;
	String _take_errors();
	void _on_poll();
	void _on_collect_timeout();
	void _request_fix();

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	void set_controller(AIAgentController *p_controller) { controller = p_controller; }
	// The debugger connected to a freshly started game: errors buffered from
	// earlier runs must not be attributed to this one.
	void on_game_session_started();

	AIRuntimeWatch();
	~AIRuntimeWatch();
};

#endif // TOOLS_ENABLED
