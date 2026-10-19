#ifdef TOOLS_ENABLED

#include "ai_assistant_panel.h"
#include "../common/ai_settings.h"
#include "../common/ai_code_text.h"

#include "../agent/ai_chat_store.h"
#include "ai_chat_view.h"
#include "ai_details_dialog.h"
#include "ai_history_list.h"
#include "../providers/ai_credential_store.h"
#include "../prompt/ai_knowledge.h"
#include "../common/ai_log.h"
#include "ai_mention_autocomplete.h"
#include "../agent/ai_runtime_watch.h"
#include "../network/ai_model_specs_updater.h"
#include "ai_settings_dialog.h"
#include "ai_theme.h"

#include "editor/themes/editor_scale.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/os/time.h"
#include "servers/display/display_server.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/option_button.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/main/viewport.h"

static const int INPUT_MIN_HEIGHT = 60;
static const int INPUT_MAX_HEIGHT = 200;
static const int INPUT_FALLBACK_MARGIN = 8;
static const int ATTACHMENT_MAX_CHARS = 50000;
// The permission prompt: how wide its text is, and how much of the code it
// shows at once before the view scrolls.
static const int PERMISSION_TEXT_WIDTH = 520;
static const int PERMISSION_CODE_WIDTH = 720;
static const int PERMISSION_CODE_MIN_LINES = 4;
static const int PERMISSION_CODE_MAX_LINES = 20;
static const float FILE_DIALOG_RATIO = 0.6f;
static const char *SCREENSHOT_DIR = "user://ai_screenshots";


// The preset buttons; their prompts are knowledge files, so they can be edited.
static const struct {
	AILocalization::StringID label;
	const char *prompt_id;
} PRESETS[] = {
	{ AILocalization::STR_PRESET_FIX_ERRORS, "preset_fix_errors" },
	{ AILocalization::STR_PRESET_PERFORMANCE, "preset_performance" },
};

void AIAssistantPanel::_notification(int p_what) {
	if (p_what != NOTIFICATION_READY) {
		return;
	}
	const int migrated_keys = AICredentialStore::migrate_from_editor_settings();
	controller->reload_settings();
	// Context windows, output limits and thinking settings change as models
	// come out; a copy older than a week is replaced, unless the user turned
	// that off (Advanced settings). Not in headless runs
	// (tests, evals), which must not depend on the network.
	if (DisplayServer::get_singleton() && DisplayServer::get_singleton()->get_name() != "headless") {
		AIModelSpecsUpdater *specs_updater = memnew(AIModelSpecsUpdater);
		add_child(specs_updater);
		specs_updater->connect("updated", callable_mp(settings_dialog, &AISettingsDialog::refresh_model_facts));
		specs_updater->update_if_stale();
	}
	_system_message(TR(STR_SYS_READY));
	if (migrated_keys > 0) {
		_system_message(AI_L("Your API keys were moved out of the editor settings file into an encrypted credential store.", "API 密钥已从编辑器设置文件迁移到加密的凭据存储中。"));
	}

	// Override files change what the model is told; never let that happen unseen.
	const AIKnowledge *knowledge = AIKnowledge::get_singleton();
	if (!knowledge->get_overridden_files().is_empty()) {
		_system_message(itos(knowledge->get_overridden_files().size()) +
						AI_L(" prompt/knowledge files are loaded from ", " 个提示词/知识文件来自覆盖目录 ") + knowledge->get_override_dir() +
						AI_L(" instead of the built-in ones.", "，替代了内置版本。"),
				AIAgentController::NOTICE_WARNING);
	}

	const bool restore = AISettings::get("restore_last_chat", true);
	if (restore) {
		// Deferred so the chat view finishes its first layout pass.
		callable_mp(this, &AIAssistantPanel::_restore_last_chat).call_deferred();
	}
}

void AIAssistantPanel::_system_message(const String &p_text, AIAgentController::NoticeLevel p_level) {
	chat_view->append_notice(p_level, p_text);
}

bool AIAssistantPanel::_get_send_on_enter() const {
	return AISettings::get("send_on_enter", true);
}

