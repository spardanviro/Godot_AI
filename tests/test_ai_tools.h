#pragma once

#include "../agent/ai_session.h"
#include "../common/ai_tool_types.h"
#include "../providers/ai_provider.h"
#include "../tools/ai_toolbox.h"

#include "core/io/json.h"
#include "scene/2d/node_2d.h"
#include "scene/main/node.h"
#include "tests/test_macros.h"

namespace TestAITools {

static AIToolCall _call(const String &p_id, const String &p_name, const String &p_arguments = "{}") {
	AIToolCall call;
	call.id = p_id;
	call.name = p_name;
	call.arguments = p_arguments;
	return call;
}

static Dictionary _parse(const String &p_json) {
	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse(p_json) == OK);
	return json->get_data();
}

// Main (Node2D) > Player (Node2D, at 120,80, in group "heroes") > Sprite (Node)
struct Scene {
	Node2D *root = nullptr;
	Node2D *player = nullptr;

	Scene() {
		root = memnew(Node2D);
		root->set_name("Main");
		player = memnew(Node2D);
		player->set_name("Player");
		player->set_position(Vector2(120, 80));
		root->add_child(player);
		player->add_to_group("heroes", true);
		Node *sprite = memnew(Node);
		sprite->set_name("Sprite");
		player->add_child(sprite);
	}
	~Scene() { memdelete(root); }
};

// --- Toolbox ---

TEST_CASE("[AIAssistant][Tools] every tool has a name, a description and an object schema, and only reads") {
	const AIToolbox toolbox;
	const Vector<AIToolSpec> specs = toolbox.get_specs(true);

	REQUIRE(specs.size() == 7); // Five that look at the project, two that look at the web.
	for (const AIToolSpec &tool : specs) {
		CHECK_FALSE(tool.name.is_empty());
		CHECK(tool.description.length() > 20);
		CHECK(String(tool.parameters.get("type", "")) == "object");
		CHECK(tool.parameters.has("properties"));
		CHECK(tool.is_read_only());
		CHECK(toolbox.has_tool(tool.name));
	}
	CHECK_FALSE(toolbox.has_tool("delete_everything"));
}

TEST_CASE("[AIAssistant][SceneTree][Tools] get_scene_tree lists paths by indentation and stops at max_depth") {
	Scene scene;
	AIToolbox toolbox;
	toolbox.set_scene_root_for_tests(scene.root);
	bool ok = false;

	const String whole = toolbox.call("get_scene_tree", "", ok);
	const bool whole_ok = ok;
	const String shallow = toolbox.call("get_scene_tree", "{\"max_depth\": 1}", ok);

	CHECK(whole_ok);
	CHECK(whole == "Main (Node2D)\n  Player (Node2D)\n    Sprite (Node)\n");
	CHECK(shallow == "Main (Node2D)\n  Player (Node2D) [1 children not shown]\n");
}

TEST_CASE("[AIAssistant][SceneTree][Tools] get_node_properties reads named properties and what differs from the default") {
	Scene scene;
	AIToolbox toolbox;
	toolbox.set_scene_root_for_tests(scene.root);
	bool ok = false;

	const String named = toolbox.call("get_node_properties", "{\"path\": \"Player\", \"properties\": [\"position\", \"no_such\"]}", ok);
	const bool named_ok = ok;
	const String changed = toolbox.call("get_node_properties", "{\"path\": \"Main/Player\"}", ok);
	const String root = toolbox.call("get_node_properties", "{\"path\": \".\"}", ok);

	CHECK(named_ok);
	CHECK(named.contains("Class: Node2D"));
	CHECK(named.contains("Groups: heroes"));
	CHECK(named.contains("Children: Sprite"));
	CHECK(named.contains("  position = Vector2(120, 80)"));
	CHECK(named.contains("  no_such = (no such property)"));
	// Only what was changed; rotation and scale are at their defaults.
	CHECK(changed.contains("  position = Vector2(120, 80)"));
	CHECK_FALSE(changed.contains("rotation"));
	CHECK(root.contains("Node: .\n"));
	CHECK(root.contains("Children: Player"));
}

