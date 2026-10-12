#pragma once

#ifdef TOOLS_ENABLED

#include "ai_checkpoint_manager.h"
#include "ai_error_monitor.h"
#include "ai_permission_manager.h"
#include "ai_script_executor.h"

// The gate every piece of model-written code passes through, and the place it
// runs: review (syntax, safety policy, permissions), then execution wrapped in
// a checkpoint and error monitoring.
class AICodeRunner {
public:
	enum Action {
		ACTION_RUN, // Cleared to run.
		ACTION_CONFIRM, // The user has to allow it first.
		ACTION_FIX_SYNTAX, // Does not parse; ask the model to correct it.
		ACTION_FIX_SAFETY, // Violates the safety policy; ask for a compliant version.
		ACTION_REFUSE, // Blocked or denied; asking again would not help.
	};

	struct Review {
		Action action = ACTION_REFUSE;
		String code; // The code to run (with the automatic repairs applied).
		String message; // The error, or the confirmation prompt.
	};

	struct Result {
		bool success = false;
		String output;
		String printed; // What the code print()ed: its own account of what it did.
		String error;
		String summary; // One line describing what the code did.
		int checkpoint_id = -1; // For Revert; -1 if nothing could be snapshotted.
	};

	// p_confirm_everything: ask even when no permission category requires it
	// (auto-execute is off).
	Review review(const String &p_code, bool p_confirm_everything) const;

	// Runs reviewed code. Error monitoring stays on afterwards, because errors
	// raised by deferred calls and signal callbacks arrive after the code
	// returns; collect them with take_late_errors().
	Result execute(const String &p_code);
	// Ends monitoring and returns what the code raised since it ran ("" if clean).
	String take_late_errors();

	void reload_permissions() { permission_manager->load_from_settings(); }
	bool may_write_files() const;

	Ref<AICheckpointManager> get_checkpoint_manager() const { return checkpoint_manager; }
	Ref<AIPermissionManager> get_permission_manager() const { return permission_manager; }
	Ref<AIErrorMonitor> get_error_monitor() const { return error_monitor; }

	AICodeRunner();

private:
	Ref<AIScriptExecutor> script_executor;
	Ref<AIPermissionManager> permission_manager;
	Ref<AICheckpointManager> checkpoint_manager;
	Ref<AIErrorMonitor> error_monitor;
};

#endif // TOOLS_ENABLED