void AIAssistantPanel::_refresh_ui_texts() {
	title_label->set_text(TR(STR_PANEL_TITLE));
	new_chat_button->set_text(TR(STR_BTN_NEW));
	new_chat_button->set_tooltip_text(TR(STR_TIP_NEW_CHAT));
	history_button->set_text(history_visible ? TR(STR_BTN_BACK) : TR(STR_BTN_HISTORY));
	history_button->set_tooltip_text(TR(STR_TIP_HISTORY));
	screenshot_button->set_text(TR(STR_BTN_SCREENSHOT));
	screenshot_button->set_tooltip_text(TR(STR_TIP_SCREENSHOT));
	settings_button->set_text(TR(STR_BTN_SETTINGS));
	settings_button->set_tooltip_text(TR(STR_TIP_SETTINGS));
	send_button->set_text(TR(STR_BTN_SEND));

	const int selected_mode = mode_selector->get_selected();
	mode_selector->set_item_text(AIAgentController::MODE_ASK, TR(STR_MODE_ASK));
	mode_selector->set_item_text(AIAgentController::MODE_AGENT, TR(STR_MODE_AGENT));
	mode_selector->set_item_text(AIAgentController::MODE_PLAN, TR(STR_MODE_PLAN));
	mode_selector->select(selected_mode);
	mode_selector->set_tooltip_text(TR(STR_MODE_TOOLTIP));

	input_field->set_placeholder(_get_send_on_enter() ? TR(STR_INPUT_PLACEHOLDER_ENTER) : TR(STR_INPUT_PLACEHOLDER_CTRL));
	for (int i = 0; i < preset_buttons.size(); i++) {
		preset_buttons[i]->set_text(AILocalization::get(PRESETS[i].label));
	}
	permission_dialog->set_title(TR(STR_PERM_DIALOG_TITLE));
}

// =============================================================================
// User actions
// =============================================================================

void AIAssistantPanel::_on_send_pressed() {
	if (history_visible) {
		_show_chat_view();
	}
	const String text = input_field->get_text().strip_edges();
	if (text.is_empty() || controller->is_busy()) {
		return;
	}
	autocomplete->hide_popup();

	if (!controller->get_session().is_empty()) {
		chat_view->append_separator();
	}
	// Shown with mention chips; sent with the mentions expanded to node details.
	chat_view->append_user_message(input_field->get_text_with_mention_bbcode());
	const String expanded = input_field->get_text_with_expanded_mentions();

	input_field->set_text("");
	input_field->clear_mentions();
	input_field->set_custom_minimum_size(Size2(0, INPUT_MIN_HEIGHT * EDSCALE));

	controller->submit_user_message(expanded, text, (AIAgentController::Mode)mode_selector->get_selected());
}

void AIAssistantPanel::_on_stop_pressed() {
	controller->stop();
}

void AIAssistantPanel::_on_new_chat_pressed() {
	if (!controller->new_session()) {
		_system_message(AI_L("Stop the current response before starting a new chat.", "请先停止当前回复，再新建对话。"), AIAgentController::NOTICE_WARNING);
		return;
	}
	input_field->set_text("");
	input_field->clear_mentions();
	if (history_visible) {
		_show_chat_view();
	}
	_system_message(TR(STR_SYS_NEW_CONVERSATION));
}

void AIAssistantPanel::_on_history_pressed() {
	if (history_visible) {
		_show_chat_view();
	} else {
		_show_history_view();
	}
}

void AIAssistantPanel::_on_preset_pressed(const String &p_prompt_id) {
	if (controller->is_busy()) {
		return;
	}
	input_field->set_text(AIKnowledge::get_singleton()->get_prompt(p_prompt_id));
	_on_send_pressed();
}

void AIAssistantPanel::_on_settings_pressed() {
	settings_dialog->open();
}

void AIAssistantPanel::_on_settings_confirmed() {
	if (controller->is_busy()) {
		// The work in flight keeps the provider it started with; the new
		// settings are applied as soon as it ends (see _on_state_changed).
		settings_reload_pending = true;
		_system_message(AI_L("Settings saved. They take effect once the current response has finished.", "设置已保存，将在当前回复结束后生效。"));
		return;
	}
	controller->reload_settings();
}

