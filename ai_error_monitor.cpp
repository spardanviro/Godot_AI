#ifdef TOOLS_ENABLED

#include "ai_error_monitor.h"

AIErrorMonitor *AIErrorMonitor::singleton = nullptr;

void AIErrorMonitor::_bind_methods() {
}

void AIErrorMonitor::_error_handler_func(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	AIErrorMonitor *self = static_cast<AIErrorMonitor *>(p_userdata);
	if (!self) {
		return;
	}

	// Only capture errors and warnings, skip shader/script parsing noise.
	if (p_type != ERR_HANDLER_ERROR && p_type != ERR_HANDLER_WARNING && p_type != ERR_HANDLER_SCRIPT) {
		return;
	}

	// Filter out errors from the AI assistant module (e.g., chat save failures).
	// These are internal panel issues, not user or AI code errors.
	String file_str = p_file ? String(p_file) : "";
	if (file_str.find("ai_assistant") >= 0 ||
		file_str.find("ai_error_monitor") >= 0 ||
		file_str.find("ai_checkpoint") >= 0 ||
		file_str.find("ai_permission") >= 0 ||
		file_str.find("ai_web_search") >= 0 ||
		file_str.find("ai_image_gen") >= 0 ||
		file_str.find("ai_audio_gen") >= 0 ||
		file_str.find("ai_profiler") >= 0 ||
		file_str.find("ai_context") >= 0 ||
		file_str.find("ai_ui_agent") >= 0) {
		return;
	}

	// Also filter by error message content — some errors (e.g., directory creation)
	// come from Godot core but are triggered by AI panel operations.
	String msg_str = p_error ? String(p_error) : "";
	String detail_str = p_message ? String(p_message) : "";
	if (msg_str.find("ai_assistant_chats") >= 0 || detail_str.find("ai_assistant_chats") >= 0) {
		return;
	}

	ErrorEntry entry;
	entry.message = String(p_error);
	if (p_message && p_message[0]) {
		entry.message += ": " + String(p_message);
	}
	entry.function = p_function ? String(p_function) : "";
	entry.file = p_file ? String(p_file) : "";
	entry.line = p_line;
	entry.is_warning = (p_type == ERR_HANDLER_WARNING);
	entry.during_ai_execution = self->ai_execution_active;
	entry.timestamp = Time::get_singleton()->get_ticks_msec();

	// Add to appropriate buffers.
	if (self->ai_execution_active) {
		self->ai_errors.push_back(entry);
	}

	self->all_errors.push_back(entry);
	while (self->all_errors.size() > self->max_buffer_size) {
		self->all_errors.remove_at(0);
	}
}

void AIErrorMonitor::begin_ai_execution() {
	ai_errors.clear();
	ai_execution_active = true;
}

void AIErrorMonitor::end_ai_execution() {
	ai_execution_active = false;
}

bool AIErrorMonitor::has_ai_execution_errors() const {
	return !ai_errors.is_empty();
}

String AIErrorMonitor::get_ai_execution_errors_text() const {
	if (ai_errors.is_empty()) {
		return "";
	}

	String result;
	for (int i = 0; i < ai_errors.size(); i++) {
		const ErrorEntry &e = ai_errors[i];
		if (!result.is_empty()) {
			result += "\n";
		}
		result += (e.is_warning ? "[WARNING] " : "[ERROR] ");
		result += e.message;
	}
	return result;
}

String AIErrorMonitor::get_recent_console_errors(int p_count) const {
	if (all_errors.is_empty()) {
		return "No recent errors or warnings in the console.";
	}

	String result;
	int start = MAX(0, all_errors.size() - p_count);
	int count = 0;
	for (int i = start; i < all_errors.size(); i++) {
		const ErrorEntry &e = all_errors[i];
		// Skip AI-execution errors for user analysis — they were already handled.
		if (e.during_ai_execution) {
			continue;
		}
		if (!result.is_empty()) {
			result += "\n";
		}
		result += (e.is_warning ? "[WARNING] " : "[ERROR] ");
		result += e.message;
		if (!e.file.is_empty()) {
			result += " (at " + e.file + ":" + itos(e.line) + ")";
		}
		count++;
	}

	if (count == 0) {
		return "No recent user/editor errors in the console.";
	}

	return "Recent console errors/warnings (" + itos(count) + "):\n" + result;
}

int AIErrorMonitor::get_recent_error_count() const {
	int count = 0;
	for (int i = 0; i < all_errors.size(); i++) {
		if (!all_errors[i].during_ai_execution) {
			count++;
		}
	}
	return count;
}

void AIErrorMonitor::clear_ai_errors() {
	ai_errors.clear();
}

void AIErrorMonitor::receive_debugger_error(const String &p_message, const String &p_file, int p_line, bool p_is_warning) {
	AIErrorMonitor *self = singleton;
	if (!self) {
		return;
	}
	DebuggerErrorEntry entry;
	entry.message = p_message;
	entry.file = p_file;
	entry.line = p_line;
	entry.is_warning = p_is_warning;
	entry.timestamp = Time::get_singleton()->get_ticks_msec();

	self->debugger_errors.push_back(entry);
	while (self->debugger_errors.size() > self->max_debugger_buffer_size) {
		self->debugger_errors.remove_at(0);
	}
}

String AIErrorMonitor::get_recent_debugger_errors(int p_count) const {
	if (debugger_errors.is_empty()) {
		return "";
	}
	String result;
	int start = MAX(0, debugger_errors.size() - p_count);
	for (int i = start; i < debugger_errors.size(); i++) {
		const DebuggerErrorEntry &e = debugger_errors[i];
		if (!result.is_empty()) {
			result += "\n";
		}
		result += (e.is_warning ? "[WARNING] " : "[ERROR] ");
		result += e.message;
		if (!e.file.is_empty()) {
			result += " (at " + e.file + ":" + itos(e.line) + ")";
		}
	}
	return result;
}

int AIErrorMonitor::get_debugger_error_count() const {
	return debugger_errors.size();
}

void AIErrorMonitor::clear_debugger_errors() {
	debugger_errors.clear();
}

void AIErrorMonitor::clear_all() {
	ai_errors.clear();
	all_errors.clear();
	debugger_errors.clear();
}

AIErrorMonitor::AIErrorMonitor() {
	singleton = this;
	error_handler.errfunc = _error_handler_func;
	error_handler.userdata = this;
	add_error_handler(&error_handler);
}

AIErrorMonitor::~AIErrorMonitor() {
	remove_error_handler(&error_handler);
	if (singleton == this) {
		singleton = nullptr;
	}
}

#endif // TOOLS_ENABLED
