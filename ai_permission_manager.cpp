#ifdef TOOLS_ENABLED

#include "ai_permission_manager.h"
#include "core/object/class_db.h"
#include "editor/settings/editor_settings.h"

void AIPermissionManager::_bind_methods() {
}

String AIPermissionManager::_category_name(Category p_cat) {
	switch (p_cat) {
		case PERM_CREATE_NODES: return "Create Nodes";
		case PERM_DELETE_NODES: return "Delete Nodes";
		case PERM_MODIFY_PROPERTIES: return "Modify Properties";
		case PERM_MODIFY_SCRIPTS: return "Modify Scripts";
		case PERM_CHANGE_PROJECT_SETTINGS: return "Change Project Settings";
		case PERM_FILE_WRITE: return "Write Files";
		case PERM_FILE_DELETE: return "Delete Files";
		default: return "Unknown";
	}
}

String AIPermissionManager::_category_setting_key(Category p_cat) {
	switch (p_cat) {
		case PERM_CREATE_NODES: return "ai_assistant/permissions/create_nodes";
		case PERM_DELETE_NODES: return "ai_assistant/permissions/delete_nodes";
		case PERM_MODIFY_PROPERTIES: return "ai_assistant/permissions/modify_properties";
		case PERM_MODIFY_SCRIPTS: return "ai_assistant/permissions/modify_scripts";
		case PERM_CHANGE_PROJECT_SETTINGS: return "ai_assistant/permissions/project_settings";
		case PERM_FILE_WRITE: return "ai_assistant/permissions/file_write";
		case PERM_FILE_DELETE: return "ai_assistant/permissions/file_delete";
		default: return "";
	}
}

void AIPermissionManager::set_permission(Category p_category, Level p_level) {
	if (p_category >= 0 && p_category < PERM_MAX) {
		permissions[p_category] = p_level;
	}
}

AIPermissionManager::Level AIPermissionManager::get_permission(Category p_category) const {
	if (p_category >= 0 && p_category < PERM_MAX) {
		return permissions[p_category];
	}
	return LEVEL_ALLOW;
}

void AIPermissionManager::load_from_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	for (int i = 0; i < PERM_MAX; i++) {
		String key = _category_setting_key((Category)i);
		if (!key.is_empty() && es->has_setting(key)) {
			permissions[i] = (Level)(int)es->get_setting(key);
		}
	}
}

void AIPermissionManager::save_to_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	for (int i = 0; i < PERM_MAX; i++) {
		String key = _category_setting_key((Category)i);
		if (!key.is_empty()) {
			es->set_setting(key, (int)permissions[i]);
		}
	}
	es->save();
}

// Speculative risk classifier — fast path before per-category permission checks.
// Mirrors Claude Code's bash_classifier: runs synchronously, classifies based
// on known-safe and known-dangerous patterns, and returns a tier that lets the
// caller skip the confirmation dialog for obviously low-risk operations.
AIPermissionManager::RiskTier AIPermissionManager::classify_risk(const String &p_code) {
	// ── Tier 3: BLOCKED — operations that are never allowed ──────────────────
	static const char *blocked_patterns[] = {
		"OS.execute", "OS.shell_open", "OS.kill", "OS.create_process",
		"OS.create_process", "OS.set_environment",
		"HTTPClient.new", // direct HTTP client (use HTTPRequest node instead)
		nullptr
	};
	for (int i = 0; blocked_patterns[i]; i++) {
		if (p_code.find(blocked_patterns[i]) != -1) {
			return RISK_BLOCKED;
		}
	}

	// ── Tier 2: DESTRUCTIVE — changes that can't be easily undone ────────────
	static const char *destructive_patterns[] = {
		"DirAccess.remove", "remove_absolute", "OS.move_to_trash",
		"queue_free()", ".free()",
		"remove_child(",
		"ProjectSettings.set_setting", "ProjectSettings.save",
		"InputMap.erase_action", "InputMap.action_erase_event",
		nullptr
	};
	for (int i = 0; destructive_patterns[i]; i++) {
		if (p_code.find(destructive_patterns[i]) != -1) {
			return RISK_DESTRUCTIVE;
		}
	}

	// ── Tier 0: READ-ONLY — pure inspection, no side effects ─────────────────
	// Heuristic: code only calls getters, print(), or inspects the scene tree.
	// We flag it as read-only ONLY when none of the write indicators are present.
	static const char *write_indicators[] = {
		"add_child", ".new()", "add_root_node",
		"ResourceSaver", "FileAccess.open", "store_string", "store_line",
		"set_script", "source_code",
		"add_theme_", "set(", "add_do_",
		".position =", ".rotation =", ".scale =", ".visible =",
		"PackedScene", "pack(",
		nullptr
	};
	bool has_write = false;
	for (int i = 0; write_indicators[i]; i++) {
		if (p_code.find(write_indicators[i]) != -1) {
			has_write = true;
			break;
		}
	}
	if (!has_write) {
		return RISK_READ_ONLY;
	}

	// Everything else that passed the blocked/destructive checks is additive.
	return RISK_ADDITIVE;
}