void AIAssistantPanel::_on_screenshot_pressed() {
	SubViewport *viewport = EditorInterface::get_singleton()->get_editor_viewport_3d(0);
	if (!viewport) {
		viewport = EditorInterface::get_singleton()->get_editor_viewport_2d();
	}
	Ref<Image> image;
	if (viewport) {
		image = viewport->get_texture()->get_image();
	}

	// Saved under user:// so it does not pollute the project.
	String file_path;
	if (image.is_valid() && !image->is_empty()) {
		DirAccess::make_dir_recursive_absolute(ProjectSettings::get_singleton()->globalize_path(SCREENSHOT_DIR));
		const String timestamp = Time::get_singleton()->get_datetime_string_from_system(false, true).replace(":", "-");
		file_path = String(SCREENSHOT_DIR).path_join("screenshot_" + timestamp + ".png");
		if (image->save_png(file_path) != OK) {
			file_path = String();
		}
	}
	if (file_path.is_empty()) {
		_system_message(String(U"❌ ") + TR(STR_SCREENSHOT_FAILED), AIAgentController::NOTICE_ERROR);
		return;
	}

	controller->set_pending_screenshot(file_path);
	_system_message(String(U"📷 ") + TR(STR_SCREENSHOT_SAVED) + " (" + itos(image->get_width()) + "x" + itos(image->get_height()) + ")");
}

void AIAssistantPanel::_on_attach_pressed() {
	file_dialog->popup_centered_ratio(FILE_DIALOG_RATIO);
}

void AIAssistantPanel::_on_file_attach_selected(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		_system_message(AI_L("Failed to open file: ", "无法打开文件：") + p_path, AIAgentController::NOTICE_ERROR);
		return;
	}
	String content = file->get_as_utf8_string();
	const bool truncated = content.length() > ATTACHMENT_MAX_CHARS;
	if (truncated) {
		content = content.left(ATTACHMENT_MAX_CHARS) + "\n[File truncated at " + itos(ATTACHMENT_MAX_CHARS) + " characters]";
	}

	const String attachment = controller->add_attachment(p_path, content);
	String status = String(U"📎 ") + AI_L("Attached: ", "已附加：") + p_path.get_file();
	if (AISession::is_attachment_backref(attachment)) {
		status += AI_L(" (reference — content already sent this session)", "（引用 — 内容已在本次会话中发送过）");
	} else if (truncated) {
		status += AI_L(" (truncated)", "（已截断）");
	}
	_system_message(status);
}

void AIAssistantPanel::_on_permission_confirmed() {
	controller->confirm_permission(true);
}

void AIAssistantPanel::_on_permission_cancelled() {
	controller->confirm_permission(false);
}

void AIAssistantPanel::_on_link_clicked(int p_kind, int p_id) {
	switch (p_kind) {
		case AIChatView::LINK_SAVE:
			_save_code_block(p_id);
			break;
		case AIChatView::LINK_DETAILS:
			details_dialog->show_response(chat_view->get_detail_response(p_id), controller->get_response_parser());
			break;
		case AIChatView::LINK_THINKING:
			details_dialog->show_thinking(chat_view->get_thinking_text(p_id));
			break;
		case AIChatView::LINK_REVERT:
			_ask_to_confirm(AI_L("Restore this checkpoint?\n\nThe scene and the project's text files go back to how they were before that step. Changes made since then — yours included — are lost.",
							"恢复到这个检查点？\n\n场景和项目里的文本文件会回到那一步之前的状态。此后所做的修改（包括你自己的）都会丢失。"),
					callable_mp(this, &AIAssistantPanel::_revert_checkpoint).bind(p_id));
			break;
		case AIChatView::LINK_EXECUTE_PLAN:
			if (controller->is_busy()) {
				_system_message(AI_L("Stop the current response before executing the plan.", "请先停止当前回复，再执行计划。"), AIAgentController::NOTICE_WARNING);
			} else {
				controller->execute_plan();
			}
			break;
		default:
			break;
	}
}

void AIAssistantPanel::_save_code_block(int p_index) {
	const String code = chat_view->get_code_block(p_index);
	if (code.is_empty()) {
		return;
	}
	String file_name;
	const String script = AICodeText::build_saved_script(code, p_index, file_name);
	const String save_path = "res://" + file_name;

	Ref<FileAccess> file = FileAccess::open(save_path, FileAccess::WRITE);
	if (file.is_null()) {
		_system_message(AI_L("Failed to save code to: ", "代码保存失败：") + save_path, AIAgentController::NOTICE_ERROR);
		return;
	}
	file->store_string(script);
	file.unref();
	_system_message(AI_L("Code saved to: ", "代码已保存到：") + save_path, AIAgentController::NOTICE_SUCCESS);
	EditorInterface::get_singleton()->get_resource_filesystem()->scan();
}

