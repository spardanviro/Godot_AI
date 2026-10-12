#pragma once

#include "../ai_checkpoint_manager.h"
#include "../ai_error_monitor.h"
#include "../ai_localization.h"
#include "../ai_web_search.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestAIServices {

// --- Search results ---

static const char *SEARCH_PAGE =
		"<div class=\"result results_links\">"
		"<a rel=\"nofollow\" class=\"result__a\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fdocs.example.org%2Fpage%3Fa%3D1%26b%3D2&amp;rut=abc\">TileMapLayer &amp; friends</a>"
		"<a class=\"result__snippet\" href=\"x\">Use <b>TileMapLayer</b> instead of TileMap.</a>"
		"</div>"
		"<div class=\"result\">"
		"<a class=\"result__a\" href=\"https://second.example.com/\">Second result</a>"
		"</div>";

TEST_CASE("[AIAssistant][WebSearch] results are listed with title, target URL and snippet") {
	Ref<AIWebSearch> search;
	search.instantiate();

	const String results = search->parse_search_results(SEARCH_PAGE);

	CHECK(results.contains("(2 found)"));
	CHECK(results.contains("TileMapLayer & friends"));
	CHECK(results.contains("Use TileMapLayer instead of TileMap."));
	CHECK(results.contains("https://second.example.com/"));
}

TEST_CASE("[AIAssistant][WebSearch] the target of a redirect link keeps its own query string") {
	Ref<AIWebSearch> search;
	search.instantiate();

	const String results = search->parse_search_results(SEARCH_PAGE);

	CHECK(results.contains("URL: https://docs.example.org/page?a=1&b=2\n"));
	CHECK_FALSE(results.contains("rut=abc"));
}

TEST_CASE("[AIAssistant][WebSearch] a page without results says so instead of returning nothing") {
	Ref<AIWebSearch> search;
	search.instantiate();

	const String results = search->parse_search_results("<html><body>Please complete the challenge.</body></html>");

	CHECK(results.contains("No search results found"));
}

TEST_CASE("[AIAssistant][WebSearch] page text drops markup, scripts and styles") {
	Ref<AIWebSearch> search;
	search.instantiate();

	const String text = search->extract_text_from_html(
			"<html><head><style>p { color: red; }</style><script\n src=\"a.js\">var secret = 1;</script></head>"
			"<body><h1>Title</h1><p>First <b>bold</b> line.</p><script>alert(2)</script><p>Second line.</p></body></html>");

	CHECK(text.contains("Title"));
	CHECK(text.contains("First bold line."));
	CHECK(text.contains("Second line."));
	CHECK_FALSE(text.contains("color: red"));
	CHECK_FALSE(text.contains("secret"));
	CHECK_FALSE(text.contains("alert"));
	CHECK_FALSE(text.contains("<"));
}

TEST_CASE("[AIAssistant][WebSearch] entities are decoded, named and numeric") {
	Ref<AIWebSearch> search;
	search.instantiate();

	const String text = search->extract_text_from_html("<p>a &amp; b &lt;c&gt; &quot;d&quot; it&#39;s &#x41;&mdash;z &unknownthing; end</p>");

	CHECK(text.contains("a & b <c> \"d\" it's A"));
	CHECK(text.contains(String::chr(0x2014) + "z"));
	CHECK_FALSE(text.contains("?"));
	CHECK(text.ends_with("end"));
}

TEST_CASE("[AIAssistant][WebSearch] a long page is cut to the context budget and says so") {
	Ref<AIWebSearch> search;
	search.instantiate();

	const String text = search->extract_text_from_html("<p>" + String("word ").repeat(4000) + "</p>");

	CHECK(text.length() < 8100);
	CHECK(text.contains("[Content truncated"));
}

TEST_CASE("[AIAssistant][WebSearch] a URL without a scheme gets https") {
	Ref<AIWebSearch> search;
	search.instantiate();

	CHECK(search->sanitize_url("  docs.godotengine.org/en/stable ") == "https://docs.godotengine.org/en/stable");
	CHECK(search->sanitize_url("http://example.com") == "http://example.com");
}

// --- Checkpoints: the project's files ---

static void _write(const String &p_path, const String &p_text) {
	DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_text);
}

static String _read(const String &p_path) {
	return FileAccess::exists(p_path) ? FileAccess::get_file_as_string(p_path) : String("<missing>");
}

// A project folder with a script, a data file in a subfolder and a binary asset.
struct ProjectFixture {
	String root;
	Ref<AICheckpointManager> checkpoints;

