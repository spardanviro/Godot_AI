#ifdef TOOLS_ENABLED

#include "ai_error_monitor.h"
#include "../network/ai_network_thread.h"

#include "core/object/class_db.h"
#include "core/os/os.h"

AIErrorMonitor *AIErrorMonitor::singleton = nullptr;
Mutex AIErrorMonitor::singleton_mutex;

void AIErrorMonitor::_bind_methods() {
}

void AIErrorMonitor::_error_handler_func(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	AIErrorMonitor *self = static_cast<AIErrorMonitor *>(p_userdata);
	if (!self) {
		return;
	}

	// What the assistant's own network threads print (a refused connection,
	// a TLS alert) is neither the user's code nor the model's.
	if (AINetworkThread::is_current()) {
		return;
	}

	// Only capture errors and warnings, skip shader/script parsing noise.
	if (p_type != ERR_HANDLER_ERROR && p_type != ERR_HANDLER_WARNING && p_type != ERR_HANDLER_SCRIPT) {
		return;
	}

	// Nothing from here on may print an error: the engine's error lock is held.
	self->record(p_function ? String(p_function) : String(), p_file ? String(p_file) : String(), p_line,
			p_error ? String(p_error) : String(), p_message ? String(p_message) : String(), p_type == ERR_HANDLER_WARNING);
}

void AIErrorMonitor::record(const String &p_function, const String &p_file, int p_line, const String &p_error, const String &p_message, bool p_is_warning) {
	// Errors of the assistant itself (a chat that could not be saved, a
	// checkpoint that could not be written) are its own business, not
	// something to hand to the model as the project's problem.
	static const char *own_files[] = {
		"ai_assistant", "ai_error_monitor", "ai_checkpoint", "ai_permission", "ai_web_search",
		"ai_image_gen", "ai_audio_gen", "ai_profiler", "ai_context", nullptr
	};
	for (int i = 0; own_files[i]; i++) {
		if (p_file.find(own_files[i]) >= 0) {
			return;
		}
	}
	// Some come from the engine's core but are caused by the panel.
	if (p_error.find("ai_assistant_chats") >= 0 || p_message.find("ai_assistant_chats") >= 0) {
		return;
	}

	ErrorEntry entry;
	entry.message = p_message.is_empty() ? p_error : p_error + ": " + p_message;
	entry.function = p_function;
	entry.file = p_file;
	entry.line = p_line;
	entry.is_warning = p_is_warning;
	// OS outlives every thread that can still print; Time does not.
	entry.timestamp = OS::get_singleton()->get_ticks_msec();

	MutexLock lock(mutex);
	entry.during_ai_execution = ai_execution_active;
	if (ai_execution_active) {
		ai_errors.push_back(entry);
	}
	all_errors.push_back(entry);
	while (all_errors.size() > max_buffer_size) {
		all_errors.remove_at(0);
	}
}

void AIErrorMonitor::begin_ai_execution() {
	MutexLock lock(mutex);
	ai_errors.clear();
	ai_execution_active = true;
}

void AIErrorMonitor::end_ai_execution() {
	MutexLock lock(mutex);
	ai_execution_active = false;
}

bool AIErrorMonitor::is_ai_executing() const {
	MutexLock lock(mutex);
	return ai_execution_active;
}

Vector<AIErrorMonitor::ErrorEntry> AIErrorMonitor::get_ai_execution_errors() const {
	MutexLock lock(mutex);
	return ai_errors;
}

bool AIErrorMonitor::has_ai_execution_errors() const {
	MutexLock lock(mutex);
	return !ai_errors.is_empty();
}

String AIErrorMonitor::get_ai_execution_errors_text(bool p_include_warnings) const {
	const Vector<ErrorEntry> errors = get_ai_execution_errors();
	if (errors.is_empty()) {
		return "";
	}

	// Build deduplicated output: track each unique formatted line and its count.
	Vector<String> unique_lines;
	Vector<int> counts;

	for (int i = 0; i < errors.size(); i++) {
		const ErrorEntry &e = errors[i];
		if (e.is_warning && !p_include_warnings) {
			continue;
		}

		String line;
		line += (e.is_warning ? "[WARNING] " : "[ERROR] ");
		line += e.message;
		if (!e.file.is_empty()) {
			line += " (at " + e.file + ":" + itos(e.line);
			if (!e.function.is_empty()) {
				line += " in " + e.function;
			}
			line += ")";
		}

		// Check for duplicate.
		int found = -1;
		for (int j = 0; j < unique_lines.size(); j++) {
			if (unique_lines[j] == line) {
				found = j;
				break;
			}
		}
		if (found >= 0) {
			counts.write[found]++;
		} else {
			unique_lines.push_back(line);
			counts.push_back(1);
		}
	}

	String result;
	for (int i = 0; i < unique_lines.size(); i++) {
		if (!result.is_empty()) {
			result += "\n";
		}
		result += unique_lines[i];
		if (counts[i] > 1) {
			result += " (x" + itos(counts[i]) + ")";
		}
	}
	return result;
}

