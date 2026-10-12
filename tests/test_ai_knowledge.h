#pragma once

#include "../ai_api_doc_loader.h"
#include "../ai_domain_prompts.h"
#include "../ai_gotchas_index.h"
#include "../ai_knowledge.h"
#include "../ai_system_prompt.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestAIKnowledge {

static void _write_file(const String &p_path, const String &p_text) {
	DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(f.is_valid());
	f->store_string(p_text);
}

static void _remove_dir(const String &p_dir) {
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_valid()) {
		dir->erase_contents_recursive();
		DirAccess::remove_absolute(p_dir);
	}
}

static AIKnowledge::Gotcha _gotcha(const String &p_keywords, const String &p_content) {
	AIKnowledge::Gotcha gotcha;
	gotcha.keywords = p_keywords.split(",");
	gotcha.content = p_content;
	return gotcha;
}

static AIKnowledge::Domain _domain(const String &p_id, bool p_whole_word, const String &p_keywords) {
	AIKnowledge::Domain domain;
	domain.id = p_id;
	domain.whole_word = p_whole_word;
	domain.keywords = p_keywords.split(",");
	domain.body = "[" + p_id + "]";
	return domain;
}

// --- Formats ---

TEST_CASE("[AIAssistant][Knowledge] parse_gotchas reads keywords and keeps entry text verbatim") {
	const String text =
			"Notes before the first entry are ignored.\n"
			"<!-- keywords: move_and_slide, CharacterBody2D -->\n"
			"### No delta\nmove_and_slide() already applies delta.\n\n"
			"<!-- keywords: Tween -->\n"
			"### Tweens\nCreate tweens with create_tween().\n";

	const Vector<AIKnowledge::Gotcha> entries = AIKnowledge::parse_gotchas(text);

	REQUIRE(entries.size() == 2);
	CHECK(entries[0].keywords.size() == 2);
	CHECK(entries[0].keywords[1] == "CharacterBody2D");
	CHECK(entries[0].content == "### No delta\nmove_and_slide() already applies delta.\n\n");
	CHECK(entries[1].content == "### Tweens\nCreate tweens with create_tween().\n");
}

TEST_CASE("[AIAssistant][Knowledge] parse_gotchas normalises CRLF and skips entries without content or keywords") {
	const String text =
			"<!-- keywords: A -->\r\nbody a\r\n"
			"<!-- keywords: -->\r\nno keywords\r\n"
			"<!-- keywords: C -->\r\n\r\n";

	const Vector<AIKnowledge::Gotcha> entries = AIKnowledge::parse_gotchas(text);

	REQUIRE(entries.size() == 1);
	CHECK(entries[0].content == "body a\n");
}

TEST_CASE("[AIAssistant][Knowledge] parse_domain reads front matter and keeps the body verbatim") {
	const String text =
			"---\n"
			"id: tilemap\n"
			"order: 20\n"
			"match: stem\n"
			"keywords:\n"
			"  - TileMap\n"
			"  - tile set\n"
			"---\n"
			"\n## Tiles\nUse TileMapLayer.\n";
	AIKnowledge::Domain domain;
	String error;

	const bool parsed = AIKnowledge::parse_domain(text, "fallback", domain, error);

	CHECK(parsed);
	CHECK(domain.id == "tilemap");
	CHECK(domain.order == 20);
	CHECK_FALSE(domain.whole_word);
	REQUIRE(domain.keywords.size() == 2);
	CHECK(domain.keywords[0] == "tilemap"); // Lowercased for matching.
	CHECK(domain.keywords[1] == "tile set");
	CHECK(domain.body == "\n## Tiles\nUse TileMapLayer.\n");
}

TEST_CASE("[AIAssistant][Knowledge] parse_domain takes the id from the file name and accepts inline keywords") {
	AIKnowledge::Domain domain;
	String error;

	const bool parsed = AIKnowledge::parse_domain("---\nmatch: word\nkeywords: ui, hud\n---\nbody\n", "ui", domain, error);

	CHECK(parsed);
	CHECK(domain.id == "ui");
	CHECK(domain.whole_word);
	CHECK(domain.keywords.size() == 2);
}