	ProjectFixture(const String &p_name) {
		root = TestUtils::get_temp_path(p_name);
		_write(root.path_join("player.gd"), "extends Node\n");
		_write(root.path_join("data/levels.json"), "{}");
		_write(root.path_join("icon.png"), "not really a png");
		checkpoints.instantiate();
		checkpoints->set_storage_for_tests(root, TestUtils::get_temp_path(p_name + "_backups"));
	}
};

TEST_CASE("[AIAssistant][Checkpoints] restoring puts back files that were changed or deleted") {
	ProjectFixture project("ai_checkpoint_restore");
	project.checkpoints->begin_file_snapshot();
	const int id = project.checkpoints->create_files_checkpoint("before the code ran");
	REQUIRE(id >= 0);

	_write(project.root.path_join("player.gd"), "extends Node\nfunc broken(:\n");
	DirAccess::remove_absolute(project.root.path_join("data/levels.json"));
	const bool restored = project.checkpoints->restore_checkpoint(id);

	CHECK(restored);
	CHECK(_read(project.root.path_join("player.gd")) == "extends Node\n");
	CHECK(_read(project.root.path_join("data/levels.json")) == "{}");
	CHECK(project.checkpoints->get_last_restore_report().files_restored == 2);
	CHECK(project.checkpoints->get_last_restore_report().files_removed == 0);
}

TEST_CASE("[AIAssistant][Checkpoints] restoring removes files created since, and leaves untouched ones alone") {
	ProjectFixture project("ai_checkpoint_new_files");
	project.checkpoints->begin_file_snapshot();
	const int id = project.checkpoints->create_files_checkpoint("before the code ran");

	_write(project.root.path_join("enemy.gd"), "extends Node\n");
	_write(project.root.path_join("scenes/level.tscn"), "[gd_scene]\n");
	project.checkpoints->restore_checkpoint(id);

	CHECK_FALSE(FileAccess::exists(project.root.path_join("enemy.gd")));
	CHECK_FALSE(FileAccess::exists(project.root.path_join("scenes/level.tscn")));
	CHECK(_read(project.root.path_join("player.gd")) == "extends Node\n");
	CHECK(_read(project.root.path_join("icon.png")) == "not really a png"); // Existed before: kept.
	CHECK(project.checkpoints->get_last_restore_report().files_removed == 2);
	CHECK(project.checkpoints->get_last_restore_report().files_restored == 0);
}

TEST_CASE("[AIAssistant][Checkpoints] a checkpoint can be restored once; it and later ones are then gone") {
	ProjectFixture project("ai_checkpoint_once");
	project.checkpoints->begin_file_snapshot();
	const int first = project.checkpoints->create_files_checkpoint("first");
	project.checkpoints->begin_file_snapshot();
	const int second = project.checkpoints->create_files_checkpoint("second");
	REQUIRE(project.checkpoints->get_checkpoint_count() == 2);

	const bool restored = project.checkpoints->restore_checkpoint(first);

	CHECK(restored);
	CHECK(project.checkpoints->get_checkpoint_count() == 0);
	CHECK_FALSE(project.checkpoints->restore_checkpoint(first));
	CHECK_FALSE(project.checkpoints->restore_checkpoint(second));
	CHECK(project.checkpoints->get_checkpoint_description(second).is_empty());
}

TEST_CASE("[AIAssistant][Checkpoints] without a file snapshot there is no files checkpoint") {
	ProjectFixture project("ai_checkpoint_skipped");

	project.checkpoints->begin_file_snapshot();
	project.checkpoints->skip_file_snapshot(); // The code was found not to touch files.
	const int id = project.checkpoints->create_files_checkpoint("nothing to snapshot");

	CHECK(id == -1);
	CHECK(project.checkpoints->get_checkpoint_count() == 0);
}

TEST_CASE("[AIAssistant][Checkpoints] only the newest ten are kept") {
	ProjectFixture project("ai_checkpoint_limit");
	int first = -1;
	for (int i = 0; i < 12; i++) {
		project.checkpoints->begin_file_snapshot();
		const int id = project.checkpoints->create_files_checkpoint("step " + itos(i));
		if (i == 0) {
			first = id;
		}
	}

	CHECK(project.checkpoints->get_checkpoint_count() == 10);
	CHECK(project.checkpoints->get_checkpoint_description(first).is_empty()); // Evicted.
	CHECK(project.checkpoints->get_checkpoint_description(project.checkpoints->get_latest_id()) == "step 11");
}

// --- Error monitor ---

