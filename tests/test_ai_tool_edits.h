#pragma once

#include "../agent/ai_session.h"
#include "../execution/ai_permission_manager.h"
#include "../prompt/ai_knowledge.h"
#include "../prompt/ai_system_prompt.h"
#include "../providers/ai_provider.h"
#include "../tools/ai_tool_values.h"
#include "../tools/ai_toolbox.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/json.h"
#include "scene/2d/node_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "tests/test_macros.h"

namespace TestAIToolEdits {

static Dictionary _parse(const String &p_json) {
	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse(p_json) == OK);
	return json->get_data();
}

static AIToolCall _call(const String &p_id, const String &p_name, const String &p_arguments = "{}") {
	AIToolCall call;
	call.id = p_id;
	call.name = p_name;
	call.arguments = p_arguments;
	return call;
}

// Main (Node2D) > Enemy (Node2D) > Shape (CollisionShape2D); Start (Button)
struct Scene {
	Node2D *root = nullptr;
	AIToolbox toolbox;

	Scene() {
		root = memnew(Node2D);
		root->set_name("Main");
		Node2D *enemy = memnew(Node2D);
		enemy->set_name("Enemy");
		root->add_child(enemy);
		enemy->set_owner(root);
		CollisionShape2D *shape = memnew(CollisionShape2D);
		shape->set_name("Shape");
		enemy->add_child(shape);
		shape->set_owner(root);
		Button *start = memnew(Button);
		start->set_name("Start");
		root->add_child(start);
		start->set_owner(root);
		toolbox.set_scene_root_for_tests(root);
	}
	~Scene() { memdelete(root); }

	String run(const String &p_tool, const String &p_arguments, bool *r_ok = nullptr) {
		bool ok = false;
		const String result = toolbox.call(p_tool, p_arguments, ok);
		if (r_ok) {
			*r_ok = ok;
		}
		return result;
	}
};

// --- Values ---

TEST_CASE("[AIAssistant][ToolEdits] engine values can be written as text, as components or as a colour code") {
	Variant value;
	String error;

	CHECK(AIToolValues::to_variant("Vector2(100, 50)", Variant::VECTOR2, value, error));
	CHECK(value == Variant(Vector2(100, 50)));
	Array components;
	components.push_back(3.0);
	components.push_back(4.0);
	CHECK(AIToolValues::to_variant(components, Variant::VECTOR2, value, error));
	CHECK(value == Variant(Vector2(3, 4)));
	CHECK(AIToolValues::to_variant("#ff0000", Variant::COLOR, value, error));
	CHECK(value == Variant(Color(1, 0, 0)));
	CHECK(AIToolValues::to_variant(7.0, Variant::INT, value, error));
	CHECK(value.get_type() == Variant::INT);
	CHECK(AIToolValues::to_variant("hello", Variant::STRING, value, error));
	CHECK(AIToolValues::to_variant("Player/Sprite", Variant::NODE_PATH, value, error));
	CHECK(value.get_type() == Variant::NODE_PATH);
	// A whole number meant for a property of any type is an integer.
	CHECK(AIToolValues::to_variant(5.0, Variant::NIL, value, error));
	CHECK(value.get_type() == Variant::INT);
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] a property value cannot bring code with it") {
	Scene scene;
	Variant value;
	String error;
	bool as_object_text = true;
	bool as_resource = true;
	bool as_path = true;
	bool nested = true;

	// The value parser can build objects from text; a script made that way would never have been asked about.
	CHECK_FALSE(AIToolValues::to_variant("Object(GDScript,\"source_code\":\"extends Node\")", Variant::OBJECT, value, error));
	// Where anything is taken, such text stays what it is: text.
	CHECK(AIToolValues::to_variant("Object( GDScript )", Variant::NIL, value, error));
	CHECK(value.get_type() == Variant::STRING);
	CHECK_FALSE(AIToolValues::to_variant("[Resource(\"res://a.gd\")]", Variant::ARRAY, value, error));
	scene.run("set_node_properties", "{\"path\": \"Enemy\", \"properties\": {\"script\": \"Object(GDScript,\\\"source_code\\\":\\\"extends Node2D\\\")\"}}", &as_object_text);
	scene.run("set_node_properties", "{\"path\": \"Enemy\", \"properties\": {\"script\": {\"$resource\": \"GDScript\", \"source_code\": \"extends Node2D\"}}}", &as_resource);
	scene.run("set_node_properties", "{\"path\": \"Enemy\", \"properties\": {\"script\": \"res://icon.svg\"}}", &as_path);
	scene.run("create_node", "{\"class\": \"Node2D\", \"name\": \"Carrier\", \"properties\": {\"material\": {\"$resource\": \"CanvasItemMaterial\", \"script\": {\"$resource\": \"GDScript\"}}}}", &nested);

	CHECK_FALSE(as_object_text);
	CHECK_FALSE(as_resource);
	CHECK_FALSE(as_path);
	CHECK_FALSE(nested);
	CHECK(scene.root->get_node(NodePath("Enemy"))->get_script().is_null());
	CHECK_FALSE(scene.root->has_node(NodePath("Carrier")));
	String reason;
	CHECK_FALSE(AIToolValues::is_instantiable("GDScript", "Resource", reason));
	CHECK_FALSE(AIToolValues::is_instantiable("PackedScene", "Resource", reason));
	CHECK(AIToolValues::is_instantiable("RectangleShape2D", "Resource", reason));
}