void AIAssistantPanel::_revert_checkpoint(int p_checkpoint_id) {
	if (controller->is_busy()) {
		_system_message(AI_L("Stop the current response before reverting.", "请先停止当前回复，再执行回滚。"), AIAgentController::NOTICE_WARNING);
		return;
	}
	Ref<AICheckpointManager> checkpoints = controller->get_checkpoint_manager();
	// Read the description first: restoring discards this checkpoint and later ones.
	const String description = checkpoints->get_checkpoint_description(p_checkpoint_id);
	if (!checkpoints->restore_checkpoint(p_checkpoint_id)) {
		_system_message(AI_L("Could not restore this checkpoint: it has expired, was already rolled back, or its scene is no longer the active tab.", "无法恢复此检查点：它已过期、已被回滚，或对应场景不是当前标签页。"), AIAgentController::NOTICE_ERROR);
		return;
	}
	const AICheckpointManager::RestoreReport &report = checkpoints->get_last_restore_report();
	String detail;
	if (report.files_restored > 0 || report.files_removed > 0) {
		detail = AI_L(" (files restored: ", "（还原文件：") + itos(report.files_restored) +
				AI_L(", new files moved to trash: ", "，新增文件移入回收站：") + itos(report.files_removed) + AI_L(")", "）");
	}
	if (report.files_partial) {
		detail += AI_L(" Note: large or binary files are not covered by checkpoints.", " 注意：大文件和二进制文件不在检查点范围内。");
	}
	_system_message(AI_L("Checkpoint restored: ", "已恢复检查点：") + description + detail, AIAgentController::NOTICE_SUCCESS);
}

void AIAssistantPanel::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (!key.is_valid() || !key->is_pressed() || key->is_echo()) {
		return;
	}
	if (autocomplete->handle_key(key)) {
		input_field->accept_event();
		return;
	}
	if (key->get_keycode() != Key::ENTER) {
		return;
	}
	const bool send = _get_send_on_enter() ? (!key->is_shift_pressed() && !key->is_ctrl_pressed()) : key->is_ctrl_pressed();
	if (send) {
		_on_send_pressed();
		input_field->accept_event();
	}
}

// Grows the input field with its content. Deferred from text_changed so the
// layout change does not land in the middle of that signal.
void AIAssistantPanel::_update_input_height() {
	const Ref<StyleBox> style = input_field->get_theme_stylebox(SNAME("normal"), SNAME("TextEdit"));
	const int margin = style.is_valid() ? (int)(style->get_margin(SIDE_TOP) + style->get_margin(SIDE_BOTTOM)) : INPUT_FALLBACK_MARGIN;
	const int height = CLAMP(input_field->get_total_visible_line_count() * input_field->get_line_height() + margin, INPUT_MIN_HEIGHT, INPUT_MAX_HEIGHT);
	if ((int)input_field->get_custom_minimum_size().y != height) {
		input_field->set_custom_minimum_size(Size2(0, height));
	}
}

// =============================================================================
// Controller events
// =============================================================================

void AIAssistantPanel::_on_state_changed(int p_state) {
	const bool busy = p_state != AIAgentController::STATE_IDLE;
	send_button->set_visible(!busy);
	stop_button->set_visible(busy);
	if (p_state != AIAgentController::STATE_AWAITING_PERMISSION && permission_dialog->is_visible()) {
		permission_dialog->hide(); // The question it asks is no longer open.
	}
	if (!busy && settings_reload_pending) {
		settings_reload_pending = false;
		controller->reload_settings();
	}
}

void AIAssistantPanel::_on_permission_requested(const String &p_text, const String &p_code) {
	permission_text->set_text(p_text);
	const String code = p_code.strip_edges();
	permission_code->set_visible(!code.is_empty());
	if (!code.is_empty()) {
		permission_code->add_theme_font_override(SceneStringName(font), get_theme_font(SNAME("source"), EditorStringName(EditorFonts)));
		permission_code->set_text(code);
		permission_code->set_v_scroll(0);
		permission_code->set_h_scroll(0);
		// As tall as the code, up to a limit; beyond it the view scrolls.
		const int lines = CLAMP(permission_code->get_line_count() + 1, PERMISSION_CODE_MIN_LINES, PERMISSION_CODE_MAX_LINES);
		permission_code->set_custom_minimum_size(Size2(PERMISSION_CODE_WIDTH * EDSCALE, lines * permission_code->get_line_height()));
	}
	permission_dialog->reset_size();
	permission_dialog->popup_centered();
}

