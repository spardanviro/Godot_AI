#pragma once

#include "../agent/ai_session.h"
#include "../execution/ai_permission_manager.h"
#include "../prompt/ai_system_prompt.h"
#include "../providers/ai_provider.h"
#include "../tools/ai_tool_values.h"
#include "../tools/ai_toolbox.h"

#include "core/io/json.h"
#include "scene/2d/node_2d.h"
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/gui/button.h"
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

// --- Files and permissions ---

TEST_CASE("[AIAssistant][ToolEdits] files the editor owns, secret stores and the project root are not written") {
	String reason;

	CHECK(AIToolbox::is_writable_project_file("res://scripts/player.gd", reason));
	CHECK(AIToolbox::is_writable_project_file("res://data/levels.json", reason));

	CHECK_FALSE(AIToolbox::is_writable_project_file("res://player.gd", reason));
	CHECK(reason.contains("folder"));
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

TEST_CASE("[AIAssistant][ToolEdits] the prompt template yields one variant or the other, without the markers") {
	const String prompt = "intro\n<!-- SCRIPT-ONLY -->\nwrite a code block\n<!-- END SCRIPT-ONLY -->\n<!-- TOOLS-ONLY -->\nuse the tools\n<!-- END TOOLS-ONLY -->\nshared rules\n";

	const String script = AISystemPrompt::select_variant(prompt, false);
	const String tools = AISystemPrompt::select_variant(prompt, true);

	CHECK(script == "intro\nwrite a code block\nshared rules\n");
	CHECK(tools == "intro\nuse the tools\nshared rules\n");
	CHECK(AISystemPrompt::select_variant("no markers\nat all", true) == "no markers\nat all");
}

} // namespace TestAIToolEdits