TEST_CASE("[AIAssistant][ToolEdits] a value of the wrong kind is an error that says what was expected") {
	Variant value;
	String error;

	CHECK_FALSE(AIToolValues::to_variant("tall", Variant::VECTOR2, value, error));
	CHECK(error.contains("Vector2"));
	CHECK_FALSE(AIToolValues::to_variant(5.0, Variant::VECTOR2, value, error));
	Array three;
	three.resize(3);
	three.fill(1.0);
	CHECK_FALSE(AIToolValues::to_variant(three, Variant::VECTOR2, value, error));
	CHECK_FALSE(AIToolValues::to_variant("res://no_such_file.tres", Variant::OBJECT, value, error));
	CHECK_FALSE(AIToolValues::to_variant("not a path", Variant::OBJECT, value, error));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] a resource is created from its class and its own properties") {
	Variant value;
	String error;
	const Dictionary shape = _parse("{\"$resource\": \"RectangleShape2D\", \"size\": [32, 48]}");
	const Dictionary editor_class = _parse("{\"$resource\": \"EditorSettings\"}");
	const Dictionary not_a_resource = _parse("{\"$resource\": \"Node2D\"}");
	const Dictionary bad_property = _parse("{\"$resource\": \"RectangleShape2D\", \"radius\": 3}");

	const bool created = AIToolValues::to_variant(shape, Variant::OBJECT, value, error);
	const Ref<RectangleShape2D> rectangle = value;

	CHECK(created);
	REQUIRE(rectangle.is_valid());
	CHECK(rectangle->get_size() == Vector2(32, 48));
	CHECK_FALSE(AIToolValues::to_variant(editor_class, Variant::OBJECT, value, error));
	CHECK_FALSE(AIToolValues::to_variant(not_a_resource, Variant::OBJECT, value, error));
	CHECK_FALSE(AIToolValues::to_variant(bad_property, Variant::OBJECT, value, error));
	CHECK(error.contains("radius"));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] only engine classes of the right kind can be created") {
	String error;

	CHECK(AIToolValues::is_instantiable("Sprite2D", "Node", error));
	CHECK_FALSE(AIToolValues::is_instantiable("NoSuchNode", "Node", error));
	CHECK_FALSE(AIToolValues::is_instantiable("RectangleShape2D", "Node", error));
	CHECK_FALSE(AIToolValues::is_instantiable("CanvasItem", "Node", error)); // Abstract.
	CHECK_FALSE(AIToolValues::is_instantiable("AIAgentController", "Node", error));
	CHECK_FALSE(AIToolValues::is_instantiable("EditorPlugin", "Node", error));
}