TEST_CASE("[AIAssistant][Knowledge] parse_domain explains what is wrong with a malformed file") {
	static const char *malformed[] = {
		"no front matter at all\n",
		"---\nid: x\nkeywords:\n  - a\nbody without a closing fence\n",
		"---\nid: x\n---\nbody but no keywords\n",
		"---\nkeywords: a\nmatch: fuzzy\n---\nbody\n",
		"---\nkeywords: a\nunknown: 1\n---\nbody\n",
		"---\nkeywords: a\n---\n   \n",
		nullptr
	};

	for (int i = 0; malformed[i]; i++) {
		AIKnowledge::Domain domain;
		String error;
		CHECK_MESSAGE(!AIKnowledge::parse_domain(malformed[i], "x", domain, error), malformed[i]);
		CHECK_FALSE(error.is_empty());
	}
}

TEST_CASE("[AIAssistant][Knowledge] parse_api_docs splits on class headings") {
	const Vector<AIKnowledge::ApiDoc> docs = AIKnowledge::parse_api_docs(
			"## Node\nNode: name, owner.\n  add_child(node).\n## Camera2D\nCamera2D: zoom.\n");

	REQUIRE(docs.size() == 2);
	CHECK(docs[0].class_name == "Node");
	CHECK(docs[0].text == "Node: name, owner.\n  add_child(node).\n");
	CHECK(docs[1].class_name == "Camera2D");
	CHECK(docs[1].text == "Camera2D: zoom.\n");
}

TEST_CASE("[AIAssistant][Knowledge] format fills placeholders in one pass and keeps unknown ones") {
	Dictionary values;
	values["error"] = "Unexpected {{name}} at line 3";
	values["name"] = "SHOULD NOT APPEAR";
	values["count"] = 2;

	const String result = AIKnowledge::format("Fix {{ error }} ({{count}} left, {{missing}})", values);

	// Text substituted in is not scanned again, so error output that happens
	// to contain "{{...}}" cannot pull other values in.
	CHECK(result == "Fix Unexpected {{name}} at line 3 (2 left, {{missing}})");
}

TEST_CASE("[AIAssistant][Knowledge] system prompt receives the project instructions at the placeholder") {
	const String prompt_template = String("Intro.\n\n") + AISystemPrompt::PROJECT_INSTRUCTIONS_PLACEHOLDER + "## Capabilities\n";

	const String with_project = AISystemPrompt::build(prompt_template, "Use 4 spaces.");
	const String without_project = AISystemPrompt::build(prompt_template, "  \n");

	CHECK(with_project == "Intro.\n\n## Project Instructions (from godot_ai.md)\nUse 4 spaces.\n\n## Capabilities\n");
	CHECK(without_project == "Intro.\n\n## Capabilities\n");
}

// --- Built-in data ---

TEST_CASE("[AIAssistant][Knowledge] the built-in knowledge base loads completely") {
	AIKnowledge knowledge;
	knowledge.set_override_dir(String());

	CHECK(knowledge.get_warnings().is_empty());
	CHECK(knowledge.get_overridden_files().is_empty());
	CHECK(knowledge.get_system_prompt().find(AISystemPrompt::PROJECT_INSTRUCTIONS_PLACEHOLDER) >= 0);
	CHECK(knowledge.get_system_prompt().find(AISystemPrompt::CACHE_BOUNDARY) >= 0);
	CHECK(knowledge.get_gotchas().size() > 350);
	CHECK(knowledge.get_api_docs().size() >= 50);
	CHECK(knowledge.get_domains().size() >= 13);
	CHECK(knowledge.get_domains()[0].id == "ui"); // Lowest order comes first.
}

TEST_CASE("[AIAssistant][Knowledge] every prompt template the code asks for exists") {
	static const char *required[] = {
		"mode_ask", "mode_plan", "mode_plan_step",
		"retry_compile_error", "retry_safety", "retry_no_code", "retry_exec_error", "retry_deferred_errors",
		"plan_step", "runtime_fix", "web_results", "summary_system", "summary_user",
		"preset_fix_errors", "preset_performance", nullptr
	};
	AIKnowledge knowledge;
	knowledge.set_override_dir(String());

	for (int i = 0; required[i]; i++) {
		CHECK_MESSAGE(knowledge.has_prompt(required[i]), required[i]);
		CHECK_MESSAGE(!knowledge.get_prompt(required[i]).ends_with("\n"), required[i]);
	}
}

