#ifdef TOOLS_ENABLED

#include "ai_code_runner.h"

#include "ai_log.h"
#include "ai_response_analysis.h"

#include "editor/editor_interface.h"

static const int CHECKPOINT_LABEL_CHARS = 50;
// What AIScriptExecutor reports for a run without warnings.
static const char *PLAIN_SUCCESS_OUTPUT = "Script executed successfully.";

AICodeRunner::AICodeRunner() {
	script_executor.instantiate();
	permission_manager.instantiate();
	checkpoint_manager.instantiate();
	// The monitor hooks the engine's error handler and exposes itself as a
	// singleton (the debugger feeds it game errors), so a second controller in
	// the same editor shares the first one's instead of replacing it.
	error_monitor = Ref<AIErrorMonitor>(AIErrorMonitor::get_singleton());
	if (error_monitor.is_null()) {
		error_monitor.instantiate();
	}
}

bool AICodeRunner::may_write_files() const {
	return permission_manager->get_permission(AIPermissionManager::PERM_FILE_WRITE) != AIPermissionManager::LEVEL_DENY;
}

AICodeRunner::Review AICodeRunner::review(const String &p_code, bool p_confirm_everything) const {
	Review result;
	// Repair the mistakes models keep making, then let the GDScript parser have
	// the first word: a parse error is cheaper to fix before anything has run.
	result.code = script_executor->auto_fix_code(p_code);

	const String compile_error = script_executor->compile_check(result.code);
	if (!compile_error.is_empty()) {
		result.action = ACTION_FIX_SYNTAX;
		result.message = compile_error;
		return result;
	}

	const String safety_error = script_executor->check_safety(result.code);
	if (!safety_error.is_empty()) {
		result.action = ACTION_FIX_SAFETY;
		result.message = safety_error;
		return result;
	}

	const AIPermissionManager::RiskTier tier = AIPermissionManager::classify_risk(result.code);
	if (tier == AIPermissionManager::RISK_BLOCKED) {
		result.action = ACTION_REFUSE;
		result.message = AI_L("Code contains a blocked operation and was not executed.", "代码包含被禁止的操作，未执行。");
		return result;
	}

	// The per-category table is consulted for every tier: the risk tier only
	// adds friction, it never lets code skip a Deny or an Ask.
	const AIPermissionManager::PermissionResult permission = permission_manager->check_code_permissions(result.code);
	if (!permission.allowed) {
		result.action = ACTION_REFUSE;
		result.message = permission.description;
		return result;
	}
	if (!permission.needs_confirmation && !p_confirm_everything) {
		result.action = ACTION_RUN;
		return result;
	}

	result.action = ACTION_CONFIRM;
	if (!permission.description.is_empty()) {
		result.message = permission.description + TR(STR_PERM_PROCEED);
	} else if (tier == AIPermissionManager::RISK_DESTRUCTIVE) {
		result.message = AI_L("This code will make changes that cannot be easily undone.", "这段代码将做出难以撤销的更改。") + TR(STR_PERM_PROCEED);
	} else {
		result.message = TR(STR_PERM_EXECUTE_CONFIRM);
	}
	return result;
}

static bool _may_touch_files(const String &p_code) {
	static const char *indicators[] = {
		"res://", "user://", "FileAccess", "DirAccess", "ResourceSaver", "ResourceLoader", "store_", "save", "ProjectSettings",
		"ConfigFile", "rename", "remove", "copy", "make_dir", "move_to_trash", "load(", "pack(", "InputMap", "source_code", "reload", nullptr
	};
	for (int i = 0; indicators[i]; i++) {
		if (p_code.find(indicators[i]) >= 0) {
			return true;
		}
	}
	return false;
}

AICodeRunner::Result AICodeRunner::execute(const String &p_code) {
	const String description = "Before: " + p_code.get_slice("\n", 0).strip_edges().left(CHECKPOINT_LABEL_CHARS);
	const bool had_scene = EditorInterface::get_singleton()->get_edited_scene_root() != nullptr;

	// Snapshot the project's text files first; whichever checkpoint is created
	// for this run adopts it, so Revert also undoes file writes and deletes.
	// Copying the project's text files is the expensive part, and most code
	// only edits the scene. It is skipped only when nothing in the code
	// names a file, a path or a saving call.
	if (_may_touch_files(p_code)) {
		checkpoint_manager->begin_file_snapshot();
	} else {
		checkpoint_manager->skip_file_snapshot();
	}
	Result result;
	if (checkpoint_manager->create_checkpoint(description)) {
		result.checkpoint_id = checkpoint_manager->get_latest_id();
	}

	error_monitor->begin_ai_execution();
	const Dictionary outcome = script_executor->execute(p_code);
	result.success = outcome["success"];
	result.output = outcome["output"];
	result.error = outcome["error"];

	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (result.checkpoint_id < 0 && result.success && !had_scene && scene_root) {
		// The code opened a scene from nothing: reverting means closing it.
		const String scene_path = scene_root->get_scene_file_path();
		result.checkpoint_id = checkpoint_manager->create_scene_open_checkpoint(scene_path, "Opened: " + scene_path.get_file());
	} else if (result.checkpoint_id < 0) {
		// No scene involved; keep a files-only checkpoint so writes can be reverted.
		result.checkpoint_id = checkpoint_manager->create_files_checkpoint(description);
	}

	if (!result.success) {
		error_monitor->end_ai_execution();
		error_monitor->clear_ai_errors();
		return result;
	}
	// Warnings are worth showing; otherwise the executor only says that the
	// script ran, and what the code itself announces is the better summary.
	const String output = result.output.strip_edges();
	if (output.is_empty() || output == PLAIN_SUCCESS_OUTPUT) {
		result.summary = AIResponseAnalysis::summarize_code(p_code);
	} else {
		result.summary = output.replace("\n", "  |  ");
	}
	return result;
}

String AICodeRunner::take_late_errors() {
	error_monitor->end_ai_execution();
	// A warning is not a reason to make the model redo work that succeeded.
	const String errors = error_monitor->get_ai_execution_errors_text(false);
	error_monitor->clear_ai_errors();
	return errors;
}

#endif // TOOLS_ENABLED