// --- Scene tools ---

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] create_node adds a node owned by the scene, with its properties set") {
	Scene scene;
	bool ok = false;

	const String result = scene.run("create_node",
			"{\"class\": \"CollisionShape2D\", \"name\": \"Hit\", \"parent\": \"Enemy\", \"groups\": [\"hazards\"],"
			" \"properties\": {\"position\": \"Vector2(8, 16)\", \"shape\": {\"$resource\": \"RectangleShape2D\", \"size\": \"Vector2(10, 20)\"}}}",
			&ok);
	CollisionShape2D *hit = Object::cast_to<CollisionShape2D>(scene.root->get_node_or_null(NodePath("Enemy/Hit")));

	CHECK(ok);
	CHECK(result == "Created Enemy/Hit (CollisionShape2D).");
	REQUIRE(hit);
	CHECK(hit->get_owner() == scene.root);
	CHECK(hit->get_position() == Vector2(8, 16));
	CHECK(hit->is_in_group("hazards"));
	REQUIRE(hit->get_shape().is_valid());
	CHECK(Ref<RectangleShape2D>(hit->get_shape())->get_size() == Vector2(10, 20));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] a create_node that cannot be completed adds nothing") {
	Scene scene;
	const int children_before = scene.root->get_child_count();
	bool bad_property_ok = true;
	bool bad_class_ok = true;
	bool bad_parent_ok = true;

	const String bad_property = scene.run("create_node", "{\"class\": \"Node2D\", \"properties\": {\"no_such\": 1}}", &bad_property_ok);
	scene.run("create_node", "{\"class\": \"EditorPlugin\"}", &bad_class_ok);
	scene.run("create_node", "{\"class\": \"Node2D\", \"parent\": \"Nobody\"}", &bad_parent_ok);
	// A taken name gets a number, and the result says so.
	const String renamed = scene.run("create_node", "{\"class\": \"Node2D\", \"name\": \"Enemy\"}");

	CHECK_FALSE(bad_property_ok);
	CHECK(bad_property.contains("no property 'no_such'"));
	CHECK_FALSE(bad_class_ok);
	CHECK_FALSE(bad_parent_ok);
	CHECK(scene.root->get_child_count() == children_before + 1);
	CHECK(renamed.contains("was taken"));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] set_node_properties sets all of them or none") {
	Scene scene;
	Node2D *enemy = Object::cast_to<Node2D>(scene.root->get_node(NodePath("Enemy")));
	bool ok = false;
	bool partly_wrong_ok = true;

	const String result = scene.run("set_node_properties", "{\"path\": \"Enemy\", \"properties\": {\"position\": [5, 6], \"visible\": false}, \"add_groups\": [\"enemies\"]}", &ok);
	scene.run("set_node_properties", "{\"path\": \"Enemy\", \"properties\": {\"position\": [50, 60], \"rotation\": \"sideways\"}}", &partly_wrong_ok);

	CHECK(ok);
	CHECK(result.contains("position = Vector2(5, 6)"));
	CHECK(enemy->is_in_group("enemies"));
	CHECK_FALSE(enemy->is_visible());
	CHECK_FALSE(partly_wrong_ok);
	CHECK(enemy->get_position() == Vector2(5, 6)); // The valid half was not applied either.
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] rename, delete and connect check what they are asked to do") {
	Scene scene;
	bool root_delete_ok = true;
	bool clash_ok = true;
	bool no_signal_ok = true;
	bool connect_ok = false;

	scene.run("delete_node", "{\"path\": \".\"}", &root_delete_ok);
	scene.run("rename_node", "{\"path\": \"Start\", \"name\": \"Enemy\"}", &clash_ok);
	scene.run("connect_signal", "{\"source\": \"Start\", \"signal\": \"exploded\", \"target\": \".\", \"method\": \"_on_x\"}", &no_signal_ok);
	const String connected = scene.run("connect_signal", "{\"source\": \"Start\", \"signal\": \"pressed\", \"target\": \".\", \"method\": \"_on_start_pressed\"}", &connect_ok);
	const String again = scene.run("connect_signal", "{\"source\": \"Start\", \"signal\": \"pressed\", \"target\": \".\", \"method\": \"_on_start_pressed\"}");
	const String properties = scene.run("get_node_properties", "{\"path\": \"Start\"}");
	scene.run("rename_node", "{\"path\": \"Start\", \"name\": \"Go\"}");
	const String deleted = scene.run("delete_node", "{\"path\": \"Enemy\"}");

	CHECK_FALSE(root_delete_ok);
	CHECK_FALSE(clash_ok);
	CHECK_FALSE(no_signal_ok);
	CHECK(connect_ok);
	// The script that will hold the method does not exist yet; the model is told.
	CHECK(connected.contains("has no method '_on_start_pressed' yet"));
	CHECK(again.begins_with("Already connected"));
	CHECK(properties.contains("Connection: pressed -> .::_on_start_pressed()"));
	CHECK(scene.root->has_node(NodePath("Go")));
	CHECK(deleted == "Deleted Enemy and what was under it.");
	CHECK_FALSE(scene.root->has_node(NodePath("Enemy")));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] an anchor preset on a Control takes effect, as it does in the Inspector") {
	Scene scene;
	bool ok = false;

	scene.run("create_node", "{\"class\": \"Label\", \"name\": \"Title\", \"parent\": \"Start\", \"properties\": {\"text\": \"Hi\", \"anchors_preset\": 8}}", &ok);
	const Control *title = Object::cast_to<Control>(scene.root->get_node_or_null(NodePath("Start/Title")));
	scene.run("set_node_properties", "{\"path\": \"Start/Title\", \"properties\": {\"anchors_preset\": 5}}");

	CHECK(ok);
	REQUIRE(title);
	// Preset 5 is "centre top": anchored to the middle horizontally, the top vertically.
	CHECK(title->get_anchor(SIDE_LEFT) == doctest::Approx(0.5));
	CHECK(title->get_anchor(SIDE_RIGHT) == doctest::Approx(0.5));
	CHECK(title->get_anchor(SIDE_TOP) == doctest::Approx(0.0));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] a node moves to another parent with what is under it, and stays owned by the scene") {
	Scene scene;
	bool ok = false;

	const String moved = scene.run("move_node", "{\"path\": \"Enemy\", \"parent\": \"Start\"}", &ok);

	CHECK(ok);
	CHECK(moved.contains("Moved Enemy to Start/Enemy"));
	const Node *shape = scene.root->get_node_or_null(NodePath("Start/Enemy/Shape"));
	REQUIRE(shape);
	// Leaving the tree clears owners; without one a node is not saved with the scene.
	CHECK(shape->get_owner() == scene.root);
	CHECK(shape->get_parent()->get_owner() == scene.root);
	CHECK_FALSE(scene.root->has_node(NodePath("Enemy")));
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] a node is reordered under its parent, and cannot be moved where it cannot go") {
	Scene scene;
	bool reordered = false;
	bool under_itself = true;
	bool root_moved = true;
	bool clash = true;
	bool missing = true;

	scene.run("move_node", "{\"path\": \"Start\", \"parent\": \".\", \"index\": 0}", &reordered);
	scene.run("move_node", "{\"path\": \"Enemy\", \"parent\": \"Enemy/Shape\"}", &under_itself);
	scene.run("move_node", "{\"path\": \".\", \"parent\": \"Enemy\"}", &root_moved);
	scene.run("create_node", "{\"class\": \"Node2D\", \"name\": \"Start\", \"parent\": \"Enemy\"}");
	const String clash_result = scene.run("move_node", "{\"path\": \"Start\", \"parent\": \"Enemy\"}", &clash);
	scene.run("move_node", "{\"path\": \"Start\", \"parent\": \"Nowhere\"}", &missing);

	CHECK(reordered);
	CHECK(scene.root->get_child(0)->get_name() == StringName("Start"));
	CHECK_FALSE(under_itself);
	CHECK_FALSE(root_moved);
	CHECK_FALSE(clash);
	CHECK(clash_result.contains("already has a child named 'Start'"));
	CHECK_FALSE(missing);
	CHECK(scene.root->get_node_or_null(NodePath("Enemy/Shape")));
}