TEST_CASE("[AIAssistant][SceneTree][Tools] what goes wrong is a result the model can read, not a failure of the run") {
	Scene scene;
	AIToolbox toolbox;
	toolbox.set_scene_root_for_tests(scene.root);
	bool ok = true;

	const String unknown = toolbox.call("delete_everything", "{}", ok);
	const bool unknown_ok = ok;
	const String missing = toolbox.call("get_node_properties", "{\"path\": \"Nobody\"}", ok);
	const bool missing_ok = ok;
	const String bad_json = toolbox.call("get_scene_tree", "{\"max_depth\": ", ok);
	const bool bad_json_ok = ok;
	const String no_path = toolbox.call("get_node_properties", "{}", ok);
	const String outside = toolbox.call("get_node_properties", "{\"path\": \"..\"}", ok);
	const bool outside_ok = ok;

	CHECK_FALSE(unknown_ok);
	CHECK(unknown.contains("there is no tool named 'delete_everything'"));
	CHECK_FALSE(missing_ok);
	CHECK(missing.contains("get_scene_tree"));
	CHECK_FALSE(bad_json_ok);
	CHECK(bad_json.begins_with("Error:"));
	CHECK(no_path.contains("'path' is required"));
	// The scene root's parent is not part of the scene.
	CHECK_FALSE(outside_ok);
}

TEST_CASE("[AIAssistant][Tools] without a scene the scene tools say so") {
	AIToolbox toolbox;
	bool ok = true;

	const String tree = toolbox.call("get_scene_tree", "{}", ok);

	CHECK_FALSE(ok);
	CHECK(tree.contains("no scene is open"));
}