String AIErrorMonitor::get_recent_console_errors(int p_count) const {
	Vector<ErrorEntry> errors;
	{
		MutexLock lock(mutex);
		errors = all_errors;
	}
	if (errors.is_empty()) {
		return "No recent errors or warnings in the console.";
	}

	String result;
	int start = MAX(0, errors.size() - p_count);
	int count = 0;
	for (int i = start; i < errors.size(); i++) {
		const ErrorEntry &e = errors[i];
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
	Vector<ErrorEntry> errors;
	{
		MutexLock lock(mutex);
		errors = all_errors;
	}
	int count = 0;
	for (int i = 0; i < errors.size(); i++) {
		if (!errors[i].during_ai_execution) {
			count++;
		}
	}
	return count;
}

void AIErrorMonitor::clear_ai_errors() {
	MutexLock lock(mutex);
	ai_errors.clear();
}

void AIErrorMonitor::receive_debugger_error(const String &p_message, const String &p_file, int p_line, bool p_is_warning) {
	MutexLock singleton_lock(singleton_mutex);
	AIErrorMonitor *self = singleton;
	if (!self) {
		return;
	}
	DebuggerErrorEntry entry;
	entry.message = p_message;
	entry.file = p_file;
	entry.line = p_line;
	entry.is_warning = p_is_warning;
	entry.timestamp = OS::get_singleton()->get_ticks_msec();

	MutexLock lock(self->mutex);
	self->debugger_errors.push_back(entry);
	while (self->debugger_errors.size() > self->max_debugger_buffer_size) {
		self->debugger_errors.remove_at(0);
	}
}

void AIErrorMonitor::receive_game_output(const String &p_line) {
	static const int MAX_LINES = 200;
	static const int MAX_LINE_CHARS = 500;
	MutexLock singleton_lock(singleton_mutex);
	AIErrorMonitor *self = singleton;
	if (!self) {
		return;
	}
	MutexLock lock(self->mutex);
	self->game_output.push_back(p_line.left(MAX_LINE_CHARS));
	self->game_output_total++;
	while (self->game_output.size() > MAX_LINES) {
		self->game_output.remove_at(0);
	}
}

uint64_t AIErrorMonitor::get_game_output_total() const {
	MutexLock lock(mutex);
	return game_output_total;
}

String AIErrorMonitor::get_game_output_since(uint64_t p_total, int p_max_lines) const {
	MutexLock lock(mutex);
	const int printed = (int)MIN(game_output_total - MIN(p_total, game_output_total), (uint64_t)game_output.size());
	const int count = MIN(printed, p_max_lines);
	String out;
	for (int i = game_output.size() - count; i < game_output.size(); i++) {
		out += game_output[i] + "\n";
	}
	return (printed > count ? "(" + itos(printed - count) + " earlier lines not shown)\n" : String()) + out;
}

String AIErrorMonitor::get_recent_debugger_errors(int p_count) const {
	Vector<DebuggerErrorEntry> errors;
	{
		MutexLock lock(mutex);
		errors = debugger_errors;
	}
	if (errors.is_empty()) {
		return "";
	}
	String result;
	int start = MAX(0, errors.size() - p_count);
	for (int i = start; i < errors.size(); i++) {
		const DebuggerErrorEntry &e = errors[i];
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
	MutexLock lock(mutex);
	return debugger_errors.size();
}

void AIErrorMonitor::clear_debugger_errors() {
	MutexLock lock(mutex);
	debugger_errors.clear();
}

void AIErrorMonitor::clear_all() {
	MutexLock lock(mutex);
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
	MutexLock singleton_lock(singleton_mutex);
	if (singleton == this) {
		singleton = nullptr;
	}
}

#endif // TOOLS_ENABLED