TEST_CASE("[AIAssistant][Knowledge] built-in templates carry the placeholders the code fills in") {
	AIKnowledge knowledge;
	knowledge.set_override_dir(String());

	CHECK(knowledge.get_prompt("retry_compile_error").find("{{error}}") >= 0);
	CHECK(knowledge.get_prompt("retry_exec_error").find("{{error}}") >= 0);
	CHECK(knowledge.get_prompt("retry_deferred_errors").find("{{errors}}") >= 0);
	CHECK(knowledge.get_prompt("plan_step").find("{{step_text}}") >= 0);
	CHECK(knowledge.get_prompt("runtime_fix").find("{{errors}}") >= 0);
	CHECK(knowledge.get_prompt("web_results").find("{{content}}") >= 0);
	CHECK(knowledge.get_prompt("summary_user").find("{{history}}") >= 0);
}

// --- Overrides ---

TEST_CASE("[AIAssistant][Knowledge] an override directory replaces files and adds new ones") {
	const String dir = TestUtils::get_temp_path("ai_knowledge_override");
	_remove_dir(dir);
	_write_file(dir.path_join("prompts/mode_ask.md"), "ASK OVERRIDE\n");
	_write_file(dir.path_join("gotchas/team.md"), "<!-- keywords: TeamLogger -->\n### Use TeamLogger\nNever call print() directly.\n");
	_write_file(dir.path_join("domains/netcode.md"), "---\nkeywords: rollback\n---\n## Netcode\n");
	AIKnowledge baseline;
	baseline.set_override_dir(String());

	AIKnowledge knowledge;
	knowledge.set_override_dir(dir);

	CHECK(knowledge.get_prompt("mode_ask") == "ASK OVERRIDE");
	CHECK(knowledge.get_prompt("mode_plan") == baseline.get_prompt("mode_plan")); // Untouched files stay built in.
	CHECK(knowledge.get_gotchas().size() == baseline.get_gotchas().size() + 1);
	CHECK(knowledge.get_domains().size() == baseline.get_domains().size() + 1);
	CHECK(knowledge.get_overridden_files().size() == 3);
	CHECK(AIGotchasIndex::select(knowledge.get_gotchas(), "how do I use TeamLogger here").find("Never call print() directly.") >= 0);

	_remove_dir(dir);
}

TEST_CASE("[AIAssistant][Knowledge] a broken override file is reported and does not take the rest down") {
	const String dir = TestUtils::get_temp_path("ai_knowledge_broken");
	_remove_dir(dir);
	_write_file(dir.path_join("domains/broken.md"), "this file has no front matter\n");

	AIKnowledge knowledge;
	ERR_PRINT_OFF;
	knowledge.set_override_dir(dir);
	ERR_PRINT_ON;

	REQUIRE(knowledge.get_warnings().size() == 1);
	CHECK(knowledge.get_warnings()[0].begins_with("domains/broken.md: "));
	CHECK(knowledge.get_gotchas().size() > 350);

	_remove_dir(dir);
}

TEST_CASE("[AIAssistant][Knowledge] reload bumps the generation so derived caches are rebuilt") {
	AIKnowledge knowledge;
	knowledge.set_override_dir(String());
	const uint64_t before = knowledge.get_generation();

	knowledge.reload();

	CHECK(knowledge.get_generation() == before + 1);
}

// --- Selection ---

TEST_CASE("[AIAssistant][Gotchas] a specific keyword selects its entry") {
	Vector<AIKnowledge::Gotcha> entries;
	for (int i = 0; i < 40; i++) {
		entries.push_back(_gotcha("Filler" + itos(i), "filler " + itos(i) + "\n"));
	}
	entries.push_back(_gotcha("move_and_slide,velocity", "### No delta\n"));

	const String selected = AIGotchasIndex::select(entries, "Why does move_and_slide() ignore my speed?");
	const String unrelated = AIGotchasIndex::select(entries, "make the background blue");

	CHECK(selected == "## Relevant Gotchas\n\n### No delta\n");
	CHECK(unrelated.is_empty());
}