TEST_CASE("[AIAssistant][Tools] only project text files that are not secret stores may be read") {
	String reason;

	CHECK(AIToolbox::is_readable_project_file("res://player.gd", reason));
	CHECK(AIToolbox::is_readable_project_file("res://scenes/main.tscn", reason));
	CHECK(AIToolbox::is_readable_project_file("res://project.godot", reason));

	CHECK_FALSE(AIToolbox::is_readable_project_file("C:/Windows/win.ini", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("user://settings.cfg", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://../outside.gd", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://export_presets.cfg", reason));
	CHECK(reason.contains("passwords"));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://android/Export_Presets.cfg", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://.env", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://.godot/editor/cache.cfg", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://icon.png", reason));
	CHECK_FALSE(AIToolbox::is_readable_project_file("res://keystore.jks", reason));
}

TEST_CASE("[AIAssistant][Tools] a call is described in one line for the transcript") {
	CHECK(AIToolbox::describe_call(_call("1", "get_scene_tree")) == "get_scene_tree()");
	CHECK(AIToolbox::describe_call(_call("1", "get_node_properties", "{\"path\": \"Player\"}")) == "get_node_properties(path: Player)");
	CHECK(AIToolbox::describe_call(_call("1", "read_file", "not json")) == "read_file()");
}

// --- Fragments ---

TEST_CASE("[AIAssistant][Tools] interleaved fragments are put together by index") {
	AIToolCallAccumulator accumulator;

	accumulator.add(0, "call_a", "read_file", "");
	accumulator.add(1, "call_b", "get_errors", "");
	accumulator.add(0, "", "", "{\"path\": \"res://");
	accumulator.add(1, "", "", "{}");
	accumulator.add(0, "", "", "a.gd\"}");
	accumulator.add(5, "", "", "stray arguments"); // Never named: not a call.
	accumulator.add(1000, "call_x", "far_away", "{}"); // Not an index a reply has.
	const Vector<AIToolCall> calls = accumulator.get_calls();

	REQUIRE(calls.size() == 2);
	CHECK(calls[0].id == "call_a");
	CHECK(calls[0].name == "read_file");
	CHECK(calls[0].arguments == "{\"path\": \"res://a.gd\"}");
	CHECK(calls[1].name == "get_errors");
	CHECK(calls[1].arguments == "{}");
}

// --- Provider ---

TEST_CASE("[AIAssistant][Tools] a request offers the tools only when some are set") {
	Ref<AIProvider> provider = AIProvider::create("openai");
	const String plain = provider->build_stream_request_body("system", Array(), "hello");

	provider->set_tools(AIToolbox().get_specs(true));
	const Dictionary body = _parse(provider->build_stream_request_body("system", Array(), "hello"));

	CHECK_FALSE(_parse(plain).has("tools"));
	REQUIRE(Array(body["tools"]).size() == 7);
	const Dictionary first = Array(body["tools"])[0];
	CHECK(String(first["type"]) == "function");
	CHECK(String(Dictionary(first["function"])["name"]) == "get_scene_tree");
	CHECK(Dictionary(first["function"]).has("parameters"));
	CHECK(provider->supports_tools());
	CHECK(AIProvider::create("anthropic")->supports_tools());
	CHECK(AIProvider::create("gemini")->supports_tools());
}

TEST_CASE("[AIAssistant][Tools] a tool round goes out in the API's own form, and a continuation adds no user turn") {
	AISession session;
	session.push_user("where is the player?");
	Vector<AIToolCall> calls;
	calls.push_back(_call("call_1", "get_node_properties", "{\"path\": \"Player\"}"));
	session.push_assistant_tool_calls("", "", calls);
	session.push_tool_result(calls[0], "position = Vector2(1, 2)");
	Ref<AIProvider> provider = AIProvider::create("deepseek");
	provider->set_tools(AIToolbox().get_specs(true));

	const Dictionary body = _parse(provider->build_stream_request_body("system", session.build_api_messages(true), ""));
	const Array messages = body["messages"];

	REQUIRE(messages.size() == 4); // system, user, assistant, tool — and nothing after.
	const Dictionary assistant = messages[2];
	CHECK(String(assistant["role"]) == "assistant");
	CHECK(assistant["content"].get_type() == Variant::NIL);
	const Dictionary wire_call = Array(assistant["tool_calls"])[0];
	CHECK(String(wire_call["id"]) == "call_1");
	CHECK(String(wire_call["type"]) == "function");
	CHECK(String(Dictionary(wire_call["function"])["name"]) == "get_node_properties");
	CHECK(String(Dictionary(wire_call["function"])["arguments"]) == "{\"path\": \"Player\"}");
	const Dictionary tool = messages[3];
	CHECK(String(tool["role"]) == "tool");
	CHECK(String(tool["tool_call_id"]) == "call_1");
	CHECK(String(tool["content"]) == "position = Vector2(1, 2)");
	CHECK_FALSE(tool.has("name"));
}

TEST_CASE("[AIAssistant][Tools] tool call fragments are read out of a stream delta") {
	Ref<AIProvider> provider = AIProvider::create("openai");

	const Dictionary opening = provider->parse_stream_delta_ex(
			"{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_9\",\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"arguments\":\"\"}}]}}]}");
	const Dictionary more = provider->parse_stream_delta_ex(
			"{\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"{\\\"pa\"}}]}}]}");
	const Dictionary text = provider->parse_stream_delta_ex("{\"choices\":[{\"delta\":{\"content\":\"hi\"},\"finish_reason\":\"tool_calls\"}]}");

	const Dictionary first = Array(opening["tool_calls"])[0];
	CHECK(int(first["index"]) == 0);
	CHECK(String(first["id"]) == "call_9");
	CHECK(String(first["name"]) == "read_file");
	const Dictionary second = Array(more["tool_calls"])[0];
	CHECK(String(second["id"]).is_empty());
	CHECK(String(second["arguments"]) == "{\"pa");
	CHECK_FALSE(text.has("tool_calls"));
	// Ending for tool calls is not being cut off.
	CHECK_FALSE(text.has("finish"));
}

// --- Session ---

static AISession _session_with_round() {
	AISession session;
	session.push_user("question");
	Vector<AIToolCall> calls;
	calls.push_back(_call("a", "get_scene_tree"));
	calls.push_back(_call("b", "read_file", "{\"path\": \"res://x.gd\"}"));
	session.push_assistant_tool_calls("Let me look.", "", calls);
	session.push_tool_result(calls[0], "Main (Node2D)");
	session.push_tool_result(calls[1], "1: extends Node");
	session.push_assistant("Here is the answer.");
	return session;
}

TEST_CASE("[AIAssistant][Tools] for a provider without tools a round is written out as ordinary turns") {
	const AISession session = _session_with_round();

	const Array messages = session.build_api_messages(false);

	REQUIRE(messages.size() == 4);
	const Dictionary assistant = messages[1];
	const Dictionary results = messages[2];
	CHECK(String(assistant["role"]) == "assistant");
	CHECK(String(assistant["content"]).begins_with("Let me look.\n[Called tool get_scene_tree {}]"));
	CHECK(String(assistant["content"]).contains("[Called tool read_file {\"path\": \"res://x.gd\"}]"));
	CHECK_FALSE(assistant.has("tool_calls"));
	// One user turn for all results keeps the roles alternating.
	CHECK(String(results["role"]) == "user");
	CHECK(String(results["content"]).contains("--- get_scene_tree ---\nMain (Node2D)"));
	CHECK(String(results["content"]).contains("--- read_file ---\n1: extends Node"));
	CHECK(String(Dictionary(messages[3])["content"]) == "Here is the answer.");
}

TEST_CASE("[AIAssistant][Tools] a round that is no longer whole is never sent as tool messages") {
	AISession session;
	session.push_user("question");
	Vector<AIToolCall> calls;
	calls.push_back(_call("a", "get_scene_tree"));
	calls.push_back(_call("b", "get_errors"));
	session.push_assistant_tool_calls("", "", calls);
	session.push_tool_result(calls[0], "Main (Node2D)"); // The second result never came.

	const Array messages = session.build_api_messages(true);

	for (int i = 0; i < messages.size(); i++) {
		const Dictionary message = messages[i];
		CHECK_MESSAGE(String(message["role"]) != "tool", "a tool message without its whole round is rejected by the API");
		CHECK_FALSE(message.has("tool_calls"));
	}
	CHECK(String(Dictionary(messages[2])["content"]).contains("Main (Node2D)"));
}

TEST_CASE("[AIAssistant][Tools] compressing away the call leaves its result as plain text") {
	AISession session = _session_with_round();
	// Drops the question and the assistant message that carried the calls.
	const AISession::DropResult dropped = session.drop_oldest_for_gap(1);
	session.drop_oldest_for_gap(1);

	const Array messages = session.build_api_messages(true);

	CHECK(dropped.dropped_messages >= 1);
	for (int i = 0; i < messages.size(); i++) {
		CHECK(String(Dictionary(messages[i])["role"]) != "tool");
	}
}

TEST_CASE("[AIAssistant][Tools] a round is saved with the chat and summarised by what was looked up") {
	const AISession session = _session_with_round();
	AISession reloaded;

	const bool loaded = reloaded.from_dict(session.to_dict());
	const Dictionary stored_call_message = reloaded.get_full_history()[1];
	Dictionary silent;
	silent["role"] = "assistant";
	silent["content"] = "";
	silent["tool_calls"] = stored_call_message["tool_calls"];

	CHECK(loaded);
	CHECK(reloaded.build_api_messages(true).size() == session.build_api_messages(true).size());
	CHECK(Array(stored_call_message["tool_calls"]).size() == 2);
	CHECK(AISession::summarize_message(silent) == "AI: [looked up: get_scene_tree, read_file]");
	CHECK(AISession::summarize_message(reloaded.get_full_history()[2]).is_empty());
	// The title is still the user's first message.
	CHECK(reloaded.get_title() == "question");
}

} // namespace TestAITools
