#pragma once

#ifdef TOOLS_ENABLED

#include "../agent/ai_agent_controller.h"
#include "ai_mention_highlighter.h"
#include "ai_mention_text_edit.h"

#include "scene/gui/box_container.h"

class AIChatView;
class AIDetailsDialog;
class AIHistoryList;
class AIMentionAutocomplete;
class AIRuntimeWatch;
class AISettingsDialog;
class Button;
class ConfirmationDialog;
class EditorFileDialog;
class Label;
class OptionButton;
class RichTextLabel;
class ScrollContainer;

// The dock. It lays out the widgets, forwards what the user does to
// AIAgentController and shows what the controller reports in the chat view.
// It holds no conversation or automation state of its own.
class AIAssistantPanel : public VBoxContainer {
	GDCLASS(AIAssistantPanel, VBoxContainer);

	AIAgentController *controller = nullptr;
	AIRuntimeWatch *runtime_watch = nullptr;

	// Toolbar.
	Label *title_label = nullptr;
	Button *new_chat_button = nullptr;
	Button *history_button = nullptr;
	Button *screenshot_button = nullptr;
	Button *settings_button = nullptr;
	OptionButton *mode_selector = nullptr;

	// Chat view and input.
	AIChatView *chat_view = nullptr;
	Vector<Button *> preset_buttons;
	HBoxContainer *input_bar = nullptr;
	AIMentionTextEdit *input_field = nullptr;
	Ref<AIMentionHighlighter> mention_highlighter;
	AIMentionAutocomplete *autocomplete = nullptr;
	Button *send_button = nullptr;
	Button *stop_button = nullptr;

	// History view (replaces the chat view while visible).
	AIHistoryList *history_list = nullptr;
	bool history_visible = false;

	// Dialogs.
	AISettingsDialog *settings_dialog = nullptr;
	bool settings_reload_pending = false; // Settings changed while a run was in flight.
	ConfirmationDialog *permission_dialog = nullptr;
	Label *permission_text = nullptr;
	// The code a permission prompt is about, whole and scrollable.
	TextEdit *permission_code = nullptr;
	// For actions that cannot be taken back (restoring a checkpoint,
	// deleting a chat): asks first, then runs confirm_action.
	ConfirmationDialog *confirm_dialog = nullptr;
	Callable confirm_action;
	AIDetailsDialog *details_dialog = nullptr;
	EditorFileDialog *file_dialog = nullptr;

	// --- User actions ---
	void _on_send_pressed();
	void _on_stop_pressed();
	void _on_new_chat_pressed();
	void _on_history_pressed();
	void _on_screenshot_pressed();
	void _on_attach_pressed();
	void _on_settings_pressed();
	void _on_settings_confirmed();
	void _on_preset_pressed(const String &p_prompt_id);
	void _on_file_attach_selected(const String &p_path);
	void _on_link_clicked(int p_kind, int p_id);
	void _on_permission_confirmed();
	void _on_permission_cancelled();
	void _on_input_gui_input(const Ref<class InputEvent> &p_event);
	void _update_input_height();
	void _save_code_block(int p_index);
	void _revert_checkpoint(int p_checkpoint_id);

	// --- Controller events ---
	void _on_state_changed(int p_state);
	void _on_permission_requested(const String &p_text, const String &p_code);
	void _on_session_changed();

	// --- History view ---
	void _show_history_view();
	void _show_chat_view();
	void _on_history_item_clicked(const String &p_file_path);
	void _on_history_item_deleted(const String &p_file_path);
	void _on_history_delete_pressed(const String &p_file_path);
	void _ask_to_confirm(const String &p_text, const Callable &p_action);
	void _on_confirm_accepted();
	void _restore_last_chat();

	// --- Construction ---
	void _build_toolbar();
	void _build_chat_area();
	void _build_input_bar();
	void _build_dialogs();
	void _connect_controller();
	Button *_add_button(Control *p_parent, void (AIAssistantPanel::*p_handler)());

	void _refresh_ui_texts();
	void _system_message(const String &p_text, AIAgentController::NoticeLevel p_level = AIAgentController::NOTICE_INFO);
	bool _get_send_on_enter() const;

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	// Called by AIAssistantPlugin when the debugger connects to a started game.
	void on_game_session_started();

	// Toolbar "@" button and the scene-tree menu entry.
	void insert_mention_of_selected_node();
	// Context-menu callbacks: an Array of Node, or a PackedStringArray of paths.
	void mention_nodes_from_context(const Variant &p_arg);
	void mention_files_from_context(const Variant &p_arg);

	AIAssistantPanel();
};

#endif // TOOLS_ENABLED