void AIAssistantPanel::_on_session_changed() {
	chat_view->show_history(controller->get_session().get_full_history(), controller->get_response_parser());
}

void AIAssistantPanel::on_game_session_started() {
	runtime_watch->on_game_session_started();
}

// =============================================================================
// History view
// =============================================================================

void AIAssistantPanel::_show_history_view() {
	history_list->refresh();
	chat_view->set_visible(false);
	input_bar->set_visible(false);
	history_list->set_visible(true);
	history_visible = true;
	history_button->set_text(TR(STR_BTN_BACK));
}

void AIAssistantPanel::_show_chat_view() {
	history_list->set_visible(false);
	chat_view->set_visible(true);
	input_bar->set_visible(true);
	history_visible = false;
	history_button->set_text(TR(STR_BTN_HISTORY));
}

void AIAssistantPanel::_on_history_item_clicked(const String &p_file_path) {
	_show_chat_view();
	if (!controller->load_session(p_file_path) && controller->is_busy()) {
		_system_message(AI_L("Stop the current response before loading another chat.", "请先停止当前回复，再载入其他对话。"), AIAgentController::NOTICE_WARNING);
	}
}

void AIAssistantPanel::_on_history_delete_pressed(const String &p_file_path) {
	_ask_to_confirm(AI_L("Delete this chat? This cannot be undone.", "删除这个对话？此操作无法撤销。"),
			callable_mp(this, &AIAssistantPanel::_on_history_item_deleted).bind(p_file_path));
}

void AIAssistantPanel::_ask_to_confirm(const String &p_text, const Callable &p_action) {
	confirm_action = p_action;
	confirm_dialog->set_text(p_text);
	confirm_dialog->popup_centered();
}

void AIAssistantPanel::_on_confirm_accepted() {
	const Callable action = confirm_action;
	confirm_action = Callable();
	if (action.is_valid()) {
		action.call();
	}
}

void AIAssistantPanel::_on_history_item_deleted(const String &p_file_path) {
	const bool had_messages = !controller->get_session().is_empty();
	if (!controller->delete_chat(p_file_path)) {
		_show_chat_view();
		_system_message(AI_L("Stop the current response before deleting a chat.", "请先停止当前回复，再删除对话。"), AIAgentController::NOTICE_WARNING);
		return;
	}
	if (had_messages && controller->get_session().is_empty()) {
		_system_message(TR(STR_SYS_CHAT_DELETED));
	}
	callable_mp(history_list, &AIHistoryList::refresh).call_deferred();
}

void AIAssistantPanel::_restore_last_chat() {
	const String latest = AIChatStore::get_latest_path();
	if (!latest.is_empty() && controller->get_session().is_empty()) {
		controller->load_session(latest);
	}
}

// =============================================================================
// Mentions
// =============================================================================

void AIAssistantPanel::insert_mention_of_selected_node() {
	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	if (!selection) {
		return;
	}
	const TypedArray<Node> nodes = selection->get_selected_nodes();
	if (nodes.is_empty()) {
		_system_message(TR(STR_SYS_NO_NODES_SELECTED), AIAgentController::NOTICE_WARNING);
		return;
	}
	mention_nodes_from_context(nodes);
}

void AIAssistantPanel::mention_nodes_from_context(const Variant &p_arg) {
	const Array nodes = p_arg;
	for (int i = 0; i < nodes.size(); i++) {
		const Node *node = Object::cast_to<Node>(nodes[i]);
		if (node) {
			input_field->insert_mention(String(node->get_name()), String(node->get_path()), node->get_class());
		}
	}
	if (!nodes.is_empty()) {
		input_field->grab_focus();
	}
}