TEST_CASE("[AIAssistant][Gotchas] keywords match whole identifiers only") {
	Vector<AIKnowledge::Gotcha> entries;
	for (int i = 0; i < 40; i++) {
		entries.push_back(_gotcha("Filler" + itos(i), "filler\n"));
	}
	entries.push_back(_gotcha("Tween", "### Tweens\n"));

	CHECK(AIGotchasIndex::select(entries, "animate it with a tween please").find("### Tweens") >= 0);
	CHECK(AIGotchasIndex::select(entries, "put the item in between the others").is_empty());
}

TEST_CASE("[AIAssistant][Gotchas] a keyword shared by most entries does not qualify on its own") {
	Vector<AIKnowledge::Gotcha> entries;
	for (int i = 0; i < 40; i++) {
		entries.push_back(_gotcha("Node2D", "entry " + itos(i) + "\n"));
	}

	CHECK(AIGotchasIndex::select(entries, "move my Node2D").is_empty());
}

TEST_CASE("[AIAssistant][Gotchas] the most relevant entries win when the budget runs out") {
	Vector<AIKnowledge::Gotcha> entries;
	for (int i = 0; i < 40; i++) {
		entries.push_back(_gotcha("Filler" + itos(i), "filler\n"));
	}
	const String big = String("x").repeat(AIGotchasIndex::BUDGET_CHARS - 100) + "\n";
	entries.push_back(_gotcha("AnimationTree", "ONE-HIT " + big));
	entries.push_back(_gotcha("AnimationTree,BlendSpace2D", "TWO-HITS " + big));

	const String selected = AIGotchasIndex::select(entries, "AnimationTree with a BlendSpace2D");

	CHECK(selected.find("TWO-HITS") >= 0);
	CHECK(selected.find("ONE-HIT") < 0);
	CHECK(selected.length() <= AIGotchasIndex::BUDGET_CHARS + 40);
}

TEST_CASE("[AIAssistant][Gotchas] generic words are not used as keywords") {
	CHECK(AIGotchasIndex::is_useful_keyword("move_and_slide"));
	CHECK(AIGotchasIndex::is_useful_keyword("CharacterBody3D"));
	CHECK(AIGotchasIndex::is_useful_keyword("Tween"));
	CHECK(AIGotchasIndex::is_useful_keyword("SSAO"));
	CHECK(AIGotchasIndex::is_useful_keyword("NOTIFICATION_READY"));
	CHECK_FALSE(AIGotchasIndex::is_useful_keyword("Never"));
	CHECK_FALSE(AIGotchasIndex::is_useful_keyword("scene"));
	CHECK_FALSE(AIGotchasIndex::is_useful_keyword("SILENTLY"));
}

TEST_CASE("[AIAssistant][Domains] stem keywords are anchored on the left only") {
	const AIKnowledge::Domain tilemap = _domain("tilemap", false, "tile");

	CHECK(AIDomainPrompts::matches(tilemap, "paint some tiles"));
	CHECK(AIDomainPrompts::matches(tilemap, "use a tilemap"));
	CHECK_FALSE(AIDomainPrompts::matches(tilemap, "a versatile solution"));
}

TEST_CASE("[AIAssistant][Domains] whole-word keywords allow a plural but not a longer word") {
	const AIKnowledge::Domain ui = _domain("ui", true, "ui,tab,button");

	CHECK(AIDomainPrompts::matches(ui, "design the ui"));
	CHECK(AIDomainPrompts::matches(ui, "add two buttons"));
	CHECK_FALSE(AIDomainPrompts::matches(ui, "build a guide"));
	CHECK_FALSE(AIDomainPrompts::matches(ui, "sort the table"));
}