// --- The project ---

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] an input action is registered with its keys and buttons, once") {
	Scene scene;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String setting = "input/ai_test_jump";
	bool ok = false;
	bool again_ok = false;
	bool bad_key = true;
	bool built_in = true;
	bool empty = true;

	const String registered = scene.run("add_input_action", "{\"action\": \"ai_test_jump\", \"keys\": [\"Space\", \"w\"], \"mouse_buttons\": [\"left\"]}", &ok);
	const String again = scene.run("add_input_action", "{\"action\": \"ai_test_jump\", \"keys\": [\"Space\", \"Up\"]}", &again_ok);
	scene.run("add_input_action", "{\"action\": \"ai_test_jump\", \"keys\": [\"NotAKey\"]}", &bad_key);
	scene.run("add_input_action", "{\"action\": \"ui_accept\", \"keys\": [\"Space\"]}", &built_in);
	scene.run("add_input_action", "{\"action\": \"ai_test_empty\"}", &empty);

	CHECK(ok);
	CHECK(registered.contains("Registered the input action 'ai_test_jump'"));
	REQUIRE(settings->has_setting(setting));
	const Dictionary entry = settings->get_setting(setting);
	const Array events = entry["events"];
	// Space, W, the left button — and Up from the second call; Space is not there twice.
	REQUIRE(events.size() == 4);
	const Ref<InputEventKey> space = events[0];
	REQUIRE(space.is_valid());
	CHECK(space->get_physical_keycode() == Key::SPACE);
	const Ref<InputEventMouseButton> click = events[2];
	REQUIRE(click.is_valid());
	CHECK(click->get_button_index() == MouseButton::LEFT);
	CHECK(double(entry["deadzone"]) == doctest::Approx(0.2));
	CHECK(again_ok);
	CHECK(again.contains("Added to the input action"));
	CHECK(again.contains("Up"));
	CHECK_FALSE(again.contains("Space,"));
	CHECK_FALSE(bad_key);
	CHECK_FALSE(built_in);
	CHECK_FALSE(empty);
	CHECK_FALSE(settings->has_setting("input/ai_test_empty"));

	settings->set_setting(setting, Variant());
}