Vector<AIPermissionManager::Category> AIPermissionManager::categorize_code(const String &p_code) const {
	Vector<Category> result;

	// Detect node creation.
	if (p_code.find("add_child") != -1 || p_code.find(".new()") != -1 || p_code.find("add_root_node") != -1) {
		result.push_back(PERM_CREATE_NODES);
	}

	// Detect node deletion.
	if (p_code.find("remove_child") != -1 || p_code.find("queue_free") != -1 || p_code.find("free()") != -1) {
		result.push_back(PERM_DELETE_NODES);
	}

	// Detect property modification.
	if (p_code.find(".position") != -1 || p_code.find(".rotation") != -1 ||
			p_code.find(".scale") != -1 || p_code.find("set(") != -1 ||
			p_code.find("add_do_property") != -1) {
		result.push_back(PERM_MODIFY_PROPERTIES);
	}

	// Detect script modification.
	if (p_code.find("set_script") != -1 || p_code.find("source_code") != -1 ||
			p_code.find("GDScript.new") != -1) {
		result.push_back(PERM_MODIFY_SCRIPTS);
	}

	// Detect project settings.
	if (p_code.find("ProjectSettings") != -1) {
		result.push_back(PERM_CHANGE_PROJECT_SETTINGS);
	}

	// Detect file writes.
	if (p_code.find("ResourceSaver") != -1 || p_code.find("FileAccess.open") != -1 ||
			p_code.find("store_string") != -1) {
		result.push_back(PERM_FILE_WRITE);
	}

	// Detect file deletion.
	if (p_code.find("DirAccess.remove") != -1 || p_code.find("remove_absolute") != -1 ||
			p_code.find(".remove(") != -1 || p_code.find("OS.move_to_trash") != -1) {
		result.push_back(PERM_FILE_DELETE);
	}

	return result;
}

AIPermissionManager::PermissionResult AIPermissionManager::check_code_permissions(const String &p_code) const {
	PermissionResult result;
	result.categories = categorize_code(p_code);

	for (int i = 0; i < result.categories.size(); i++) {
		Level level = get_permission(result.categories[i]);
		if (level == LEVEL_DENY) {
			result.allowed = false;
			result.description = "Blocked: Operation '" + _category_name(result.categories[i]) + "' is denied by permission settings.";
			return result;
		}
		if (level == LEVEL_ASK) {
			result.needs_confirmation = true;
		}
	}

	if (result.needs_confirmation) {
		result.description = get_code_action_summary(result.categories);
	}

	return result;
}

String AIPermissionManager::get_code_action_summary(const Vector<Category> &p_categories) const {
	if (p_categories.is_empty()) {
		return "Execute code";
	}

	String summary = "This code will: ";
	for (int i = 0; i < p_categories.size(); i++) {
		if (i > 0) {
			summary += ", ";
		}
		summary += _category_name(p_categories[i]);
	}
	return summary;
}

AIPermissionManager::AIPermissionManager() {
	// Default: all operations allowed (matches existing behavior).
	for (int i = 0; i < PERM_MAX; i++) {
		permissions[i] = LEVEL_ALLOW;
	}
	load_from_settings();
}

#endif // TOOLS_ENABLED