TEST_CASE("[AIAssistant][Domains] CJK keywords match as substrings") {
	const AIKnowledge::Domain ui = _domain("ui", true, String::utf8("界面,背包"));

	CHECK(AIDomainPrompts::matches(ui, String::utf8("帮我做一个游戏界面")));
	CHECK_FALSE(AIDomainPrompts::matches(ui, String::utf8("添加一个敌人")));
}

TEST_CASE("[AIAssistant][Domains] matching domains are concatenated in the order given") {
	Vector<AIKnowledge::Domain> domains;
	domains.push_back(_domain("ui", true, "hud"));
	domains.push_back(_domain("animation", false, "anim"));
	domains.push_back(_domain("audio", false, "sound"));

	const String prompt = AIDomainPrompts::build_contextual_prompt(domains, "Animate the HUD");
	const Vector<String> ids = AIDomainPrompts::detect_domains(domains, "Animate the HUD");

	CHECK(prompt == "[ui][animation]");
	CHECK(ids.size() == 2);
}

TEST_CASE("[AIAssistant][ApiDocs] references are injected for classes named exactly") {
	Vector<AIKnowledge::ApiDoc> docs;
	AIKnowledge::ApiDoc node;
	node.class_name = "Node";
	node.text = "Node: name.\n";
	docs.push_back(node);
	AIKnowledge::ApiDoc camera;
	camera.class_name = "Camera2D";
	camera.text = "Camera2D: zoom.\n";
	docs.push_back(camera);

	const String selected = AIAPIDocLoader::select(docs, "attach a Camera2D to the player node");
	const String none = AIAPIDocLoader::select(docs, "what is a Node2D?");

	// "node" (lowercase) and "Node2D" are not the class Node.
	CHECK(selected.find("Camera2D: zoom.") >= 0);
	CHECK(selected.find("Node: name.") < 0);
	CHECK(none.is_empty());
}

// --- Glossary ---

TEST_CASE("[AIAssistant][Glossary] lines map terms to engine names; comments and malformed lines are skipped") {
	const Vector<AIKnowledge::GlossaryEntry> glossary = AIKnowledge::parse_glossary(
			String::utf8("# a comment\n跳跃, 跳起 => CharacterBody2D, move_and_slide\nno arrow here\n => Orphan\nTween =>\n补间 => Tween\n"));

	REQUIRE(glossary.size() == 2);
	CHECK(glossary[0].terms.size() == 2);
	CHECK(glossary[0].names == "CharacterBody2D move_and_slide");
	CHECK(glossary[1].names == "Tween");
}

TEST_CASE("[AIAssistant][Glossary] a message is extended with the names its terms stand for, and otherwise left alone") {
	const Vector<AIKnowledge::GlossaryEntry> glossary = AIKnowledge::parse_glossary(String::utf8("跳跃 => CharacterBody2D, move_and_slide\n补间, TWEENING => Tween\n"));
	const String message = String::utf8("给角色加上跳跃");

	const String expanded = AIKnowledge::expand_with_glossary(glossary, message);

	CHECK(expanded.begins_with(message));
	CHECK(expanded.contains("move_and_slide"));
	CHECK_FALSE(expanded.contains("Tween"));
	CHECK(AIKnowledge::expand_with_glossary(glossary, "smooth tweening please").contains("Tween")); // Case-insensitive.
	CHECK(AIKnowledge::expand_with_glossary(glossary, "hello") == "hello");
}

TEST_CASE("[AIAssistant][Glossary] a Chinese request selects gotchas and API notes from the built-in knowledge") {
	AIKnowledge knowledge;
	knowledge.set_override_dir(String());
	const String message = String::utf8("给玩家加上跳跃和重力");

	const String direct = AIGotchasIndex::select(knowledge.get_gotchas(), message);
	const String expanded = knowledge.expand_message(message);
	const String via_glossary = AIGotchasIndex::select(knowledge.get_gotchas(), expanded);

	CHECK(direct.is_empty()); // Why the glossary exists.
	CHECK(via_glossary.contains("move_and_slide"));
	CHECK_FALSE(AIAPIDocLoader::select(knowledge.get_api_docs(), expanded).is_empty());
	CHECK(knowledge.get_glossary().size() > 60);
}

} // namespace TestAIKnowledge