TEST_CASE("[AIAssistant][SceneTree][ToolEdits] a project setting that exists is changed to a value of its type; others are refused") {
	Scene scene;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String name = "display/window/size/viewport_width";
	const Variant before = settings->get_setting(name);
	bool ok = false;
	bool unknown = true;
	bool wrong_type = true;
	bool autoload = true;
	bool input = true;
	bool no_scene = true;

	const String changed = scene.run("set_project_setting", "{\"name\": \"display/window/size/viewport_width\", \"value\": 1280}", &ok);
	const int width = settings->get_setting(name);
	scene.run("set_project_setting", "{\"name\": \"display/window/size/viewport_wide\", \"value\": 1}", &unknown);
	scene.run("set_project_setting", "{\"name\": \"display/window/size/viewport_width\", \"value\": \"wide\"}", &wrong_type);
	scene.run("set_project_setting", "{\"name\": \"autoload/Boot\", \"value\": \"*res://boot.gd\"}", &autoload);
	scene.run("set_project_setting", "{\"name\": \"input/jump\", \"value\": {}}", &input);
	scene.run("set_project_setting", "{\"name\": \"application/run/main_scene\", \"value\": \"res://nowhere.tscn\"}", &no_scene);
	settings->set_setting(name, before);

	CHECK(ok);
	CHECK(width == 1280);
	CHECK(changed.contains("Set display/window/size/viewport_width to 1280"));
	CHECK_FALSE(unknown);
	CHECK_FALSE(wrong_type);
	CHECK_FALSE(autoload);
	CHECK_FALSE(input);
	CHECK_FALSE(no_scene);
}

// --- Files and permissions ---

