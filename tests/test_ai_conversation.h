#pragma once

#include "../ai_chat_view.h"
#include "../ai_plan.h"
#include "../ai_project_memory.h"

#include "tests/test_macros.h"

namespace TestAIConversation {

// --- Project memory (res://godot_ai.md) ---

TEST_CASE("[AIAssistant][Memory] the first note creates the quirks section after the user's own instructions") {
	const String result = AIProjectMemory::insert_note("# Project rules\nUse tabs.", "TileMapLayer replaces TileMap", "2026-03-07");

	CHECK(result ==
			"# Project rules\nUse tabs.\n\n"
			"## AI-Discovered Quirks\n"
			"_Quirks recorded automatically by Godot AI during your sessions._\n\n"
			"- [2026-03-07] TileMapLayer replaces TileMap\n");
}

TEST_CASE("[AIAssistant][Memory] later notes go to the top of the existing section and keep the rest intact") {
	const String existing =
			"# Project rules\n\n"
			"## AI-Discovered Quirks\n"
			"- [2026-01-01] older note\n\n"
			"## Something else\nkept\n";

	const String result = AIProjectMemory::insert_note(existing, "newer note", "2026-03-07");

	CHECK(result ==
			"# Project rules\n\n"
			"## AI-Discovered Quirks\n"
			"- [2026-03-07] newer note\n"
			"- [2026-01-01] older note\n\n"
			"## Something else\nkept\n");
}

TEST_CASE("[AIAssistant][Memory] a note can start an empty file") {
	const String result = AIProjectMemory::insert_note("", "first", "2026-03-07");

	CHECK(result.begins_with("\n## AI-Discovered Quirks\n"));
	CHECK(result.ends_with("- [2026-03-07] first\n"));
}

// --- Plan progress ---

TEST_CASE("[AIAssistant][Plan] a plan that stopped part-way resumes at the step it stopped at") {
	AIPlan plan;
	// No project is loaded in unit tests, so no checklist file is written.
	plan.set_from_text("1. First\n2. Second\n3. Third");
	plan.start();
	plan.advance();
	plan.stop(); // Step 2 failed.

	plan.start();

	CHECK(plan.is_running());
	CHECK(plan.get_current_step() == "2. Second");
}

TEST_CASE("[AIAssistant][Plan] a finished plan leaves nothing to execute again") {
	AIPlan plan;
	plan.set_from_text("1. Only step");
	plan.start();

	const bool more = plan.advance();

	CHECK_FALSE(more);
	CHECK_FALSE(plan.is_running());
	CHECK(plan.is_empty());
}

TEST_CASE("[AIAssistant][Plan] an empty plan cannot be started") {
	AIPlan plan;

	plan.start();

	CHECK(plan.is_empty());
	CHECK_FALSE(plan.is_running());
	CHECK(plan.get_current_step().is_empty());
}

// --- Chat links ---

TEST_CASE("[AIAssistant][ChatView] link targets round-trip through their text form") {
	const AIChatView::Link save = AIChatView::parse_link("save:3");
	const AIChatView::Link revert = AIChatView::parse_link("revert:12");
	const AIChatView::Link plan = AIChatView::parse_link("execute_plan:0");

	CHECK(save.kind == AIChatView::LINK_SAVE);
	CHECK(save.id == 3);
	CHECK(revert.kind == AIChatView::LINK_REVERT);
	CHECK(revert.id == 12);
	CHECK(plan.kind == AIChatView::LINK_EXECUTE_PLAN);
}

TEST_CASE("[AIAssistant][ChatView] text that is not a link target is ignored") {
	// Model output can contain [url] tags when a message is shown as BBCode.
	CHECK(AIChatView::parse_link("https://example.com").kind == AIChatView::LINK_NONE);
	CHECK(AIChatView::parse_link("").kind == AIChatView::LINK_NONE);
	CHECK(AIChatView::parse_link("delete:1").kind == AIChatView::LINK_NONE);
}

} // namespace TestAIConversation