void AIAssistantPanel::mention_files_from_context(const Variant &p_arg) {
	const PackedStringArray paths = p_arg;
	for (int i = 0; i < paths.size(); i++) {
		// Directory paths end with '/', which would leave get_file() empty.
		const String name = paths[i].rstrip("/").get_file();
		if (!name.is_empty()) {
			input_field->insert_file_mention(name, paths[i]);
		}
	}
	if (!paths.is_empty()) {
		input_field->grab_focus();
	}
}

// =============================================================================
// Construction
// =============================================================================

Button *AIAssistantPanel::_add_button(Control *p_parent, void (AIAssistantPanel::*p_handler)()) {
	Button *button = memnew(Button);
	// Labels come from AILocalization, not the editor's translation catalog.
	button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	button->connect("pressed", callable_mp(this, p_handler));
	p_parent->add_child(button);
	return button;
}

void AIAssistantPanel::_build_toolbar() {
	HBoxContainer *toolbar = memnew(HBoxContainer);
	add_child(toolbar);

	title_label = memnew(Label);
	title_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	title_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	toolbar->add_child(title_label);

	new_chat_button = _add_button(toolbar, &AIAssistantPanel::_on_new_chat_pressed);
	history_button = _add_button(toolbar, &AIAssistantPanel::_on_history_pressed);
	screenshot_button = _add_button(toolbar, &AIAssistantPanel::_on_screenshot_pressed);
	settings_button = _add_button(toolbar, &AIAssistantPanel::_on_settings_pressed);

	mode_selector = memnew(OptionButton);
	mode_selector->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	// Item ids are the AIAgentController::Mode values.
	mode_selector->add_item(TR(STR_MODE_ASK));
	mode_selector->add_item(TR(STR_MODE_AGENT));
	mode_selector->add_item(TR(STR_MODE_PLAN));
	mode_selector->select(AIAgentController::MODE_AGENT);
	toolbar->add_child(mode_selector);
}

void AIAssistantPanel::_build_chat_area() {
	chat_view = memnew(AIChatView);
	chat_view->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_view->connect("link_clicked", callable_mp(this, &AIAssistantPanel::_on_link_clicked));
	add_child(chat_view);

	HBoxContainer *preset_bar = memnew(HBoxContainer);
	add_child(preset_bar);
	for (const auto &preset : PRESETS) {
		Button *button = memnew(Button);
		button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		button->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_preset_pressed).bind(String(preset.prompt_id)));
		preset_bar->add_child(button);
		preset_buttons.push_back(button);
	}

	history_list = memnew(AIHistoryList);
	history_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	history_list->set_visible(false);
	history_list->connect("chat_selected", callable_mp(this, &AIAssistantPanel::_on_history_item_clicked));
	history_list->connect("delete_requested", callable_mp(this, &AIAssistantPanel::_on_history_delete_pressed));
	add_child(history_list);
}

void AIAssistantPanel::_build_input_bar() {
	input_bar = memnew(HBoxContainer);
	add_child(input_bar);

	Button *attach_button = _add_button(input_bar, &AIAssistantPanel::_on_attach_pressed);
	attach_button->set_text("+");
	attach_button->set_tooltip_text(AI_L("Attach file (.md, .mermaid, .txt, .json, .yaml, .gd ...)", "附加文件（.md、.mermaid、.txt、.json、.yaml、.gd 等）"));

	Button *mention_button = _add_button(input_bar, &AIAssistantPanel::insert_mention_of_selected_node);
	mention_button->set_text("@");
	mention_button->set_tooltip_text(AI_L("Insert mention of selected scene node (@NodeName)", "插入所选场景节点的提及（@节点名）"));

	input_field = memnew(AIMentionTextEdit);
	input_field->set_custom_minimum_size(Size2(0, INPUT_MIN_HEIGHT * EDSCALE));
	input_field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	// Height is driven by _update_input_height(), not by the container.
	input_field->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	input_field->set_line_wrapping_mode(TextEdit::LineWrappingMode::LINE_WRAPPING_BOUNDARY);
	input_field->connect("gui_input", callable_mp(this, &AIAssistantPanel::_on_input_gui_input));
	input_field->connect("text_changed", callable_mp(this, &AIAssistantPanel::_update_input_height), CONNECT_DEFERRED);
	input_bar->add_child(input_field);

	// Hides the placeholder characters that stand for mention chips.
	mention_highlighter.instantiate();
	input_field->set_syntax_highlighter(mention_highlighter);

	autocomplete = memnew(AIMentionAutocomplete);
	autocomplete->attach(input_field);
	add_child(autocomplete);

	send_button = _add_button(input_bar, &AIAssistantPanel::_on_send_pressed);
	stop_button = _add_button(input_bar, &AIAssistantPanel::_on_stop_pressed);
	stop_button->set_text(U"■ Stop");
	stop_button->set_visible(false);
}