TEST_CASE("[AIAssistant][ErrorMonitor] errors raised while generated code runs are kept apart from the rest") {
	Ref<AIErrorMonitor> monitor;
	monitor.instantiate();

	monitor->record("_ready", "res://before.gd", 3, "Before the run", "", false);
	monitor->begin_ai_execution();
	monitor->record("_run", "res://generated.gd", 7, "Invalid call", "Nonexistent function 'foo'", false);
	monitor->end_ai_execution();
	monitor->record("_process", "res://after.gd", 9, "After the run", "", false);

	const String during = monitor->get_ai_execution_errors_text();
	CHECK(during.contains("Invalid call: Nonexistent function 'foo'"));
	CHECK(during.contains("res://generated.gd:7"));
	CHECK_FALSE(during.contains("Before the run"));
	CHECK_FALSE(during.contains("After the run"));
	// What the user asks about with "what errors are there?" is the opposite set.
	const String console = monitor->get_recent_console_errors();
	CHECK(console.contains("Before the run"));
	CHECK(console.contains("After the run"));
	CHECK_FALSE(console.contains("Invalid call"));
	CHECK(monitor->get_recent_error_count() == 2);
}

TEST_CASE("[AIAssistant][ErrorMonitor] a warning does not count as something that went wrong") {
	Ref<AIErrorMonitor> monitor;
	monitor.instantiate();
	monitor->begin_ai_execution();

	monitor->record("_run", "res://generated.gd", 2, "Unused variable", "", true);
	monitor->end_ai_execution();

	CHECK(monitor->get_ai_execution_errors_text(false).is_empty());
	CHECK(monitor->get_ai_execution_errors_text(true).contains("[WARNING] Unused variable"));
}

TEST_CASE("[AIAssistant][ErrorMonitor] the same error many times is reported once, with its count") {
	Ref<AIErrorMonitor> monitor;
	monitor.instantiate();
	monitor->begin_ai_execution();

	for (int i = 0; i < 5; i++) {
		monitor->record("_process", "res://spam.gd", 4, "Null instance", "", false);
	}
	monitor->end_ai_execution();

	const String text = monitor->get_ai_execution_errors_text();
	CHECK(text.contains("(x5)"));
	CHECK(text.find("Null instance") == text.rfind("Null instance"));
}

TEST_CASE("[AIAssistant][ErrorMonitor] the assistant's own errors are not reported as the project's") {
	Ref<AIErrorMonitor> monitor;
	monitor.instantiate();
	monitor->begin_ai_execution();

	monitor->record("save", "modules/ai_assistant/ai_chat_store.cpp", 40, "Could not save chat", "", false);
	monitor->end_ai_execution();

	CHECK(monitor->get_ai_execution_errors_text().is_empty());
	CHECK(monitor->get_recent_error_count() == 0);
}

TEST_CASE("[AIAssistant][ErrorMonitor] the console buffer keeps the newest hundred") {
	Ref<AIErrorMonitor> monitor;
	monitor.instantiate();

	for (int i = 0; i < 130; i++) {
		monitor->record("f", "res://a.gd", i, "Error number " + itos(i), "", false);
	}

	CHECK(monitor->get_recent_error_count() == 100);
	const String console = monitor->get_recent_console_errors(200);
	CHECK(console.contains("Error number 129"));
	CHECK_FALSE(console.contains("Error number 29 "));
	CHECK_FALSE(console.contains("Error number 0 "));
}

TEST_CASE("[AIAssistant][ErrorMonitor] errors of the running game arrive on their own channel and can be cleared") {
	Ref<AIErrorMonitor> monitor;
	monitor.instantiate();

	AIErrorMonitor::receive_debugger_error("Division by zero", "res://enemy.gd", 12, false);
	const int count = monitor->get_debugger_error_count();
	const String text = monitor->get_recent_debugger_errors();
	monitor->clear_debugger_errors();

	CHECK(count == 1);
	CHECK(text.contains("[ERROR] Division by zero (at res://enemy.gd:12)"));
	CHECK(monitor->get_debugger_error_count() == 0);
	CHECK(monitor->get_recent_error_count() == 0); // Not mixed into the editor console.
}

// --- Interface strings ---

TEST_CASE("[AIAssistant][Localization] every interface string exists in English and in Chinese") {
	int translated = 0;
	for (int i = 0; i < AILocalization::STR_MAX; i++) {
		const AILocalization::StringID id = (AILocalization::StringID)i;
		const String english = AILocalization::get_for_language(id, "en");
		const String chinese = AILocalization::get_for_language(id, "zh");

		const String label = "string id " + itos(i);
		CHECK_MESSAGE(!english.is_empty(), label);
		CHECK_MESSAGE(!chinese.is_empty(), label);
		// A missing Chinese entry silently falls back to the English text.
		translated += int(chinese != english);
	}
	// A few are the same in both languages ("Godot AI"); a missing table is not.
	CHECK(translated > AILocalization::STR_MAX * 9 / 10);
}

} // namespace TestAIServices
