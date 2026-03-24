#pragma once

#ifdef TOOLS_ENABLED

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class AIPermissionManager : public RefCounted {
	GDCLASS(AIPermissionManager, RefCounted);

public:
	enum Category {
		PERM_CREATE_NODES,
		PERM_DELETE_NODES,
		PERM_MODIFY_PROPERTIES,
		PERM_MODIFY_SCRIPTS,
		PERM_CHANGE_PROJECT_SETTINGS,
		PERM_FILE_WRITE,
		PERM_FILE_DELETE,
		PERM_MAX
	};

	enum Level {
		LEVEL_ALLOW,  // Execute silently.
		LEVEL_ASK,    // Show confirmation dialog.
		LEVEL_DENY,   // Block the operation.
	};

	struct PermissionResult {
		bool allowed = true;
		bool needs_confirmation = false;
		String description; // What the code will do.
		Vector<Category> categories; // Which categories are involved.
	};

private:
	Level permissions[PERM_MAX];

	static String _category_name(Category p_cat);
	static String _category_setting_key(Category p_cat);

protected:
	static void _bind_methods();

public:
	// Set/get permission level for a category.
	void set_permission(Category p_category, Level p_level);
	Level get_permission(Category p_category) const;

	// Load/save permissions from EditorSettings.
	void load_from_settings();
	void save_to_settings();

	// Analyze code and check permissions.
	// Returns which categories the code touches.
	Vector<Category> categorize_code(const String &p_code) const;

	// Check if code is permitted. Returns a result with details.
	PermissionResult check_code_permissions(const String &p_code) const;

	// Get a human-readable summary of what the code will do.
	String get_code_action_summary(const Vector<Category> &p_categories) const;

	AIPermissionManager();
};

#endif // TOOLS_ENABLED