void AIAssistantPanel::_build_dialogs() {
	permission_dialog = memnew(ConfirmationDialog);
	VBoxContainer *permission_box = memnew(VBoxContainer);
	permission_dialog->add_child(permission_box);
	permission_text = memnew(Label);
	permission_text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	permission_text->set_custom_minimum_size(Size2(PERMISSION_TEXT_WIDTH * EDSCALE, 0));
	permission_box->add_child(permission_text);
	permission_code = memnew(TextEdit);
	permission_code->set_editable(false);
	permission_code->set_context_menu_enabled(false);
	permission_code->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	permission_box->add_child(permission_code);
	permission_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_on_permission_confirmed));
	permission_dialog->connect("canceled", callable_mp(this, &AIAssistantPanel::_on_permission_cancelled));
	add_child(permission_dialog);

	details_dialog = memnew(AIDetailsDialog);
	add_child(details_dialog);

	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	file_dialog->set_title(AI_L("Attach File to AI Assistant", "向 AI 助手附加文件"));
	file_dialog->add_filter("*.md,*.mmd,*.mermaid", "Markdown / Diagram");
	file_dialog->add_filter("*.txt,*.json,*.yaml,*.yml,*.toml,*.csv", "Text / Data");
	file_dialog->add_filter("*.gd,*.gdscript", "GDScript");
	file_dialog->connect("file_selected", callable_mp(this, &AIAssistantPanel::_on_file_attach_selected));
	add_child(file_dialog);

	// The settings dialog registers the default values of every setting.
	confirm_dialog = memnew(ConfirmationDialog);
	confirm_dialog->set_title("Godot AI");
	confirm_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_on_confirm_accepted));
	add_child(confirm_dialog);

	settings_dialog = memnew(AISettingsDialog);
	add_child(settings_dialog);
	settings_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_on_settings_confirmed));
	settings_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_refresh_ui_texts), CONNECT_DEFERRED);
	settings_dialog->connect("language_changed", callable_mp(this, &AIAssistantPanel::_refresh_ui_texts), CONNECT_DEFERRED);
}

void AIAssistantPanel::_connect_controller() {
	// What the controller reports goes straight to the transcript...
	controller->connect("notice", callable_mp(chat_view, &AIChatView::append_notice));
	controller->connect("request_started", callable_mp(chat_view, &AIChatView::begin_stream));
	controller->connect("stream_delta", callable_mp(chat_view, &AIChatView::add_stream_delta));
	controller->connect("request_finished", callable_mp(chat_view, &AIChatView::end_stream));
	controller->connect("response_received", callable_mp(chat_view, &AIChatView::show_response));
	controller->connect("execution_finished", callable_mp(chat_view, &AIChatView::show_execution));
	controller->connect("plan_ready", callable_mp(chat_view, &AIChatView::show_plan));
	controller->connect("tool_called", callable_mp(chat_view, &AIChatView::append_tool_call));
	controller->connect("tools_checkpoint", callable_mp(chat_view, &AIChatView::append_tools_checkpoint));
	// ...except what needs the rest of the dock.
	controller->connect("state_changed", callable_mp(this, &AIAssistantPanel::_on_state_changed));
	controller->connect("permission_requested", callable_mp(this, &AIAssistantPanel::_on_permission_requested));
	controller->connect("session_changed", callable_mp(this, &AIAssistantPanel::_on_session_changed));
}

AIAssistantPanel::AIAssistantPanel() {
	set_name("Godot AI");

	controller = memnew(AIAgentController);
	add_child(controller);
	runtime_watch = memnew(AIRuntimeWatch);
	runtime_watch->set_controller(controller);
	add_child(runtime_watch);

	_build_toolbar();
	_build_chat_area();
	_build_input_bar();
	_build_dialogs();
	_connect_controller();
	_refresh_ui_texts();
}

#endif // TOOLS_ENABLED
