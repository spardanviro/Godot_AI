#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/error/error_macros.h"
#include "core/string/ustring.h"
#include "core/os/time.h"

// Monitors editor console errors/warnings AND game runtime debugger errors.
// Distinguishes between AI-caused errors (during execution window) and user/editor errors.
class AIErrorMonitor : public RefCounted {
	GDCLASS(AIErrorMonitor, RefCounted);

public:
	struct ErrorEntry {
		String message;
		String function;
		String file;
		int line = 0;
		bool is_warning = false;
		bool during_ai_execution = false;
		uint64_t timestamp = 0;
	};

	// Errors from the game runtime debugger (sent via RemoteDebugger protocol).
	struct DebuggerErrorEntry {
		String message;
		String file;  // Script file from callstack (res:// path).
		int line = 0;
		bool is_warning = false;
		uint64_t timestamp = 0;
	};

private:
	static AIErrorMonitor *singleton;

	ErrorHandlerList error_handler;
	Vector<ErrorEntry> all_errors;           // Ring buffer of all editor/console errors.
	Vector<ErrorEntry> ai_errors;            // Errors captured during AI execution window.
	Vector<DebuggerErrorEntry> debugger_errors; // Ring buffer of game runtime errors.
	bool ai_execution_active = false;
	int max_buffer_size = 100;
	int max_debugger_buffer_size = 50;

	static void _error_handler_func(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type);

protected:
	static void _bind_methods();

public:
	static AIErrorMonitor *get_singleton() { return singleton; }

	// Called before/after AI code execution.
	void begin_ai_execution();
	void end_ai_execution();
	bool is_ai_executing() const { return ai_execution_active; }

	// Get errors captured during the most recent AI execution window.
	Vector<ErrorEntry> get_ai_execution_errors() const { return ai_errors; }
	String get_ai_execution_errors_text() const;
	bool has_ai_execution_errors() const;

	// Get recent non-AI errors for user-initiated analysis.
	String get_recent_console_errors(int p_count = 20) const;
	int get_recent_error_count() const;

	// Called by ScriptEditorDebugger when game runtime errors arrive via debugger protocol.
	static void receive_debugger_error(const String &p_message, const String &p_file, int p_line, bool p_is_warning);

	// Get recent game runtime (debugger) errors.
	String get_recent_debugger_errors(int p_count = 20) const;
	int get_debugger_error_count() const;
	void clear_debugger_errors();

	// Clear buffers.
	void clear_ai_errors();
	void clear_all();

	AIErrorMonitor();
	~AIErrorMonitor();
};

#endif // TOOLS_ENABLED