TEST_CASE("[AIAssistant][ToolEdits] files the editor owns and secret stores are not written; the user's path is") {
	String reason;

	CHECK(AIToolbox::is_writable_project_file("res://scripts/player.gd", reason));
	CHECK(AIToolbox::is_writable_project_file("res://data/levels.json", reason));

	// Where files belong is advice to the model, not a rule against the user's own path.
	CHECK(AIToolbox::is_writable_project_file("res://player.gd", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("res://project.godot", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("res://scenes/main.tscn", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("res://android/export_presets.cfg", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("res://.godot/x.cfg", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("user://scripts/a.gd", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("res://scripts/../../a.gd", reason));
	CHECK_FALSE(AIToolbox::is_writable_project_file("res://bin/tool.exe", reason));
}

TEST_CASE("[AIAssistant][ToolEdits] each editing tool falls under one permission category; looking needs none") {
	const AIToolbox toolbox;

	CHECK(toolbox.get_permission_category(_call("1", "get_scene_tree")) == -1);
	CHECK(toolbox.get_permission_category(_call("1", "read_file", "{\"path\": \"res://a.gd\"}")) == -1);
	CHECK(toolbox.get_permission_category(_call("1", "create_node")) == AIPermissionManager::PERM_CREATE_NODES);
	CHECK(toolbox.get_permission_category(_call("1", "instantiate_scene")) == AIPermissionManager::PERM_CREATE_NODES);
	CHECK(toolbox.get_permission_category(_call("1", "set_node_properties")) == AIPermissionManager::PERM_MODIFY_PROPERTIES);
	CHECK(toolbox.get_permission_category(_call("1", "delete_node")) == AIPermissionManager::PERM_DELETE_NODES);
	CHECK(toolbox.get_permission_category(_call("1", "attach_script")) == AIPermissionManager::PERM_MODIFY_SCRIPTS);
	// A node created with a script on it is a script being attached.
	CHECK(toolbox.get_permission_category(_call("1", "create_node", "{\"class\": \"Node\", \"script\": \"res://s/a.gd\"}")) == AIPermissionManager::PERM_MODIFY_SCRIPTS);
	// Writing a script is not writing a data file.
	CHECK(toolbox.get_permission_category(_call("1", "write_file", "{\"path\": \"res://s/a.gd\"}")) == AIPermissionManager::PERM_MODIFY_SCRIPTS);
	CHECK(toolbox.get_permission_category(_call("1", "write_file", "{\"path\": \"res://d/a.json\"}")) == AIPermissionManager::PERM_FILE_WRITE);

	int looking = 0;
	for (const AIToolSpec &tool : toolbox.get_specs()) {
		looking += tool.is_read_only() ? 1 : 0;
		if (tool.kind == AIToolSpec::KIND_EDIT) {
			CHECK_MESSAGE(toolbox.get_permission_category(_call("1", tool.name)) >= 0, tool.name);
		}
	}
	CHECK(looking == toolbox.get_specs(true).size());
	CHECK(toolbox.get_kind("run_editor_script") == AIToolSpec::KIND_HOST);
	// The web is looked at, not changed: on offer in every mode, but not answered by the toolbox itself.
	CHECK(toolbox.get_kind("web_search") == AIToolSpec::KIND_WEB);
	CHECK(toolbox.get_kind("fetch_url") == AIToolSpec::KIND_WEB);
	bool ok = true;
	CHECK(toolbox.call("fetch_url", "{\"url\": \"https://example.com\"}", ok).contains("carried out by the agent"));
	CHECK_FALSE(ok);
	// What used to be text markers. A generated asset is a file written into the project.
	for (const char *name : { "generate_image", "generate_speech" }) {
		CHECK_MESSAGE(toolbox.get_kind(name) == AIToolSpec::KIND_HOST, name);
		CHECK_MESSAGE(toolbox.get_permission_category(_call("1", name)) == AIPermissionManager::PERM_FILE_WRITE, name);
	}
	// A note outlives the conversation: it has a permission of its own, which asks.
	CHECK(toolbox.get_permission_category(_call("1", "remember")) == AIPermissionManager::PERM_REMEMBER);
	CHECK(AIPermissionManager::get_default_level(AIPermissionManager::PERM_REMEMBER) == AIPermissionManager::LEVEL_ASK);
	CHECK(toolbox.get_permission_category(_call("1", "set_project_setting")) == AIPermissionManager::PERM_CHANGE_PROJECT_SETTINGS);
	CHECK(toolbox.get_permission_category(_call("1", "add_input_action")) == AIPermissionManager::PERM_CHANGE_PROJECT_SETTINGS);
	CHECK(toolbox.get_kind("write_file") == AIToolSpec::KIND_EDIT);
}

// --- Other providers ---

static AISession _session_with_round() {
	AISession session;
	session.push_user("where is the player?");
	Vector<AIToolCall> calls;
	AIToolCall first = _call("toolu_1", "get_node_properties", "{\"path\": \"Player\"}");
	first.provider_data = "signature-abc";
	calls.push_back(first);
	calls.push_back(_call("toolu_2", "get_errors"));
	session.push_assistant_tool_calls("Checking.", "", calls);
	session.push_tool_result(calls[0], "position = Vector2(1, 2)");
	session.push_tool_result(calls[1], "(no recent errors)");
	return session;
}

TEST_CASE("[AIAssistant][ToolEdits] Anthropic: tools, a round as content blocks, all results in one user turn") {
	const AISession session = _session_with_round();
	Ref<AIProvider> provider = AIProvider::create("anthropic");
	provider->set_tools(AIToolbox().get_specs(true));

	const Dictionary body = _parse(provider->build_stream_request_body("system", session.build_api_messages(true), ""));
	const Array messages = body["messages"];

	CHECK(provider->supports_tools());
	CHECK(Dictionary(Array(body["tools"])[0]).has("input_schema"));
	REQUIRE(messages.size() == 3); // user, assistant, results — no empty user turn after.
	const Array assistant = Dictionary(messages[1])["content"];
	REQUIRE(assistant.size() == 3);
	CHECK(String(Dictionary(assistant[0])["type"]) == "text");
	CHECK(String(Dictionary(assistant[1])["type"]) == "tool_use");
	CHECK(String(Dictionary(assistant[1])["id"]) == "toolu_1");
	CHECK(String(Dictionary(Dictionary(assistant[1])["input"])["path"]) == "Player");
	CHECK(String(Dictionary(messages[2])["role"]) == "user");
	const Array results = Dictionary(messages[2])["content"];
	REQUIRE(results.size() == 2);
	CHECK(String(Dictionary(results[1])["type"]) == "tool_result");
	CHECK(String(Dictionary(results[1])["tool_use_id"]) == "toolu_2");
}

TEST_CASE("[AIAssistant][ToolEdits] Anthropic: a call is read from its content block and its argument pieces") {
	Ref<AIProvider> provider = AIProvider::create("anthropic");
	AIToolCallAccumulator accumulator;
	const char *events[] = {
		"{\"type\":\"content_block_start\",\"index\":1,\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_9\",\"name\":\"read_file\",\"input\":{}}}",
		"{\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"path\\\": \\\"res\"}}",
		"{\"type\":\"content_block_delta\",\"index\":1,\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"://a.gd\\\"}\"}}",
		"{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}",
	};

	bool cut_off = false;
	for (const char *event : events) {
		const Dictionary delta = provider->parse_stream_delta_ex(event);
		cut_off = cut_off || delta.has("finish");
		const Array fragments = delta.get("tool_calls", Array());
		for (int i = 0; i < fragments.size(); i++) {
			const Dictionary fragment = fragments[i];
			accumulator.add(fragment["index"], fragment.get("id", ""), fragment.get("name", ""), fragment.get("arguments", ""));
		}
	}
	const Vector<AIToolCall> calls = accumulator.get_calls();

	REQUIRE(calls.size() == 1);
	CHECK(calls[0].id == "toolu_9");
	CHECK(calls[0].arguments == "{\"path\": \"res://a.gd\"}");
	CHECK_FALSE(cut_off);
}

TEST_CASE("[AIAssistant][ToolEdits] Gemini: declarations, a round as function parts, the signature handed back") {
	const AISession session = _session_with_round();
	Ref<AIProvider> provider = AIProvider::create("gemini");
	provider->set_tools(AIToolbox().get_specs(true));

	const Dictionary body = _parse(provider->build_stream_request_body("system", session.build_api_messages(true), ""));
	const Array contents = body["contents"];
	const Array declarations = Dictionary(Array(body["tools"])[0])["functionDeclarations"];

	CHECK(provider->supports_tools());
	CHECK(Dictionary(declarations[0]).has("parametersJsonSchema"));
	REQUIRE(contents.size() == 3);
	CHECK(String(Dictionary(contents[1])["role"]) == "model");
	const Array model_parts = Dictionary(contents[1])["parts"];
	REQUIRE(model_parts.size() == 3);
	const Dictionary call_part = model_parts[1];
	CHECK(String(Dictionary(call_part["functionCall"])["name"]) == "get_node_properties");
	CHECK(String(Dictionary(Dictionary(call_part["functionCall"])["args"])["path"]) == "Player");
	CHECK(String(call_part["thoughtSignature"]) == "signature-abc");
	CHECK_FALSE(Dictionary(model_parts[2]).has("thoughtSignature"));
	const Array response_parts = Dictionary(contents[2])["parts"];
	REQUIRE(response_parts.size() == 2);
	const Dictionary response = Dictionary(response_parts[0])["functionResponse"];
	CHECK(String(response["name"]) == "get_node_properties");
	CHECK(String(Dictionary(response["response"])["result"]) == "position = Vector2(1, 2)");
}

TEST_CASE("[AIAssistant][ToolEdits] Gemini: calls arrive whole, each its own, with their signatures") {
	Ref<AIProvider> provider = AIProvider::create("gemini");
	AIToolCallAccumulator accumulator;
	const char *events[] = {
		"{\"candidates\":[{\"content\":{\"parts\":[{\"functionCall\":{\"name\":\"read_file\",\"args\":{\"path\":\"res://a.gd\"}},\"thoughtSignature\":\"sig-1\"}]}}]}",
		"{\"candidates\":[{\"content\":{\"parts\":[{\"functionCall\":{\"name\":\"get_errors\",\"args\":{}}}]},\"finishReason\":\"STOP\"}]}",
	};

	for (const char *event : events) {
		const Dictionary delta = provider->parse_stream_delta_ex(event);
		const Array fragments = delta.get("tool_calls", Array());
		for (int i = 0; i < fragments.size(); i++) {
			const Dictionary fragment = fragments[i];
			REQUIRE(bool(fragment.get("whole", false)));
			accumulator.add_whole(fragment.get("id", ""), fragment["name"], fragment["arguments"], fragment.get("provider_data", ""));
		}
	}
	const Vector<AIToolCall> calls = accumulator.get_calls();

	REQUIRE(calls.size() == 2);
	CHECK(calls[0].name == "read_file");
	CHECK(String(_parse(calls[0].arguments)["path"]) == "res://a.gd");
	CHECK(calls[0].provider_data == "sig-1");
	CHECK(calls[0].id != calls[1].id);
	CHECK(calls[1].name == "get_errors");
}

// --- Prompt variants ---

TEST_CASE("[AIAssistant][ToolEdits] the prompt template yields one variant, without the marker lines") {
	const String prompt = "intro\n<!-- SCRIPT-ONLY -->\nwrite a code block\n<!-- END SCRIPT-ONLY -->\n<!-- TOOLS-ONLY -->\nuse the tools\n<!-- END TOOLS-ONLY -->\n"
						  "<!-- MARKERS-ONLY -->\n[WEB_SEARCH: query]\n<!-- END MARKERS-ONLY -->\nshared rules\n";

	const String script = AISystemPrompt::select_variant(prompt, AISystemPrompt::VARIANT_SCRIPT);
	const String tools = AISystemPrompt::select_variant(prompt, AISystemPrompt::VARIANT_TOOLS);

	CHECK(script == "intro\nwrite a code block\n[WEB_SEARCH: query]\nshared rules\n");
	CHECK(tools == "intro\nuse the tools\nshared rules\n");
	CHECK(AISystemPrompt::select_variant("no markers\nat all", AISystemPrompt::VARIANT_TOOLS) == "no markers\nat all");
}

TEST_CASE("[AIAssistant][ToolEdits] the shipped prompt teaches the text markers only to a model without tools") {
	const String prompt = AIKnowledge::get_singleton()->get_system_prompt();

	const String script = AISystemPrompt::select_variant(prompt, AISystemPrompt::VARIANT_SCRIPT);
	const String tools = AISystemPrompt::select_variant(prompt, AISystemPrompt::VARIANT_TOOLS);

	for (const char *marker : { "[WEB_SEARCH:", "[FETCH_URL:", "[GENERATE_IMAGE:", "[GENERATE_AUDIO:", "[WRITE_MEMORY:", "[PLAN]" }) {
		CHECK_MESSAGE(script.contains(marker), marker);
		CHECK_FALSE_MESSAGE(tools.contains(marker), marker);
	}
	// Both are told that the message, not a mode, decides; the one with tools plans with a tool.
	CHECK(script.contains("There is no mode switch"));
	CHECK(tools.contains("There is no mode switch"));
	CHECK(tools.contains("propose_plan"));
	CHECK_FALSE(tools.contains("-ONLY -->"));
	// The variant for tools is the short one: it has no code blocks to teach.
	CHECK(tools.length() < script.length() / 2);
	CHECK(tools.length() < 16000);
}

} // namespace TestAIToolEdits
