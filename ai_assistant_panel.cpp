#ifdef TOOLS_ENABLED

#include "ai_assistant_panel.h"
#include "ai_settings_dialog.h"
#include "ai_system_prompt.h"
#include "ai_localization.h"
#include "ai_gotchas_index.h"

#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/gemini_provider.h"
#include "providers/glm_provider.h"
#include "providers/deepseek_provider.h"

#include "core/config/project_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_data.h"
#include "editor/file_system/editor_file_system.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/file_dialog.h"
#include "scene/gui/panel.h"
#include "editor/gui/editor_file_dialog.h"
#include "scene/main/http_request.h"
#include "scene/main/timer.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/crypto/crypto.h"
#include "core/crypto/crypto_core.h"
#include "scene/main/viewport.h"

#define AI_LOG(msg) print_line(String("[Godot AI] ") + msg)
#define AI_ERR(msg) ERR_PRINT(String("[Godot AI] ") + msg)
#define AI_WARN(msg) WARN_PRINT(String("[Godot AI] ") + msg)

// Forward-declare mention helper used in _on_input_gui_input (defined later).
static bool _is_mention_word_char(char32_t c);

// Static runtime error capture buffer (written on main thread only).
String AIAssistantPanel::s_runtime_errors;

// Sorter for NodeInfo by name.
struct _NodeInfoSorter {
	bool operator()(const AIAssistantPanel::NodeInfo &a, const AIAssistantPanel::NodeInfo &b) const {
		return a.name.naturalcasecmp_to(b.name) < 0;
	}
};

void AIAssistantPanel::_bind_methods() {
}

void AIAssistantPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_update_provider();
			_append_message("System", TR(STR_SYS_READY), Color(0.6, 0.8, 1.0));
			AI_LOG("Panel initialized and ready.");

			// Start the passive runtime-error watch — always on, no user toggle needed.
			if (runtime_poll_timer) {
				runtime_poll_timer->start();
			}

			// Attach the autocomplete overlay to the editor root so it floats
			// above all panels without creating a separate OS window.
			// Must be deferred: the editor root may still be setting up children
			// when NOTIFICATION_READY fires, and add_child() would assert otherwise.
			Control *base = EditorInterface::get_singleton()->get_base_control();
			if (base && autocomplete_panel) {
				base->call_deferred("add_child", autocomplete_panel);
			}

			// Auto-restore last conversation if the setting is enabled.
			{
				EditorSettings *es = EditorSettings::get_singleton();
				bool restore = true; // Default on.
				if (es && es->has_setting("ai_assistant/restore_last_chat")) {
					restore = es->get_setting("ai_assistant/restore_last_chat");
				}
				if (restore) {
					String dir_path = _get_chats_dir();
					Ref<DirAccess> dir = DirAccess::open(dir_path);
					if (dir.is_valid()) {
						Vector<String> files;
						dir->list_dir_begin();
						String fname = dir->get_next();
						while (!fname.is_empty()) {
							if (!dir->current_is_dir() && fname.ends_with(".json")) {
								files.push_back(fname);
							}
							fname = dir->get_next();
						}
						dir->list_dir_end();
						if (!files.is_empty()) {
							files.sort();
							// Last file alphabetically = most recent (timestamp-based names).
							String latest = dir_path + "/" + files[files.size() - 1];
							// Defer load so RichTextLabel finishes its layout pass first.
							callable_mp(this, &AIAssistantPanel::_load_chat).bind(latest).call_deferred();
						}
					}
				}
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			// Remove the overlay from the editor root before this node is freed.
			if (autocomplete_panel && autocomplete_panel->get_parent()) {
				autocomplete_panel->get_parent()->remove_child(autocomplete_panel);
				memdelete(autocomplete_panel);
				autocomplete_panel = nullptr;
			}
		} break;
		case NOTIFICATION_THEME_CHANGED: {
		} break;
	}
}

// --- Settings ---

bool AIAssistantPanel::_get_send_on_enter() const {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es && es->has_setting("ai_assistant/send_on_enter")) {
		return es->get_setting("ai_assistant/send_on_enter");
	}
	return true;
}

bool AIAssistantPanel::_get_autorun() const {
	EditorSettings *es = EditorSettings::get_singleton();
	if (es && es->has_setting("ai_assistant/autorun")) {
		return es->get_setting("ai_assistant/autorun");
	}
	return true; // Default: auto-run enabled
}

AIAssistantPanel::AIMode AIAssistantPanel::_get_current_mode() const {
	if (mode_selector) {
		return (AIMode)mode_selector->get_selected();
	}
	return MODE_AGENT;
}

void AIAssistantPanel::_on_mode_changed(int p_index) {
	AI_LOG("Mode changed to: " + String(p_index == 0 ? "ASK" : (p_index == 1 ? "AGENT" : "PLAN")));
}

void AIAssistantPanel::_refresh_ui_texts() {
	if (title_label) {
		title_label->set_text(TR(STR_PANEL_TITLE));
	}
	if (new_chat_button) {
		new_chat_button->set_text(TR(STR_BTN_NEW));
		new_chat_button->set_tooltip_text(TR(STR_TIP_NEW_CHAT));
	}
	if (history_button) {
		if (history_visible) {
			history_button->set_text(TR(STR_BTN_BACK));
		} else {
			history_button->set_text(TR(STR_BTN_HISTORY));
		}
		history_button->set_tooltip_text(TR(STR_TIP_HISTORY));
	}
	if (settings_button) {
		settings_button->set_text(TR(STR_BTN_SETTINGS));
		settings_button->set_tooltip_text(TR(STR_TIP_SETTINGS));
	}
	if (mode_selector) {
		int sel = mode_selector->get_selected();
		mode_selector->set_item_text(0, TR(STR_MODE_ASK));
		mode_selector->set_item_text(1, TR(STR_MODE_AGENT));
		mode_selector->set_item_text(2, TR(STR_MODE_PLAN));
		mode_selector->select(sel);
		mode_selector->set_tooltip_text(TR(STR_MODE_TOOLTIP));
	}
	if (send_button) {
		send_button->set_text(TR(STR_BTN_SEND));
	}
	if (attach_button) {
		attach_button->set_tooltip_text(TTR("Attach file (.md, .mermaid, .txt, .json, .yaml, .gd ...)"));
	}
	if (input_field) {
		if (_get_send_on_enter()) {
			input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_ENTER));
		} else {
			input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_CTRL));
		}
	}

	// Update preset buttons (Fix Errors, Performance).
	AILocalization::StringID preset_ids[] = {
		AILocalization::STR_PRESET_FIX_ERRORS,
		AILocalization::STR_PRESET_PERFORMANCE,
	};
	for (int i = 0; i < preset_buttons.size() && i < 2; i++) {
		preset_buttons[i]->set_text(AILocalization::get(preset_ids[i]));
	}
}

void AIAssistantPanel::_update_provider() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		AI_ERR("EditorSettings singleton not available.");
		return;
	}

	String provider_name = es->get_setting("ai_assistant/provider");
	// Load provider-specific API key; fall back to legacy single key.
	String api_key_val;
	{
		String per_provider_key = "ai_assistant/api_key_" + provider_name;
		if (es->has_setting(per_provider_key) && !String(es->get_setting(per_provider_key)).is_empty()) {
			api_key_val = es->get_setting(per_provider_key);
		} else {
			api_key_val = es->get_setting("ai_assistant/api_key");
		}
	}
	String model_val = es->get_setting("ai_assistant/model");
	String endpoint_val = es->get_setting("ai_assistant/api_endpoint");
	int max_tokens_val = es->get_setting("ai_assistant/max_tokens");
	float temperature_val = es->get_setting("ai_assistant/temperature");

	if (provider_name == "anthropic") {
		current_provider = Ref<AnthropicProvider>(memnew(AnthropicProvider));
	} else if (provider_name == "openai") {
		current_provider = Ref<OpenAIProvider>(memnew(OpenAIProvider));
	} else if (provider_name == "gemini") {
		current_provider = Ref<GeminiProvider>(memnew(GeminiProvider));
	} else if (provider_name == "glm") {
		current_provider = Ref<GLMProvider>(memnew(GLMProvider));
	} else if (provider_name == "deepseek") {
		current_provider = Ref<DeepSeekProvider>(memnew(DeepSeekProvider));
	} else {
		current_provider = Ref<AnthropicProvider>(memnew(AnthropicProvider));
	}

	current_provider->set_api_key(api_key_val);
	if (!model_val.is_empty()) {
		current_provider->set_model(model_val);
	}
	if (!endpoint_val.is_empty()) {
		current_provider->set_api_endpoint(endpoint_val);
	}

	// Auto-set max_tokens based on model if user hasn't customized it.
	int recommended = current_provider->get_recommended_max_tokens();
	if (max_tokens_val == 4096 && recommended != 4096) {
		// Default was never changed — use model's recommended value.
		max_tokens_val = recommended;
		es->set_setting("ai_assistant/max_tokens", max_tokens_val);
	}
	current_provider->set_max_tokens(max_tokens_val);
	current_provider->set_temperature(temperature_val);

	String actual_model = model_val.is_empty() ? current_provider->get_default_model() : model_val;
	String actual_endpoint = endpoint_val.is_empty() ? current_provider->get_default_endpoint() : endpoint_val;
	int context_len = current_provider->get_model_context_length();
	AI_LOG("Provider updated: " + provider_name);
	AI_LOG("  Model: " + actual_model);
	AI_LOG("  Endpoint: " + actual_endpoint);
	AI_LOG("  API Key: " + (api_key_val.is_empty() ? String("(not set)") : String("****" + api_key_val.right(4))));
	AI_LOG("  Max tokens: " + itos(max_tokens_val) + " (recommended: " + itos(recommended) + "), Temperature: " + String::num(temperature_val, 1));
	AI_LOG("  Context window: " + itos(context_len) + " tokens, Compression threshold: ~" + itos((int)((context_len - max_tokens_val) * 0.7)) + " tokens");

	// Reload permissions.
	permission_manager->load_from_settings();

	if (input_field) {
		if (_get_send_on_enter()) {
			input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_ENTER));
		} else {
			input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_CTRL));
		}
	}
}

// --- Chat Actions ---

void AIAssistantPanel::_on_stop_pressed() {
	if (!is_waiting_response) {
		return;
	}
	// Signal the stream thread to exit its read loop.
	stream_mutex.lock();
	stream_stop_requested = true;
	stream_mutex.unlock();
	// If the timer is running, the next poll will see stream_finished and clean up.
	// Force a final poll immediately so the UI updates without a noticeable delay.
	if (!stream_poll_timer->is_stopped()) {
		_on_stream_poll();
	}
}

void AIAssistantPanel::_on_send_pressed() {
	if (history_visible) {
		_show_chat_view();
	}

	String text = input_field->get_text().strip_edges();
	if (text.is_empty() || is_waiting_response) {
		return;
	}

	AI_LOG("========== NEW MESSAGE ==========");
	AI_LOG("User input: " + text);

	if (current_chat_id.is_empty()) {
		_generate_chat_id();
	}

	// Add conversation separator before new message (if not first message).
	if (!conversation_history.is_empty()) {
		chat_display->push_color(Color(0.3, 0.3, 0.4));
		chat_display->add_text(String::utf8("────────────────────────────────────────"));
		chat_display->pop();
		chat_display->add_newline();
	}

	// Dismiss autocomplete before sending.
	_hide_mention_autocomplete();

	// Display user message with mention chips rendered via BBCode.
	_append_message("You", input_field->get_text_with_mention_bbcode(), Color(0.9, 0.9, 0.9), true);

	// Send to AI with mentions expanded to full node-info context.
	const String expanded = input_field->get_text_with_expanded_mentions();

	// Record the raw user input for use in conversational-intent detection.
	last_user_input = text;

	input_field->set_text("");
	input_field->clear_mentions();
	input_field->set_custom_minimum_size(Size2(0, 60)); // Reset to minimum after send.

	_send_to_api(expanded);
}

void AIAssistantPanel::_on_new_chat_pressed() {
	if (!conversation_history.is_empty()) {
		_save_current_chat();
	}

	chat_display->clear();
	conversation_history.clear();
	full_conversation_history.clear();
	context_summary = "";
	pending_code = "";
	pending_plan_code = "";
	current_chat_id = "";
	pending_attachments.clear();
	displayed_code_blocks.clear();
	stored_detail_responses.clear();
	stored_thinking_texts.clear();
	// Clear paste store so file IDs reset for the new session.
	paste_path_to_id.clear();
	paste_id_to_content.clear();
	paste_next_id = 1;
	input_field->set_text("");
	input_field->clear_mentions();

	if (history_visible) {
		_show_chat_view();
	}

	_append_message("System", TR(STR_SYS_NEW_CONVERSATION), Color(0.6, 0.8, 1.0));
	AI_LOG("New conversation started.");
}

void AIAssistantPanel::_on_history_pressed() {
	if (history_visible) {
		_show_chat_view();
	} else {
		_show_history_view();
	}
}

void AIAssistantPanel::_on_screenshot_pressed() {
	// Try 3D viewport first, then 2D viewport.
	SubViewport *vp = EditorInterface::get_singleton()->get_editor_viewport_3d(0);
	if (!vp) {
		vp = EditorInterface::get_singleton()->get_editor_viewport_2d();
	}
	if (!vp) {
		_append_message("System", String(U"❌ ") + TR(STR_SCREENSHOT_FAILED), Color(1.0f, 0.4f, 0.4f));
		return;
	}

	Ref<Image> img = vp->get_texture()->get_image();
	if (img.is_null() || img->is_empty()) {
		_append_message("System", String(U"❌ ") + TR(STR_SCREENSHOT_FAILED), Color(1.0f, 0.4f, 0.4f));
		return;
	}

	// Save to user:// so it doesn't pollute the project.
	String dir_path = "user://ai_screenshots";
	Ref<DirAccess> da = DirAccess::open("user://");
	if (da.is_valid() && !da->dir_exists("ai_screenshots")) {
		da->make_dir("ai_screenshots");
	}

	String timestamp = Time::get_singleton()->get_datetime_string_from_system(false, true).replace(":", "-");
	String file_path = dir_path + "/screenshot_" + timestamp + ".png";
	Error err = img->save_png(file_path);
	if (err != OK) {
		_append_message("System", String(U"❌ ") + TR(STR_SCREENSHOT_FAILED), Color(1.0f, 0.4f, 0.4f));
		return;
	}

	pending_screenshot_path = file_path;

	// Encode as base64 for inline display info.
	int width = img->get_width();
	int height = img->get_height();

	_append_message("System",
			String(U"📷 ") + TR(STR_SCREENSHOT_SAVED) + " (" + itos(width) + "x" + itos(height) + ")",
			Color(0.4f, 0.8f, 1.0f));
	AI_LOG("[Screenshot] Captured " + itos(width) + "x" + itos(height) + " -> " + file_path);
}

void AIAssistantPanel::_on_settings_pressed() {
	if (settings_dialog) {
		settings_dialog->refresh();
		settings_dialog->popup_centered();
	}
}

void AIAssistantPanel::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (!key.is_valid() || !key->is_pressed() || key->is_echo()) {
		return;
	}

	// ── Autocomplete navigation (when popup is visible) ──────────────────────
	if (autocomplete_panel && autocomplete_panel->is_visible()) {
		const int item_count = autocomplete_list->get_item_count();
		PackedInt32Array sel = autocomplete_list->get_selected_items();
		const int cur = sel.is_empty() ? 0 : sel[0];

		switch (key->get_keycode()) {
			case Key::ESCAPE:
				_hide_mention_autocomplete();
				input_field->accept_event();
				return;
			case Key::UP:
				if (item_count > 0) {
					int next = (cur - 1 + item_count) % item_count;
					autocomplete_list->select(next);
					autocomplete_list->ensure_current_is_visible();
				}
				input_field->accept_event();
				return;
			case Key::DOWN:
				if (item_count > 0) {
					int next = (cur + 1) % item_count;
					autocomplete_list->select(next);
					autocomplete_list->ensure_current_is_visible();
				}
				input_field->accept_event();
				return;
			case Key::ENTER:
			case Key::KP_ENTER:
			case Key::TAB:
				if (!sel.is_empty()) {
					_commit_mention_autocomplete(sel[0]);
				}
				input_field->accept_event();
				return;
			default:
				break; // Let the key through to TextEdit for filter-as-you-type.
		}
	}

	// Note: Mention chips are single PUA characters, so standard Backspace
	// already deletes them atomically. No special handling needed.

	// ── Send on Enter ─────────────────────────────────────────────────────────
	if (_get_send_on_enter()) {
		if (key->get_keycode() == Key::ENTER && !key->is_shift_pressed() && !key->is_ctrl_pressed()) {
			_on_send_pressed();
			input_field->accept_event();
		}
	} else {
		if (key->get_keycode() == Key::ENTER && key->is_ctrl_pressed()) {
			_on_send_pressed();
			input_field->accept_event();
		}
	}
}

// --- Node mention helpers (file-scope statics) ---

static bool _is_mention_word_char(char32_t c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_';
}

static Node *_find_node_by_name_r(Node *p_node, const String &p_name) {
	if (!p_node) {
		return nullptr;
	}
	if (p_node->get_name() == p_name) {
		return p_node;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *found = _find_node_by_name_r(p_node->get_child(i), p_name);
		if (found) {
			return found;
		}
	}
	return nullptr;
}

// --- Node mention methods ---

// Recalculates and applies the input field height. Called deferred from
// _on_input_text_changed so layout changes don't interrupt active signals.
void AIAssistantPanel::_update_input_height() {
	if (!input_field) {
		return;
	}
	const int MIN_H = 60;
	const int MAX_H = 200;

	const int line_h = input_field->get_line_height();
	const int visible_lines = input_field->get_total_visible_line_count();

	Ref<StyleBox> style = input_field->get_theme_stylebox(SNAME("normal"), SNAME("TextEdit"));
	const int v_margin = style.is_valid()
			? (int)(style->get_margin(SIDE_TOP) + style->get_margin(SIDE_BOTTOM))
			: 8;

	const int desired_h = CLAMP(visible_lines * line_h + v_margin, MIN_H, MAX_H);
	if ((int)input_field->get_custom_minimum_size().y != desired_h) {
		input_field->set_custom_minimum_size(Size2(0, desired_h));
	}
}

// Fires every time the input_field text changes; show/update autocomplete
// when the cursor is immediately after a @partial token.
void AIAssistantPanel::_on_input_text_changed() {
	// Defer height recalculation so it runs after the text_changed signal
	// has fully settled — avoids triggering a mid-signal layout pass that
	// fires NOTIFICATION_ACCESSIBILITY_UPDATE with a stale caret column.
	callable_mp(this, &AIAssistantPanel::_update_input_height).call_deferred();

	// Suppress re-opening during _commit_mention_autocomplete to avoid the
	// freshly-inserted mention chip immediately re-triggering the popup.
	if (_suppress_autocomplete) {
		return;
	}

	const int caret_col = input_field->get_caret_column();
	const int caret_line = input_field->get_caret_line();
	const String line = input_field->get_line(caret_line);

	// Walk backwards from cursor through word chars, looking for a leading '@'.
	// Skip over PUA mention characters (they are chips, not text).
	int at_pos = -1;
	for (int i = caret_col - 1; i >= 0; i--) {
		const char32_t c = line[i];
		if (c == '@') {
			// Only trigger if '@' is at start of line or preceded by whitespace/non-word.
			if (i == 0 || !_is_mention_word_char(line[i - 1])) {
				at_pos = i;
			}
			break;
		}
		// Stop scanning if we hit a PUA char (mention chip) or non-word char.
		if (input_field->is_mention_char(c) || !_is_mention_word_char(c)) {
			break;
		}
	}

	if (at_pos >= 0) {
		const String partial = line.substr(at_pos + 1, caret_col - at_pos - 1);
		_show_mention_autocomplete(caret_line, at_pos, partial);
	} else {
		_hide_mention_autocomplete();
	}
}

void AIAssistantPanel::_show_mention_autocomplete(int p_line, int p_col, const String &p_partial) {
	autocomplete_at_line = p_line;
	autocomplete_at_col = p_col;

	// Collect all scene node info (name + type + path).
	autocomplete_node_infos = _get_all_scene_node_infos();

	autocomplete_list->clear();
	const String partial_lower = p_partial.to_lower();
	for (int i = 0; i < autocomplete_node_infos.size(); i++) {
		const NodeInfo &ni = autocomplete_node_infos[i];
		if (partial_lower.is_empty() || ni.name.to_lower().contains(partial_lower)) {
			// Show node type icon alongside the name in the autocomplete list.
			Ref<Texture2D> icon;
			if (EditorNode::get_singleton()) {
				icon = EditorNode::get_singleton()->get_class_icon(ni.type, "Node");
			}
			autocomplete_list->add_item(ni.name, icon);
			// Store the index into autocomplete_node_infos as metadata.
			autocomplete_list->set_item_metadata(autocomplete_list->get_item_count() - 1, i);
		}
	}

	if (autocomplete_list->get_item_count() == 0) {
		_hide_mention_autocomplete();
		return;
	}

	// Block signals during programmatic select() so that the connected
	// item_selected → _commit_mention_autocomplete is NOT triggered here.
	autocomplete_list->set_block_signals(true);
	autocomplete_list->select(0);
	autocomplete_list->set_block_signals(false);

	// Position the overlay panel above the caret (upward expansion).
	// We position in editor-root-control local space to avoid OS-window focus issues.
	const float POPUP_W = 250.0f;
	const float ITEM_H = 26.0f;
	const float MAX_H = 200.0f;
	const float popup_h = MIN((float)autocomplete_list->get_item_count() * ITEM_H + 8.0f, MAX_H);

	const Vector2 caret_screen = input_field->get_screen_position() + input_field->get_caret_draw_pos();
	Control *base = EditorInterface::get_singleton()->get_base_control();
	const Vector2 base_origin = base ? base->get_screen_position() : Vector2();
	const Vector2 local_pos = caret_screen - base_origin + Vector2(-4.0f, -popup_h - 4.0f);

	autocomplete_panel->set_position(local_pos);
	autocomplete_panel->set_size(Vector2(POPUP_W, popup_h));
	autocomplete_panel->show();
	autocomplete_panel->move_to_front(); // Ensure it draws on top.
}

void AIAssistantPanel::_hide_mention_autocomplete() {
	if (autocomplete_panel && autocomplete_panel->is_visible()) {
		autocomplete_panel->hide();
	}
	autocomplete_at_line = -1;
	autocomplete_at_col = -1;
}

// Replaces the "@partial" text with a mention chip, then closes the autocomplete.
// Called both by item_selected signal (single click) and keyboard Enter/Tab.
void AIAssistantPanel::_commit_mention_autocomplete(int p_item_idx) {
	if (p_item_idx < 0 || p_item_idx >= autocomplete_list->get_item_count()) {
		return;
	}
	if (autocomplete_at_col < 0) {
		return;
	}

	// Look up the full node info from our cached data.
	const int info_idx = autocomplete_list->get_item_metadata(p_item_idx);
	if (info_idx < 0 || info_idx >= autocomplete_node_infos.size()) {
		return;
	}
	const NodeInfo &ni = autocomplete_node_infos[info_idx];
	const int current_col = input_field->get_caret_column();

	// Suppress _on_input_text_changed during the edit so the freshly-inserted
	// chip does not immediately re-open the autocomplete popup.
	_suppress_autocomplete = true;

	// Delete the entire "@partial" text (including the '@' itself).
	input_field->select(autocomplete_at_line, autocomplete_at_col,
			autocomplete_at_line, current_col);
	input_field->delete_selection();

	// Insert the mention chip (a single PUA character that renders as a chip).
	input_field->insert_mention(ni.name, ni.path, ni.type);

	_suppress_autocomplete = false;
	_hide_mention_autocomplete();
	input_field->grab_focus();
}

// Toolbar "@" button: inserts @NodeName for every currently selected node.
void AIAssistantPanel::_on_mention_button_pressed() {
	insert_mention_of_selected_node();
}

void AIAssistantPanel::insert_mention_of_selected_node() {
#ifdef TOOLS_ENABLED
	EditorSelection *sel = EditorInterface::get_singleton()->get_selection();
	if (!sel) {
		return;
	}
	const TypedArray<Node> nodes = sel->get_selected_nodes();
	if (nodes.is_empty()) {
		_append_message("System", TTR("No nodes selected in the scene tree."), Color(1.0f, 0.8f, 0.3f));
		return;
	}
	for (int i = 0; i < nodes.size(); i++) {
		const Node *n = Object::cast_to<Node>(nodes[i]);
		if (!n) {
			continue;
		}
		input_field->insert_mention(
				String(n->get_name()),
				String(n->get_path()),
				n->get_class());
	}
	input_field->grab_focus();
#endif
}

// Returns deduplicated sorted list of all node names in the current scene.
Vector<String> AIAssistantPanel::_get_all_scene_node_names() const {
	Vector<String> names;
	const Vector<NodeInfo> infos = _get_all_scene_node_infos();
	for (int i = 0; i < infos.size(); i++) {
		names.push_back(infos[i].name);
	}
	return names;
}

// Returns deduplicated sorted list of all scene nodes with name, path, and type.
Vector<AIAssistantPanel::NodeInfo> AIAssistantPanel::_get_all_scene_node_infos() const {
	Vector<NodeInfo> infos;
#ifdef TOOLS_ENABLED
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (!root) {
		return infos;
	}
	HashSet<String> seen;
	List<Node *> queue;
	queue.push_back(root);
	while (!queue.is_empty()) {
		Node *n = queue.front()->get();
		queue.pop_front();
		const String nm = n->get_name();
		if (!seen.has(nm)) {
			seen.insert(nm);
			NodeInfo ni;
			ni.name = nm;
			ni.path = String(n->get_path());
			ni.type = n->get_class();
			infos.push_back(ni);
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			queue.push_back(n->get_child(i));
		}
	}
	infos.sort_custom<_NodeInfoSorter>();
#endif
	return infos;
}

// Note: _expand_mentions and _render_mentions_bbcode are now handled by
// AIMentionTextEdit::get_text_with_expanded_mentions() and
// AIMentionTextEdit::get_text_with_mention_bbcode() respectively.

// --- N4: File Attachment ---

void AIAssistantPanel::_on_attach_pressed() {
#ifdef TOOLS_ENABLED
	if (!file_dialog) {
		return;
	}
	file_dialog->popup_centered_ratio(0.6f);
#endif
}

String AIAssistantPanel::_make_paste_ref(int p_id, const String &p_path, int p_char_count) const {
	return "[Attached file #" + itos(p_id) + ": " + p_path.get_file() +
		" (" + itos(p_char_count) + " chars) — already provided in this session]";
}

String AIAssistantPanel::_build_attachment_content(const String &p_path, const String &p_raw_content) {
	// For small files, always inline (no dedup overhead).
	if (p_raw_content.length() <= PASTE_INLINE_LIMIT) {
		return "--- ATTACHED FILE: " + p_path.get_file() + " ---\n" + p_raw_content + "\n--- END FILE ---";
	}

	// Check if this file was already attached in this session.
	if (paste_path_to_id.has(p_path)) {
		int existing_id = paste_path_to_id[p_path];
		const String &stored = paste_id_to_content[existing_id];
		// Check if the content changed since it was last stored.
		if (stored == p_raw_content) {
			// Content unchanged — return a back-reference only. The AI already
			// has the full content from a previous turn; no need to repeat it.
			return _make_paste_ref(existing_id, p_path, p_raw_content.length());
		}
		// Content changed — update store and include full content again.
		paste_id_to_content[existing_id] = p_raw_content;
		return "--- ATTACHED FILE #" + itos(existing_id) + " (updated): " + p_path.get_file() +
			" ---\n" + p_raw_content + "\n--- END FILE ---";
	}

	// New file — assign an ID, store it, include full content.
	int new_id = paste_next_id++;
	paste_path_to_id[p_path] = new_id;
	paste_id_to_content[new_id] = p_raw_content;
	return "--- ATTACHED FILE #" + itos(new_id) + ": " + p_path.get_file() +
		" ---\n" + p_raw_content + "\n--- END FILE ---";
}

void AIAssistantPanel::_on_file_attach_selected(const String &p_path) {
	// Read the file and store its content as an attachment for the next message.
	Ref<FileAccess> fa = FileAccess::open(p_path, FileAccess::READ);
	if (fa.is_null()) {
		_append_message("System", TTR("Failed to open file: ") + p_path, Color(1.0, 0.4, 0.4));
		return;
	}

	const int MAX_CHARS = 50000;
	String content = fa->get_as_utf8_string();
	fa->close();

	bool truncated = false;
	if (content.length() > MAX_CHARS) {
		content = content.substr(0, MAX_CHARS);
		truncated = true;
	}

	// De-duplicate: if this file (same path, same content) was already attached
	// in this session, insert a back-reference instead of the full content again.
	String attachment = _build_attachment_content(p_path, content);
	if (truncated) {
		attachment += "\n[File truncated at " + itos(MAX_CHARS) + " characters]";
	}

	pending_attachments.push_back(attachment);

	bool is_backref = attachment.begins_with("[Attached file #");
	String status = String(U"\U0001F4CE") + " Attached: " + p_path.get_file();
	if (is_backref) {
		status += " (reference — content already sent this session)";
	} else if (truncated) {
		status += TTR(" (truncated)");
	}
	_append_message("System", status, Color(0.5, 0.8, 1.0));
	AI_LOG("File attached: " + p_path + " (" + itos(content.length()) + " chars)" +
		(is_backref ? " [back-ref]" : ""));
}

// --- N5: Code Save ---

void AIAssistantPanel::_on_meta_clicked(const Variant &p_meta) {
	String meta = p_meta;
	if (meta.begins_with("save:")) {
		int idx = meta.substr(5).to_int();
		if (idx >= 0 && idx < displayed_code_blocks.size()) {
			String code = displayed_code_blocks[idx];
			String filename = "ai_generated_" + itos(idx) + ".gd";

			// Detect if the code is already a complete GDScript file (has @tool /
			// extends / class_name / func at the top level) or is raw EditorScript
			// body code (starts with statements like DirAccess, var, etc.).
			// Raw body code must be wrapped before saving or it will cause parse errors.
			bool is_complete_script = false;
			Vector<String> lines = code.split("\n");
			for (int i = 0; i < lines.size(); i++) {
				String stripped = lines[i].strip_edges();
				if (stripped.is_empty() || stripped.begins_with("#")) {
					continue; // skip blank lines and comments
				}
				// If the first real line starts one of these, it's already a full script.
				if (stripped.begins_with("@tool") || stripped.begins_with("extends ") ||
						stripped.begins_with("class_name ") || stripped.begins_with("func ") ||
						stripped.begins_with("var ") || stripped.begins_with("const ") ||
						stripped.begins_with("signal ") || stripped.begins_with("enum ") ||
						stripped.begins_with("class ") || stripped.begins_with("static ") ||
						stripped.begins_with("@export") || stripped.begins_with("@onready")) {
					// Check extends specifically for a better filename.
					if (stripped.begins_with("extends ")) {
						String base = stripped.substr(8).strip_edges();
						filename = base.to_lower() + "_script.gd";
					}
					is_complete_script = true;
				}
				break; // only look at the first real line
			}

			String content_to_save;
			if (is_complete_script) {
				// Code already has proper GDScript structure — save as-is.
				content_to_save = code;
			} else {
				// Raw EditorScript body: wrap in a runnable EditorScript so the
				// file is valid and can be executed via File > Run in ScriptEditor.
				content_to_save = "@tool\nextends EditorScript\n\nfunc _run():\n";
				for (int i = 0; i < lines.size(); i++) {
					content_to_save += "\t" + lines[i] + "\n";
				}
				filename = "ai_editor_script_" + itos(idx) + ".gd";
			}

			String save_path = "res://" + filename;
			Ref<FileAccess> f = FileAccess::open(save_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(content_to_save);
				_append_message("System", "Code saved to: " + save_path, Color(0.5, 1.0, 0.5));
				AI_LOG("Code saved to: " + save_path);

				// Trigger rescan.
				EditorInterface::get_singleton()->get_resource_filesystem()->scan();
			} else {
				_append_message("System", "Failed to save code to: " + save_path, Color(1.0, 0.4, 0.4));
			}
		}
	} else if (meta.begins_with("details:")) {
		int detail_id = meta.substr(8).to_int();
		if (detail_id < 0 || detail_id >= stored_detail_responses.size()) {
			return;
		}

		// Show full AI response in popup dialog.
		String response = stored_detail_responses[detail_id];
		Dictionary parsed = response_parser->parse(response);
		Array text_segs = parsed["text_segments"];
		Array code_blks_raw = parsed["code_blocks"];

		// Deduplicate consecutive identical code blocks.
		Array code_blks;
		for (int j = 0; j < code_blks_raw.size(); j++) {
			if (j == 0 || String(code_blks_raw[j]) != String(code_blks_raw[j - 1])) {
				code_blks.push_back(code_blks_raw[j]);
			}
		}

		details_rtl->clear();

		for (int j = 0; j < text_segs.size(); j++) {
			details_rtl->push_color(Color(0.85, 0.85, 0.9));
			details_rtl->add_text(text_segs[j]);
			details_rtl->pop();
			details_rtl->add_newline();
			details_rtl->add_newline();
		}
		for (int j = 0; j < code_blks.size(); j++) {
			details_rtl->push_color(Color(0.5, 0.5, 0.6));
			details_rtl->add_text("--- GDScript ---");
			details_rtl->pop();
			details_rtl->add_newline();

			details_rtl->push_bgcolor(Color(0.12, 0.12, 0.16, 0.9));
			details_rtl->push_color(Color(0.9, 0.85, 0.7));
			details_rtl->push_font_size(13);
			Vector<String> lines = String(code_blks[j]).split("\n");
			for (int k = 0; k < lines.size(); k++) {
				details_rtl->add_text(lines[k]);
				if (k < lines.size() - 1) {
					details_rtl->add_newline();
				}
			}
			details_rtl->pop();
			details_rtl->pop();
			details_rtl->pop();
			details_rtl->add_newline();
			details_rtl->add_newline();
		}

		details_dialog->popup_centered(Size2(700, 500));
	} else if (meta == "execute_plan") {
		// User clicked "▶ Execute Plan" in PLAN mode — run the stored plan code.
		if (!pending_plan_code.is_empty()) {
			String code_to_run = pending_plan_code;
			pending_plan_code = "";
			_execute_code_with_monitoring(code_to_run);
		} else {
			_append_message("System", "No pending plan code to execute.", Color(1.0, 0.7, 0.3));
		}
	} else if (meta.begins_with("revert:")) {
		int idx = meta.substr(7).to_int();
		if (checkpoint_manager->restore_checkpoint(idx)) {
			_append_message("System", "Checkpoint restored: " + checkpoint_manager->get_checkpoint_description(idx), Color(0.5, 1.0, 0.5));
		} else {
			_append_message("System", "Failed to restore checkpoint.", Color(1.0, 0.4, 0.4));
		}
	} else if (meta.begins_with("thinking:")) {
		int thinking_id = meta.substr(9).to_int();
		if (thinking_id < 0 || thinking_id >= stored_thinking_texts.size()) {
			return;
		}

		// Show thinking text in popup dialog.
		String thinking_text = stored_thinking_texts[thinking_id];
		details_rtl->clear();
		details_rtl->push_color(Color(0.55, 0.55, 0.65));
		details_rtl->push_italics();

		Vector<String> lines = thinking_text.split("\n");
		for (int j = 0; j < lines.size(); j++) {
			details_rtl->add_text(lines[j]);
			if (j < lines.size() - 1) {
				details_rtl->add_newline();
			}
		}

		details_rtl->pop(); // italics
		details_rtl->pop(); // color

		details_dialog->set_title(TR(STR_THINKING_LABEL));
		details_dialog->popup_centered(Size2(700, 500));
	}
}

// --- N6: Permission ---

void AIAssistantPanel::_on_permission_confirmed() {
	if (!pending_permission_code.is_empty()) {
		_execute_code_with_monitoring(pending_permission_code);
		pending_permission_code = "";
	}
}

void AIAssistantPanel::_on_permission_cancelled() {
	_append_message("System", TR(STR_SYS_EXEC_CANCELLED), Color(1.0, 0.7, 0.3));
	pending_permission_code = "";
	auto_retry_count = 0;
}

// --- API Communication (Streaming) ---

void AIAssistantPanel::_send_to_api(const String &p_message) {
	if (!current_provider.is_valid()) {
		AI_ERR("No AI provider configured.");
		_append_message("System", TR(STR_SYS_NO_PROVIDER), Color(1.0, 0.4, 0.4));
		return;
	}

	if (current_provider->get_api_key().is_empty()) {
		AI_ERR("API key not set.");
		_append_message("System", TR(STR_SYS_NO_API_KEY), Color(1.0, 0.4, 0.4));
		return;
	}

	is_waiting_response = true;
	send_button->set_visible(false);
	stop_button->set_visible(true);

	AI_LOG("Step 1: Building system prompt...");
	String system_prompt = _get_system_prompt(p_message);
	AI_LOG("  System prompt length: " + itos(system_prompt.length()) + " chars");

	// Build the actual user message with attachments.
	String full_message = p_message;
	if (!pending_attachments.is_empty()) {
		full_message += "\n\n--- ATTACHED CONTEXT ---\n";
		for (int i = 0; i < pending_attachments.size(); i++) {
			full_message += pending_attachments[i] + "\n";
		}
		full_message += "--- END ATTACHED CONTEXT ---\n";
		pending_attachments.clear();
	}

	// Attach screenshot description if one was captured.
	if (!pending_screenshot_path.is_empty()) {
		// Load the image to get dimensions and encode as base64 for the prompt.
		Ref<Image> screenshot_img = memnew(Image);
		Error img_err = screenshot_img->load(pending_screenshot_path);
		if (img_err == OK && !screenshot_img->is_empty()) {
			PackedByteArray png_data = screenshot_img->save_png_to_buffer();
			String base64 = CryptoCore::b64_encode_str(png_data.ptr(), png_data.size());
			full_message += "\n\n--- SCREENSHOT ATTACHED ---\n";
			full_message += "A screenshot of the editor viewport (" + itos(screenshot_img->get_width()) + "x" + itos(screenshot_img->get_height()) + ") was captured.\n";
			full_message += "File: " + pending_screenshot_path + "\n";
			full_message += "Base64 PNG (first 200 chars): " + base64.substr(0, 200) + "...\n";
			full_message += "When analyzing visual issues, the screenshot shows the current state of the editor viewport.\n";
			full_message += "--- END SCREENSHOT ---\n";
		}
		pending_screenshot_path = "";
	}

	// Prepend context summary to conversation history if available.
	Array messages_to_send;
	if (!context_summary.is_empty()) {
		Dictionary summary_msg;
		summary_msg["role"] = "user";
		summary_msg["content"] = "[CONTEXT SUMMARY - Previous conversation compressed]\n" + context_summary;
		messages_to_send.push_back(summary_msg);
		Dictionary ack_msg;
		ack_msg["role"] = "assistant";
		ack_msg["content"] = "Understood. I have the context from our previous conversation.";
		messages_to_send.push_back(ack_msg);
	}
	for (int i = 0; i < conversation_history.size(); i++) {
		messages_to_send.push_back(conversation_history[i]);
	}

	AI_LOG("Step 2: Building streaming request body...");
	String body = current_provider->build_stream_request_body(system_prompt, messages_to_send, full_message);
	AI_LOG("  Request body length: " + itos(body.length()) + " chars");

	String url;
	GeminiProvider *gemini = Object::cast_to<GeminiProvider>(current_provider.ptr());
	if (gemini) {
		url = gemini->get_stream_url();
	} else {
		url = current_provider->get_stream_url();
	}

	Vector<String> headers = current_provider->get_headers();

	AI_LOG("Step 3: Starting streaming request...");
	AI_LOG("  URL: " + url);

	Dictionary user_msg;
	user_msg["role"] = "user";
	user_msg["content"] = full_message;
	conversation_history.push_back(user_msg);
	full_conversation_history.push_back(user_msg);

	_start_streaming(url, headers, body);
}

// --- Streaming Infrastructure ---

// Helper: extract "data:" payload from an SSE event block.
static String _extract_sse_data(const String &p_event) {
	Vector<String> lines = p_event.split("\n");
	String data;
	for (int i = 0; i < lines.size(); i++) {
		String line = lines[i].strip_edges(true, true);
		if (line.ends_with("\r")) {
			line = line.substr(0, line.length() - 1);
		}
		if (line.begins_with("data: ")) {
			if (!data.is_empty()) {
				data += "\n";
			}
			data += line.substr(6);
		} else if (line.begins_with("data:")) {
			if (!data.is_empty()) {
				data += "\n";
			}
			data += line.substr(5);
		}
	}
	return data;
}

void AIAssistantPanel::_start_streaming(const String &p_url, const Vector<String> &p_headers, const String &p_body) {
	_stream_url = p_url;
	_stream_headers = p_headers;
	_stream_body = p_body;
	_stream_provider = current_provider;

	stream_mutex.lock();
	stream_chunk_queue.clear();
	stream_accumulated = "";
	stream_thinking_accumulated = "";
	stream_inside_think_tag = false;
	stream_tag_buffer = "";
	stream_active = true;
	stream_finished = false;
	stream_error = false;
	stream_stop_requested = false;
	stream_error_msg = "";
	stream_response_code = 0;
	stream_display_started = false;
	stream_mutex.unlock();

	// Reset thinking display state.
	thinking_phase_active = false;
	thinking_collapsed = false;
	ai_prefix_shown = false;
	thinking_display_para_start = 0;
	generic_thinking_shown = false;

	// Show generic "Thinking..." indicator for all models immediately.
	// Real thinking tokens will replace this; content will collapse/remove it.
	thinking_display_para_start = chat_display->get_paragraph_count();
	chat_display->push_color(Color(0.55, 0.55, 0.65));
	chat_display->push_italics();
	chat_display->add_text(TR(STR_THINKING_LABEL) + "...");
	chat_display->pop(); // italics
	chat_display->pop(); // color
	chat_display->add_newline();
	generic_thinking_shown = true;

	// "AI: " prefix is now deferred — shown when first content delta arrives,
	// so thinking can display above it.

	stream_poll_timer->start(0.05);
	stream_thread.start(_stream_thread_func_static, this);

	AI_LOG("  Streaming thread started.");
}

void AIAssistantPanel::_stream_thread_func_static(void *p_userdata) {
	AIAssistantPanel *self = static_cast<AIAssistantPanel *>(p_userdata);
	self->_stream_thread_func();
}

void AIAssistantPanel::_stream_thread_func() {
	String url = _stream_url;
	bool use_tls = false;
	String host;
	int port = 80;
	String path;

	if (url.begins_with("https://")) {
		use_tls = true;
		port = 443;
		url = url.substr(8);
	} else if (url.begins_with("http://")) {
		url = url.substr(7);
	}

	int slash_pos = url.find("/");
	if (slash_pos >= 0) {
		host = url.substr(0, slash_pos);
		path = url.substr(slash_pos);
	} else {
		host = url;
		path = "/";
	}

	int colon_pos = host.find(":");
	if (colon_pos >= 0) {
		port = host.substr(colon_pos + 1).to_int();
		host = host.substr(0, colon_pos);
	}

	HTTPClient *client = HTTPClient::create();
	if (!client) {
		stream_mutex.lock();
		stream_error = true;
		stream_error_msg = "Failed to create HTTPClient.";
		stream_finished = true;
		stream_mutex.unlock();
		return;
	}

	Ref<TLSOptions> tls_opts;
	if (use_tls) {
		tls_opts = TLSOptions::client();
	}

	Error err = client->connect_to_host(host, port, tls_opts);
	if (err != OK) {
		memdelete(client);
		stream_mutex.lock();
		stream_error = true;
		stream_error_msg = "Failed to connect to " + host;
		stream_finished = true;
		stream_mutex.unlock();
		return;
	}

	while (client->get_status() == HTTPClient::STATUS_CONNECTING ||
			client->get_status() == HTTPClient::STATUS_RESOLVING) {
		client->poll();
		OS::get_singleton()->delay_usec(50000);
	}

	if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
		int status = (int)client->get_status();
		memdelete(client);
		stream_mutex.lock();
		stream_error = true;
		stream_error_msg = "Connection failed. Status: " + itos(status);
		stream_finished = true;
		stream_mutex.unlock();
		return;
	}

	CharString body_utf8 = _stream_body.utf8();
	err = client->request(HTTPClient::METHOD_POST, path, _stream_headers,
			(const uint8_t *)body_utf8.get_data(), body_utf8.length());
	if (err != OK) {
		memdelete(client);
		stream_mutex.lock();
		stream_error = true;
		stream_error_msg = "Failed to send HTTP request.";
		stream_finished = true;
		stream_mutex.unlock();
		return;
	}

	while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
		client->poll();
		OS::get_singleton()->delay_usec(50000);
	}

	if (!client->has_response()) {
		memdelete(client);
		stream_mutex.lock();
		stream_error = true;
		stream_error_msg = "No response from server.";
		stream_finished = true;
		stream_mutex.unlock();
		return;
	}

	int response_code = client->get_response_code();
	stream_mutex.lock();
	stream_response_code = response_code;
	stream_mutex.unlock();

	if (response_code != 200) {
		String error_body;
		while (client->get_status() == HTTPClient::STATUS_BODY) {
			client->poll();
			PackedByteArray chunk = client->read_response_body_chunk();
			if (chunk.size() > 0) {
				error_body += String::utf8((const char *)chunk.ptr(), chunk.size());
			}
			OS::get_singleton()->delay_usec(10000);
		}
		memdelete(client);
		stream_mutex.lock();
		stream_error = true;
		stream_error_msg = "API Error (HTTP " + itos(response_code) + "): " + error_body.left(500);
		stream_finished = true;
		stream_mutex.unlock();
		return;
	}

	String buffer;
	bool done = false;

	while (client->get_status() == HTTPClient::STATUS_BODY && !done) {
		// Check if the user requested a stop.
		stream_mutex.lock();
		bool stop_req = stream_stop_requested;
		stream_mutex.unlock();
		if (stop_req) {
			break;
		}

		client->poll();
		PackedByteArray chunk = client->read_response_body_chunk();

		if (chunk.size() > 0) {
			buffer += String::utf8((const char *)chunk.ptr(), chunk.size());

			while (true) {
				int event_end = buffer.find("\n\n");
				int skip_len = 2;

				if (event_end < 0) {
					event_end = buffer.find("\r\n\r\n");
					skip_len = 4;
				}

				if (event_end < 0) {
					break;
				}

				String event_str = buffer.substr(0, event_end);
				buffer = buffer.substr(event_end + skip_len);

				String data_payload = _extract_sse_data(event_str);
				if (!data_payload.is_empty()) {
					if (_stream_provider->is_stream_done_marker(data_payload)) {
						done = true;
						break;
					}
					Dictionary delta_ex = _stream_provider->parse_stream_delta_ex(data_payload);
					String content_delta = delta_ex["content"];
					String thinking_delta = delta_ex["thinking"];

					stream_mutex.lock();
					if (!thinking_delta.is_empty()) {
						StreamChunk chunk;
						chunk.text = thinking_delta;
						chunk.is_thinking = true;
						stream_chunk_queue.push_back(chunk);
						stream_thinking_accumulated += thinking_delta;
					}
					if (!content_delta.is_empty()) {
						// Handle <think>...</think> tags embedded in content (e.g. DeepSeek models).
						String remaining = content_delta;
						while (!remaining.is_empty()) {
							if (stream_inside_think_tag) {
								int close_pos = remaining.find("</think>");
								if (close_pos != -1) {
									// End of think tag found.
									String think_part = remaining.substr(0, close_pos);
									remaining = remaining.substr(close_pos + 8);
									stream_inside_think_tag = false;
									if (!think_part.is_empty()) {
										StreamChunk chunk;
										chunk.text = think_part;
										chunk.is_thinking = true;
										stream_chunk_queue.push_back(chunk);
										stream_thinking_accumulated += think_part;
									}
								} else {
									// Still inside think tag.
									StreamChunk chunk;
									chunk.text = remaining;
									chunk.is_thinking = true;
									stream_chunk_queue.push_back(chunk);
									stream_thinking_accumulated += remaining;
									remaining = "";
								}
							} else {
								int open_pos = remaining.find("<think>");
								if (open_pos != -1) {
									// Think tag found in content.
									String before = remaining.substr(0, open_pos);
									remaining = remaining.substr(open_pos + 7);
									stream_inside_think_tag = true;
									if (!before.is_empty()) {
										StreamChunk chunk;
										chunk.text = before;
										chunk.is_thinking = false;
										stream_chunk_queue.push_back(chunk);
										stream_accumulated += before;
									}
								} else {
									// Normal content.
									StreamChunk chunk;
									chunk.text = remaining;
									chunk.is_thinking = false;
									stream_chunk_queue.push_back(chunk);
									stream_accumulated += remaining;
									remaining = "";
								}
							}
						}
					}
					stream_mutex.unlock();
				}
			}
		}

		OS::get_singleton()->delay_usec(10000);
	}

	memdelete(client);

	stream_mutex.lock();
	stream_finished = true;
	stream_mutex.unlock();
}

void AIAssistantPanel::_on_stream_poll() {
	stream_mutex.lock();
	Vector<StreamChunk> chunks = stream_chunk_queue;
	stream_chunk_queue.clear();
	bool finished = stream_finished;
	bool error = stream_error;
	String error_msg = stream_error_msg;
	stream_mutex.unlock();

	AIMode mode = _get_current_mode();

	for (int i = 0; i < chunks.size(); i++) {
		stream_display_started = true;

		if (chunks[i].is_thinking) {
			// --- Thinking delta ---
			if (!thinking_phase_active) {
				// Remove generic indicator and start real thinking display.
				if (generic_thinking_shown) {
					int cur = chat_display->get_paragraph_count();
					int remove_count = cur - thinking_display_para_start;
					for (int r = 0; r < remove_count; r++) {
						chat_display->remove_paragraph(thinking_display_para_start);
					}
					generic_thinking_shown = false;
				}
				thinking_phase_active = true;
				thinking_display_para_start = chat_display->get_paragraph_count();
				chat_display->push_color(Color(0.55, 0.55, 0.65));
				chat_display->push_italics();
				chat_display->add_text(TR(STR_THINKING_LABEL) + ": ");
			}
			chat_display->add_text(chunks[i].text);

		} else {
			// --- Content delta ---
			// Remove generic "Thinking..." indicator if still showing.
			if (generic_thinking_shown && !thinking_phase_active) {
				int cur = chat_display->get_paragraph_count();
				int remove_count = cur - thinking_display_para_start;
				for (int r = 0; r < remove_count; r++) {
					chat_display->remove_paragraph(thinking_display_para_start);
				}
				generic_thinking_shown = false;
			}
			// If real thinking was active, collapse it now.
			if (thinking_phase_active && !thinking_collapsed) {
				_collapse_thinking_display();
			}

			// Show "AI: " prefix on first content delta (ASK mode only).
			if (mode == MODE_ASK && !ai_prefix_shown) {
				ai_prefix_shown = true;
				chat_display->push_color(Color(0.7, 0.85, 1.0));
				chat_display->push_bold();
				chat_display->add_text("AI: ");
				chat_display->pop();
				chat_display->pop();
			}

			// Display content only in ASK mode. AGENT/PLAN modes show summary after execution.
			if (mode == MODE_ASK) {
				chat_display->add_text(chunks[i].text);
			}
		}
	}

	if (finished) {
		stream_poll_timer->stop();

		if (stream_thread.is_started()) {
			stream_thread.wait_to_finish();
		}

		stream_active = false;

		// Clean up any remaining thinking display at stream end.
		if (generic_thinking_shown) {
			int cur = chat_display->get_paragraph_count();
			int remove_count = cur - thinking_display_para_start;
			for (int r = 0; r < remove_count; r++) {
				chat_display->remove_paragraph(thinking_display_para_start);
			}
			generic_thinking_shown = false;
		}
		if (thinking_phase_active && !thinking_collapsed) {
			_collapse_thinking_display();
		}

		// Restore send/stop button state.
		is_waiting_response = false;
		send_button->set_visible(true);
		stop_button->set_visible(false);

		if (stream_stop_requested) {
			// User stopped generation — show partial response if any.
			stream_stop_requested = false;
			if (mode == MODE_ASK) {
				chat_display->add_newline();
			}
			chat_display->push_color(Color(0.6, 0.6, 0.6));
			chat_display->push_italics();
			chat_display->add_text(" [stopped]");
			chat_display->pop();
			chat_display->pop();
			chat_display->add_newline();
			String partial = stream_accumulated;
			_stream_provider.unref();
			if (!partial.is_empty()) {
				// Save partial response to history so context is preserved.
				// Pass is_stopped_partial=true so code blocks are NOT executed
				// and the auto-retry loop is suppressed — stopping must be final.
				_handle_ai_response(partial, true);
			}
			return;
		}

		if (error) {
			chat_display->add_newline();
			chat_display->add_newline();
			AI_ERR("Streaming error: " + error_msg);

			// Feature: reactive token-gap compression.
			// If this is a "context too long" error, parse the exact token gap,
			// drop the minimum number of old messages, and retry automatically.
			if (_try_reactive_compress(error_msg)) {
				return; // Retry in progress — don't show error to user.
			}

			_append_message("System", "Error: " + error_msg, Color(1.0, 0.4, 0.4));
			return;
		}

		if (mode == MODE_ASK) {
			chat_display->add_newline();
			chat_display->add_newline();
		}

		String full_response = stream_accumulated;

		// Fallback: some proxies (e.g. Codex-based CLIProxyAPI) route ALL model
		// output through reasoning_content instead of content, leaving content empty.
		// In that case use the thinking tokens as the actual response so code blocks
		// can still be extracted and executed.
		if (full_response.is_empty() && !stream_thinking_accumulated.is_empty()) {
			AI_WARN("Content field empty — using thinking content as response (proxy fallback).");
			full_response = stream_thinking_accumulated;
			stream_thinking_accumulated = "";
		}

		AI_LOG("Step 6: Streaming complete. Response length: " + itos(full_response.length()) + " chars");

		_stream_provider.unref();

		_handle_ai_response(full_response);
	}
}

void AIAssistantPanel::_finish_streaming() {
	if (stream_thread.is_started()) {
		stream_thread.wait_to_finish();
	}
	stream_active = false;
	_stream_provider.unref();
}

void AIAssistantPanel::_collapse_thinking_display() {
	thinking_collapsed = true;
	thinking_phase_active = false;

	// Pop the italics and color pushed during thinking start.
	chat_display->pop(); // italics
	chat_display->pop(); // color

	// Remove thinking paragraphs from the chat display.
	int current_para = chat_display->get_paragraph_count();
	int remove_count = current_para - thinking_display_para_start;
	for (int i = 0; i < remove_count; i++) {
		chat_display->remove_paragraph(thinking_display_para_start);
	}

	// Store thinking text for popup viewing.
	int thinking_id = stored_thinking_texts.size();
	stored_thinking_texts.push_back(stream_thinking_accumulated);

	// Add collapsed clickable link.
	chat_display->push_color(Color(0.5, 0.5, 0.6));
	chat_display->push_italics();
	chat_display->add_text("[");
	chat_display->push_meta("thinking:" + itos(thinking_id));
	chat_display->push_color(Color(0.4, 0.7, 1.0));
	chat_display->add_text(TR(STR_THINKING_COLLAPSED));
	chat_display->pop(); // color
	chat_display->pop(); // meta
	chat_display->add_text("]");
	chat_display->pop(); // italics
	chat_display->pop(); // color
	chat_display->add_newline();
}

void AIAssistantPanel::_on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	AI_LOG("HTTP request completed (non-streaming). Status: " + itos(p_response_code));
}

// --- Response Handling ---

void AIAssistantPanel::_handle_ai_response(const String &p_response, bool p_is_stopped_partial) {
	Dictionary assistant_msg;
	assistant_msg["role"] = "assistant";
	assistant_msg["content"] = p_response;
	if (!stream_thinking_accumulated.is_empty()) {
		assistant_msg["thinking"] = stream_thinking_accumulated;
	}
	conversation_history.push_back(assistant_msg);
	full_conversation_history.push_back(assistant_msg);

	// Successful response clears reactive retry guard.
	reactive_retry_pending = false;

	// Auto-compress context when history gets too long.
	_compress_context_if_needed();

	AI_LOG("Step 7: Parsing response for code blocks...");
	Dictionary parsed = response_parser->parse(p_response);
	Array text_segments = parsed["text_segments"];
	Array code_blocks = parsed["code_blocks"];

	AI_LOG("  Text segments: " + itos(text_segments.size()));
	AI_LOG("  Code blocks: " + itos(code_blocks.size()));

	// Store full response for [Details] link.
	last_full_response = p_response;
	bool compact = _get_current_mode() != MODE_ASK;

	if (!code_blocks.is_empty()) {
		// Deduplicate consecutive identical code blocks.
		Array unique_blocks;
		for (int i = 0; i < code_blocks.size(); i++) {
			if (i == 0 || String(code_blocks[i]) != String(code_blocks[i - 1])) {
				unique_blocks.push_back(code_blocks[i]);
			} else {
				AI_LOG("  Skipping duplicate code block " + itos(i));
			}
		}
		code_blocks = unique_blocks;

		// Store code blocks.
		for (int i = 0; i < code_blocks.size(); i++) {
			String cb = code_blocks[i];
			AI_LOG("  Code block " + itos(i) + " (" + itos(cb.length()) + " chars):");
			AI_LOG(cb);
			displayed_code_blocks.push_back(cb);

			// In ASK mode, add [Save] link after the streamed code block.
			if (!compact) {
				int block_idx = displayed_code_blocks.size() - 1;
				chat_display->push_color(Color(0.4, 0.8, 1.0));
				chat_display->push_meta("save:" + itos(block_idx));
				chat_display->add_text("[Save]");
				chat_display->pop();
				chat_display->pop();
				chat_display->add_text(" ");
			}
		}

		// In ASK mode, or when the user pressed Stop (partial/truncated response),
		// don't execute code blocks — a truncated script would likely be broken
		// and we must never let a Stop trigger a new API request via auto-retry.
		if (_get_current_mode() == MODE_ASK || p_is_stopped_partial) {
			if (p_is_stopped_partial) {
				AI_LOG("  Stopped partial response: skipping code execution and auto-retry.");
			} else {
				AI_LOG("  ASK mode: skipping code execution.");
			}
			auto_retry_count = 0;
		} else {
			pending_code = code_blocks[code_blocks.size() - 1];

			// ── AUTO-FIX + COMPILE CHECK ────────────────────────────────────────
			// First, auto-fix known recurring issues (standalone lambdas, etc.)
			// that the AI keeps generating despite the system prompt rules.
			// Then run the GDScript parser on the fixed code.  Any remaining
			// errors are fed back to the AI for correction via auto-retry.
			{
				pending_code = script_executor->auto_fix_code(pending_code);
				String compile_error = script_executor->compile_check(pending_code);
				if (!compile_error.is_empty()) {
					AI_WARN("Pre-execution compile check FAILED:\n" + compile_error);
					if (auto_retry_count < MAX_AUTO_RETRIES) {
						auto_retry_count++;
						String retry_num = itos(auto_retry_count) + "/" + itos(MAX_AUTO_RETRIES);
						AI_LOG("Pre-execution compile retry " + retry_num);
						_append_message("System",
								(String(U"⚠") + " GDScript syntax error - auto-fixing... (" + retry_num + ")"),
								Color(1.0, 0.82f, 0.25f));
						String retry_msg =
								"The GDScript code you generated has parse errors that prevent it from compiling:\n\n" +
								compile_error +
								"\n\nFix ONLY these errors and output the corrected GDScript code block. "
								"Do not change anything else.";
						pending_code = "";
						_send_to_api(retry_msg);
					} else {
						_append_message("System",
								"GDScript parse errors (auto-fix limit reached):\n" + compile_error,
								Color(1.0, 0.4f, 0.4f));
						auto_retry_count = 0;
						pending_code = "";
					}
					return;
				}
			}
			// ── END COMPILE CHECK ─────────────────────────────────────────────────

			// ── PLAN MODE: show plan + Execute button ──────────────────────────────
			// In PLAN mode the AI returns numbered plan text followed by the code block.
			// Instead of auto-executing, surface the plan in the chat and wait for the
			// user to click "▶ Execute Plan" before we run anything.
			if (_get_current_mode() == MODE_PLAN) {
				// Show plan text (text_segments) in the chat display.
				if (!text_segments.is_empty()) {
					chat_display->push_color(Color(0.75, 0.92, 0.78));
					chat_display->push_bold();
					chat_display->add_text(U"📋 Plan:");
					chat_display->pop();
					chat_display->pop();
					chat_display->add_newline();
					for (int i = 0; i < text_segments.size(); i++) {
						chat_display->push_color(Color(0.85, 0.92, 0.88));
						chat_display->add_text(text_segments[i]);
						chat_display->pop();
					}
					chat_display->add_newline();
				}

				// Show a clickable "Execute Plan" link.
				chat_display->add_newline();
				chat_display->push_bgcolor(Color(0.12, 0.38, 0.18, 0.6));
				chat_display->push_color(Color(0.3, 1.0, 0.55));
				chat_display->push_bold();
				chat_display->push_meta("execute_plan");
				chat_display->add_text(U"  ▶  Execute Plan  ");
				chat_display->pop(); // meta
				chat_display->pop(); // bold
				chat_display->pop(); // color
				chat_display->pop(); // bgcolor
				chat_display->add_newline();
				chat_display->add_newline();

				pending_plan_code = pending_code;
				pending_code = "";
				auto_retry_count = 0;
				return;
			}
			// ── END PLAN MODE ──────────────────────────────────────────────────────

			AI_LOG("Step 8: Safety check...");
			String safety_error = script_executor->check_safety(pending_code);
			if (!safety_error.is_empty()) {
				AI_WARN("Safety check BLOCKED: " + safety_error);
				_append_message("System", safety_error, Color(1.0, 0.4, 0.4));
				pending_code = "";
			} else {
				// Speculative risk classification (inspired by Claude Code's bash_classifier).
				// Classify the code into a risk tier BEFORE consulting per-category
				// permission settings, enabling zero-friction execution for safe operations
				// and guaranteed confirmation dialogs for destructive ones.
				AIPermissionManager::RiskTier tier = AIPermissionManager::classify_risk(pending_code);
				AI_LOG("  Risk tier: " + itos((int)tier));

				if (tier == AIPermissionManager::RISK_BLOCKED) {
					// classify_risk found a blocked pattern that safety check may have
					// missed (edge case). Treat as blocked.
					_append_message("System",
						"Code contains a blocked operation and was not executed.",
						Color(1.0, 0.4, 0.4));
					pending_code = "";

				} else if (tier == AIPermissionManager::RISK_READ_ONLY) {
					// Pure inspection — no side effects possible. Skip all dialogs.
					AI_LOG("  Risk tier: READ_ONLY — executing without confirmation.");
					if (runtime_fix_in_progress) {
						runtime_fix_in_progress = false;
						runtime_restart_after_fix = true;
					}
					_execute_code_with_monitoring(pending_code);
					pending_code = "";

				} else if (tier == AIPermissionManager::RISK_DESTRUCTIVE) {
					// Destructive operations: auto-confirm if autorun is enabled
					// (user has explicitly opted in) or if this is a runtime fix.
					// Otherwise show the confirmation dialog.
					if (runtime_fix_in_progress || _get_autorun()) {
						bool is_runtime_fix = runtime_fix_in_progress;
						runtime_fix_in_progress = false;
						runtime_restart_after_fix = is_runtime_fix;
						_execute_code_with_monitoring(pending_code);
						pending_code = "";
					} else {
						AIPermissionManager::PermissionResult perm =
							permission_manager->check_code_permissions(pending_code);
						String desc = perm.description.is_empty()
							? "This code will make changes that cannot be easily undone."
							: perm.description;
						pending_permission_code = pending_code;
						pending_code = "";
						permission_dialog->set_text(desc + TR(STR_PERM_PROCEED));
						permission_dialog->popup_centered();
					}

				} else {
					// RISK_ADDITIVE: additive-only changes.
					// Use existing per-category permission logic for fine-grained control.
					AIPermissionManager::PermissionResult perm =
						permission_manager->check_code_permissions(pending_code);
					if (!perm.allowed) {
						AI_WARN("Permission DENIED: " + perm.description);
						_append_message("System", perm.description, Color(1.0, 0.4, 0.4));
						pending_code = "";
					} else if (perm.needs_confirmation && !runtime_fix_in_progress && !_get_autorun()) {
						// needs_confirmation is only enforced when autorun is OFF.
						// When autorun is ON the user has explicitly opted in to silent execution.
						pending_permission_code = pending_code;
						pending_code = "";
						permission_dialog->set_text(perm.description + TR(STR_PERM_PROCEED));
						permission_dialog->popup_centered();
					} else {
						// Auto-run: autorun enabled, runtime fix, or no confirmation needed.
						bool is_runtime_fix = runtime_fix_in_progress;
						runtime_fix_in_progress = false;
						runtime_restart_after_fix = is_runtime_fix;
						_execute_code_with_monitoring(pending_code);
						pending_code = "";
					}
				}
			}
		}
	} else {
		AI_LOG("  No code blocks found. Text-only response.");

		// In AGENT/PLAN mode: if the model returned text but no code block, it
		// described what it wants to do instead of doing it.  Auto-retry by
		// asking it to produce only the code block now.
		// Skip retry when the user's original message looks conversational (greeting,
		// question, short chitchat) — in those cases a text-only reply is correct.
		// Check whether the user's original message looks conversational (greeting /
		// acknowledgement) so we don't annoy the user with a spurious retry when
		// they just said hello.  We test the RAW user input, not the AI response.
		String user_lower = last_user_input.to_lower().strip_edges();
		bool is_conversational = false;

		// ① Exact or near-exact greeting words (full message is just a greeting).
		// We compare against the stripped, lowercased input directly.
		static const char *EXACT_GREETINGS[] = {
			"你好", "嗨", "hi", "hello", "hey", "yo",
			"thanks", "thank you", "谢谢", "感谢", "thx",
			"ok", "okay", "好的", "好", "是", "是的", "明白", "了解",
			"太好了", "很好", "nice", "great", "cool",
			nullptr
		};
		for (int ki = 0; EXACT_GREETINGS[ki] != nullptr; ki++) {
			String kw = String::utf8(EXACT_GREETINGS[ki]);
			// Match if the entire input IS the keyword, or input starts with it
			// followed only by punctuation/whitespace.
			if (user_lower == kw || user_lower.begins_with(kw + " ") ||
					user_lower.begins_with(kw + "!") || user_lower.begins_with(kw + "！") ||
					user_lower.begins_with(kw + "~") || user_lower.begins_with(kw + "，") ||
					user_lower.begins_with(kw + ",") || user_lower.begins_with(kw + "。")) {
				is_conversational = true;
				break;
			}
		}

		// ② Very short input (≤ 3 chars) that didn't match keywords is still
		//    almost certainly not a task request — treat as conversational.
		if (!is_conversational && user_lower.length() <= 3) {
			is_conversational = true;
		}
		if (compact && !p_is_stopped_partial && !p_response.strip_edges().is_empty() &&
				!is_conversational && auto_retry_count < MAX_AUTO_RETRIES) {
			auto_retry_count++;
			String retry_num = itos(auto_retry_count) + "/" + itos(MAX_AUTO_RETRIES);
			AI_WARN("No code block in response — auto-retrying for code (" + retry_num + ")");

			_append_message("System",
					String(U"⚠ ") + TR(STR_NO_CODE_BLOCK_RETRY) + " (" + retry_num + ")",
					Color(1.0, 0.82f, 0.25f));

			_send_to_api(
					"Output ONLY the GDScript code block. "
					"Start your response with ```gdscript and nothing else before it. "
					"No explanation, no planning, no tables, no bullet points. "
					"Just the executable code block.");
			return;
		}

		// In AGENT/PLAN mode, streaming text was hidden. Show text now.
		if (compact) {
			chat_display->push_color(Color(0.7, 0.85, 1.0));
			chat_display->push_bold();
			chat_display->add_text("AI: ");
			chat_display->pop();
			chat_display->pop();
			for (int i = 0; i < text_segments.size(); i++) {
				chat_display->add_text(text_segments[i]);
			}
			chat_display->add_newline();
			chat_display->add_newline();
		}
		auto_retry_count = 0;
	}

	// N8/N9: Detect image/audio generation markers in the response.
	{
		int img_pos = p_response.find("[GENERATE_IMAGE:");
		while (img_pos >= 0) {
			int img_end = p_response.find("]", img_pos);
			if (img_end > img_pos) {
				String marker = p_response.substr(img_pos, img_end - img_pos + 1);
				String prompt_val, path_val;
				int pq = marker.find("prompt=\"");
				if (pq >= 0) {
					int pq_end = marker.find("\"", pq + 8);
					if (pq_end > pq) {
						prompt_val = marker.substr(pq + 8, pq_end - pq - 8);
					}
				}
				int pp = marker.find("path=\"");
				if (pp >= 0) {
					int pp_end = marker.find("\"", pp + 6);
					if (pp_end > pp) {
						path_val = marker.substr(pp + 6, pp_end - pp - 6);
					}
				}
				if (!prompt_val.is_empty()) {
					_handle_image_generation(prompt_val, path_val);
				}
				img_pos = p_response.find("[GENERATE_IMAGE:", img_end + 1);
			} else {
				break;
			}
		}

		int aud_pos = p_response.find("[GENERATE_AUDIO:");
		while (aud_pos >= 0) {
			int aud_end = p_response.find("]", aud_pos);
			if (aud_end > aud_pos) {
				String marker = p_response.substr(aud_pos, aud_end - aud_pos + 1);
				String text_val, path_val;
				int tq = marker.find("text=\"");
				if (tq >= 0) {
					int tq_end = marker.find("\"", tq + 6);
					if (tq_end > tq) {
						text_val = marker.substr(tq + 6, tq_end - tq - 6);
					}
				}
				int pp = marker.find("path=\"");
				if (pp >= 0) {
					int pp_end = marker.find("\"", pp + 6);
					if (pp_end > pp) {
						path_val = marker.substr(pp + 6, pp_end - pp - 6);
					}
				}
				if (!text_val.is_empty()) {
					_handle_audio_generation(text_val, path_val);
				}
				aud_pos = p_response.find("[GENERATE_AUDIO:", aud_end + 1);
			} else {
				break;
			}
		}
	}

	// Web search/fetch markers.
	{
		// [WEB_SEARCH: query]
		int ws_pos = p_response.find("[WEB_SEARCH:");
		if (ws_pos >= 0) {
			int ws_end = p_response.find("]", ws_pos);
			if (ws_end > ws_pos) {
				String query = p_response.substr(ws_pos + 12, ws_end - ws_pos - 12).strip_edges();
				if (!query.is_empty()) {
					_handle_web_search(query, "");
				}
			}
		}

		// [FETCH_URL: url]
		int fu_pos = p_response.find("[FETCH_URL:");
		if (fu_pos >= 0) {
			int fu_end = p_response.find("]", fu_pos);
			if (fu_end > fu_pos) {
				String url = p_response.substr(fu_pos + 11, fu_end - fu_pos - 11).strip_edges();
				if (!url.is_empty()) {
					_handle_fetch_url(url, "");
				}
			}
		}
	}

	// [WRITE_MEMORY: content] — AI records a newly-discovered quirk or pattern
	// into res://godot_ai.md so future sessions inherit the knowledge.
	{
		int wm_pos = p_response.find("[WRITE_MEMORY:");
		while (wm_pos >= 0) {
			int wm_end = p_response.find("]", wm_pos);
			if (wm_end > wm_pos) {
				String content = p_response.substr(wm_pos + 14, wm_end - wm_pos - 14).strip_edges();
				if (!content.is_empty()) {
					_handle_write_memory(content);
				}
				wm_pos = p_response.find("[WRITE_MEMORY:", wm_end + 1);
			} else {
				break;
			}
		}
	}

	_save_current_chat();
	AI_LOG("========== INTERACTION COMPLETE ==========");
}

// --- Summary Extraction ---

String AIAssistantPanel::_extract_code_summary(const String &p_code) const {
	// Extract the last print() string literal as summary.
	int search_from = 0;
	String last_print_text = "";

	while (true) {
		int print_pos = p_code.find("print(\"", search_from);
		if (print_pos < 0) {
			break;
		}
		int str_start = print_pos + 7; // after print("
		int str_end = p_code.find("\"", str_start);
		if (str_end > str_start) {
			last_print_text = p_code.substr(str_start, str_end - str_start);
		}
		search_from = (str_end > str_start) ? str_end + 1 : str_start + 1;
	}

	if (!last_print_text.is_empty()) {
		return last_print_text;
	}

	// Fallback: analyze code patterns.
	if (p_code.find("add_child") >= 0) {
		// Try to extract node name.
		int name_pos = p_code.find(".name = \"");
		if (name_pos >= 0) {
			int name_start = name_pos + 9;
			int name_end = p_code.find("\"", name_start);
			if (name_end > name_start) {
				return "Added node '" + p_code.substr(name_start, name_end - name_start) + "' to the scene.";
			}
		}
		return "Node added to the scene.";
	}
	if (p_code.find("remove_child") >= 0) {
		return "Node removed from the scene.";
	}
	if (p_code.find("ResourceSaver.save") >= 0) {
		return "Resource saved to disk.";
	}
	if (p_code.find(".source_code") >= 0) {
		return "Script created or modified.";
	}
	if (p_code.find(".position") >= 0 || p_code.find(".rotation") >= 0) {
		return "Node transform modified.";
	}
	return "Code executed successfully.";
}

// --- N1/N3: Execute with error monitoring and checkpoints ---

void AIAssistantPanel::_execute_code_with_monitoring(const String &p_code) {
	AI_LOG("  Safety check: PASSED. Auto-executing...");

	bool compact = _get_current_mode() != MODE_ASK;

	// N3: Create checkpoint before execution.
	// Also record whether a scene was open before execution so we can create a
	// "scene-open" checkpoint if the code opens a new scene from scratch.
	String first_line = p_code.get_slice("\n", 0).strip_edges().left(50);
	Node *pre_exec_root = EditorInterface::get_singleton()->get_edited_scene_root();
	bool had_scene_before = (pre_exec_root != nullptr);
	bool checkpoint_created = checkpoint_manager->create_checkpoint("Before: " + first_line);
	int cp_idx = checkpoint_created ? (checkpoint_manager->get_checkpoint_count() - 1) : -1;

	if (!compact) {
		// Full mode: show [Revert] link and "Executing code..." message.
		if (cp_idx >= 0) {
			chat_display->push_color(Color(0.5, 0.7, 1.0));
			chat_display->push_meta("revert:" + itos(cp_idx));
			chat_display->add_text("[Revert]");
			chat_display->pop();
			chat_display->pop();
			chat_display->add_text(" ");
		}

		_append_message("System", TR(STR_SYS_EXECUTING_CODE), Color(1.0, 0.9, 0.5));
	}

	AI_LOG("---------- CODE EXECUTION ----------");

	// N1: Begin error monitoring.
	error_monitor->begin_ai_execution();

	Dictionary result = script_executor->execute(p_code);

	bool success = result["success"];
	String output = result["output"];
	String error_str = result["error"];

	if (success) {
		AI_LOG("Execution SUCCESS: " + output);

		// N3 (post-exec): if no scene was open before but code opened a new scene,
		// create a "scene-open" checkpoint so the user can still revert (close the scene).
		if (!had_scene_before && cp_idx < 0) {
			Node *post_root = EditorInterface::get_singleton()->get_edited_scene_root();
			if (post_root) {
				String new_scene_path = post_root->get_scene_file_path();
				cp_idx = checkpoint_manager->create_scene_open_checkpoint(
						new_scene_path, "Opened: " + new_scene_path.get_file());
			}
		}

		if (compact) {
			// Store detail response for popup.
			int detail_id = stored_detail_responses.size();
			stored_detail_responses.push_back(last_full_response);

			// Use actual runtime output as summary (all print() lines visible).
			// Fall back to code-extracted summary only when output is empty.
			String summary;
			if (!output.strip_edges().is_empty()) {
				summary = output.strip_edges().replace("\n", "  |  ");
				if (summary.length() > 300) {
					summary = summary.left(300) + "...";
				}
			} else {
				summary = _extract_code_summary(p_code);
			}

			// Compact summary: "AI: ✅ [summary]  [Details] [Save] [Revert]"
			chat_display->push_color(Color(0.7, 0.85, 1.0));
			chat_display->push_bold();
			chat_display->add_text("AI: ");
			chat_display->pop();
			chat_display->pop();

			chat_display->push_color(Color(0.5, 1.0, 0.5));
			chat_display->add_text(String::utf8("\xe2\x9c\x85 ") + summary);
			chat_display->pop();
			chat_display->add_text("  ");

			// [▶ Details] toggle link.
			chat_display->push_color(Color(0.6, 0.6, 0.8));
			chat_display->push_meta("details:" + itos(detail_id));
			chat_display->add_text(String::utf8("[\xe2\x96\xb6 Details]"));
			chat_display->pop();
			chat_display->pop();
			chat_display->add_text(" ");

			// [Save] link (last code block).
			if (!displayed_code_blocks.is_empty()) {
				int block_idx = displayed_code_blocks.size() - 1;
				chat_display->push_color(Color(0.4, 0.8, 1.0));
				chat_display->push_meta("save:" + itos(block_idx));
				chat_display->add_text("[Save]");
				chat_display->pop();
				chat_display->pop();
				chat_display->add_text(" ");
			}

			// [Revert] link (only if checkpoint was created).
			if (cp_idx >= 0) {
				chat_display->push_color(Color(0.5, 0.7, 1.0));
				chat_display->push_meta("revert:" + itos(cp_idx));
				chat_display->add_text("[Revert]");
				chat_display->pop();
				chat_display->pop();
			}

			chat_display->add_newline();
			chat_display->add_newline();
		} else {
			_append_message("System", String::utf8("\xe2\x9c\x85 ") + output, Color(0.5, 1.0, 0.5));
		}

		// N1: Start deferred error check (500ms) to catch async errors/warnings.
		deferred_error_timer->start(0.5);
	} else {
		error_monitor->end_ai_execution();
		AI_ERR("Execution FAILED: " + error_str);

		if (compact) {
			// Store detail response for toggle.
			int detail_id = stored_detail_responses.size();
			stored_detail_responses.push_back(last_full_response);

			// Compact error: "AI: ❌ [error]  [▶ Details]"
			chat_display->push_color(Color(0.7, 0.85, 1.0));
			chat_display->push_bold();
			chat_display->add_text("AI: ");
			chat_display->pop();
			chat_display->pop();

			chat_display->push_color(Color(1.0, 0.4, 0.4));
			chat_display->add_text(String::utf8("\xe2\x9d\x8c ") + error_str);
			chat_display->pop();
			chat_display->add_text("  ");

			chat_display->push_color(Color(0.6, 0.6, 0.8));
			chat_display->push_meta("details:" + itos(detail_id));
			chat_display->add_text(String::utf8("[\xe2\x96\xb6 Details]"));
			chat_display->pop();
			chat_display->pop();

			chat_display->add_newline();
			chat_display->add_newline();
		} else {
			_append_message("System", "Execution failed: " + error_str, Color(1.0, 0.4, 0.4));
		}

		// Auto-recovery.
		if (auto_retry_count < MAX_AUTO_RETRIES) {
			auto_retry_count++;
			AI_LOG("Auto-recovery attempt " + itos(auto_retry_count) + "/" + itos(MAX_AUTO_RETRIES));
			_append_message("System", "Auto-retrying... (" + itos(auto_retry_count) + "/" + itos(MAX_AUTO_RETRIES) + ")", Color(1.0, 0.7, 0.3));

			String retry_msg = "The code failed with error:\n" + error_str + "\nPlease fix the code and try again.";
			_save_current_chat();
			_send_to_api(retry_msg);
			return;
		} else {
			AI_WARN("Max auto-retry attempts reached.");
			_append_message("System", "Auto-recovery failed after " + itos(MAX_AUTO_RETRIES) + " attempts.", Color(1.0, 0.4, 0.4));
			auto_retry_count = 0;
		}
	}
}

// N1: Deferred error check — fires 500ms after successful execution to catch async errors/warnings.
void AIAssistantPanel::_on_deferred_error_check() {
	deferred_error_timer->stop();
	error_monitor->end_ai_execution();

	if (error_monitor->has_ai_execution_errors()) {
		String errors = error_monitor->get_ai_execution_errors_text();
		AI_WARN("Deferred errors detected after execution:\n" + errors);
		_append_message("System", "Post-execution warnings/errors detected:\n" + errors, Color(1.0, 0.7, 0.3));

		// Auto-retry to fix these errors.
		if (auto_retry_count < MAX_AUTO_RETRIES) {
			auto_retry_count++;
			AI_LOG("Auto-recovery (deferred) attempt " + itos(auto_retry_count) + "/" + itos(MAX_AUTO_RETRIES));
			_append_message("System", "Auto-fixing... (" + itos(auto_retry_count) + "/" + itos(MAX_AUTO_RETRIES) + ")", Color(1.0, 0.7, 0.3));

			String retry_msg = "The code executed but produced these warnings/errors afterwards:\n" + errors + "\nPlease fix the issues. Make sure there are no errors or warnings.";
			_save_current_chat();
			_send_to_api(retry_msg);
			return;
		} else {
			auto_retry_count = 0;
		}
	} else {
		auto_retry_count = 0;

		// Runtime watch: fix was applied without errors → re-run the same scene.
		if (runtime_restart_after_fix) {
			runtime_restart_after_fix = false;
			_append_message("System", String(U"🔄 ") + TR(STR_WATCH_RESTARTING), Color(0.5f, 1.0f, 0.85f));
			AI_LOG("[Watch] Fix applied — re-running scene: " + runtime_last_play_path);
			if (!runtime_last_play_path.is_empty()) {
				EditorInterface::get_singleton()->play_custom_scene(runtime_last_play_path);
			} else {
				EditorInterface::get_singleton()->play_current_scene();
			}
		}
	}

	error_monitor->clear_ai_errors();
}

// =============================================================================
// Runtime Error Auto-Fix ("Watch" mode)
// =============================================================================

// Static error handler — installed while the scene is playing.
// Captures GDScript runtime errors into s_runtime_errors.
void AIAssistantPanel::_runtime_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type) {
	// Capture script errors and general errors; ignore warnings and shaders.
	if (p_type != ERR_HANDLER_SCRIPT && p_type != ERR_HANDLER_ERROR) {
		return;
	}

	String msg = p_error ? String(p_error) : String();
	if (p_message && p_message[0]) {
		msg += ": " + String(p_message);
	}
	if (msg.is_empty()) {
		return;
	}

	// p_file for script errors is often the GDScript source file (res://...).
	String file_str = p_file ? String(p_file) : String();
	bool from_user_script = file_str.begins_with("res://");

	// Try to collect GDScript stack frames for richer context.
	String stack_str;
	Vector<Ref<ScriptBacktrace>> backtraces = ScriptServer::capture_script_backtraces(false);
	for (int bt = 0; bt < backtraces.size(); bt++) {
		const Ref<ScriptBacktrace> &trace = backtraces[bt];
		if (trace.is_null() || trace->is_empty()) {
			continue;
		}
		for (int f = 0; f < trace->get_frame_count(); f++) {
			String frame_file = trace->get_frame_file(f);
			if (frame_file.begins_with("res://")) {
				from_user_script = true;
			}
			if (!stack_str.is_empty()) {
				stack_str += "\n    ";
			}
			stack_str += frame_file + ":" + itos(trace->get_frame_line(f)) + " @ " + trace->get_frame_function(f) + "()";
		}
	}

	// For ERR_HANDLER_SCRIPT: always a GDScript error — capture unconditionally.
	// For ERR_HANDLER_ERROR: only capture if it traces back to a user script file.
	if (p_type == ERR_HANDLER_ERROR && !from_user_script) {
		String msg_lower = msg.to_lower();
		if (!msg_lower.contains("res://") && !msg_lower.contains(".gd")) {
			return;
		}
	}

	if (!s_runtime_errors.is_empty()) {
		s_runtime_errors += "\n---\n";
	}
	s_runtime_errors += msg;

	// Include the source location from p_file/p_line if it's a user script.
	if (from_user_script && !file_str.is_empty() && stack_str.is_empty()) {
		s_runtime_errors += "\n    at: " + file_str + ":" + itos(p_line);
	}
	if (!stack_str.is_empty()) {
		s_runtime_errors += "\n    at: " + stack_str;
	}
}

void AIAssistantPanel::_install_runtime_error_handler() {
	if (runtime_eh_installed) {
		return;
	}
	runtime_eh.errfunc = _runtime_error_handler;
	runtime_eh.userdata = nullptr;
	add_error_handler(&runtime_eh);
	runtime_eh_installed = true;
}

void AIAssistantPanel::_remove_runtime_error_handler() {
	if (!runtime_eh_installed) {
		return;
	}
	remove_error_handler(&runtime_eh);
	runtime_eh_installed = false;
}

// 200 ms poll — detects play start / stop transitions (always-on, no toggle).
void AIAssistantPanel::_on_runtime_poll_timeout() {
	bool currently_playing = EditorInterface::get_singleton()->is_playing_scene();

	if (currently_playing && !runtime_was_playing) {
		// ── Play just started ─────────────────────────────────────────────
		s_runtime_errors = "";
		runtime_collect_timer->stop();
		// Clear debugger error buffer so we only capture errors from this run.
		if (error_monitor.is_valid()) {
			error_monitor->clear_debugger_errors();
		}
		_install_runtime_error_handler();

		// Record the scene path so we can re-run the exact same scene after fix.
		Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
		runtime_last_play_path = scene_root ? scene_root->get_scene_file_path() : String();

		AI_LOG("[Watch] Scene play started — monitoring for runtime errors. Scene: " + runtime_last_play_path);

	} else if (!currently_playing && runtime_was_playing) {
		// ── Play just stopped ─────────────────────────────────────────────
		_remove_runtime_error_handler();
		runtime_collect_timer->stop();

		// Check both the in-process buffer AND the debugger error buffer.
		bool has_errors = !s_runtime_errors.is_empty() ||
				(error_monitor.is_valid() && error_monitor->get_debugger_error_count() > 0);

		if (has_errors && !runtime_fix_in_progress) {
			AI_LOG("[Watch] Scene stopped with errors — triggering auto-fix.");
			_trigger_runtime_fix();
		} else if (!runtime_fix_in_progress && runtime_fix_attempt > 0) {
			// Clean run after previous fix attempts → success.
			_append_message("System", String(U"✅ ") + TR(STR_WATCH_SUCCESS), Color(0.4f, 1.0f, 0.4f));
			AI_LOG("[Watch] Scene ran without errors — all runtime errors fixed.");
			runtime_fix_attempt = 0;
		}
	}

	runtime_was_playing = currently_playing;

	// Errors appeared while still playing → start the 2 s collect-then-stop timer.
	bool has_live_errors = !s_runtime_errors.is_empty() ||
			(error_monitor.is_valid() && error_monitor->get_debugger_error_count() > 0);
	if (currently_playing && has_live_errors && runtime_collect_timer->is_stopped()) {
		_append_message("System", String(U"⚠ ") + TR(STR_WATCH_DETECTING), Color(1.0f, 0.82f, 0.25f));
		runtime_collect_timer->start();
	}
}

// 2 s one-shot: fired after the first error during play — stop the scene now.
void AIAssistantPanel::_on_runtime_collect_timeout() {
	bool has_errors = !s_runtime_errors.is_empty() ||
			(error_monitor.is_valid() && error_monitor->get_debugger_error_count() > 0);
	if (EditorInterface::get_singleton()->is_playing_scene() && has_errors) {
		AI_LOG("[Watch] Collect timeout — stopping scene to apply fix.");
		EditorInterface::get_singleton()->stop_playing_scene();
		// poll timer will detect the transition and call _trigger_runtime_fix().
	}
}

// Build prompt and send to AI for fixing.
void AIAssistantPanel::_trigger_runtime_fix() {
	// Collect errors from both sources: in-process handler + remote debugger protocol.
	String errors = s_runtime_errors;
	if (error_monitor.is_valid() && error_monitor->get_debugger_error_count() > 0) {
		String dbg = error_monitor->get_recent_debugger_errors(20);
		if (!dbg.is_empty()) {
			if (!errors.is_empty()) {
				errors += "\n---\n";
			}
			errors += dbg;
		}
	}

	if (errors.is_empty()) {
		return;
	}

	if (runtime_fix_attempt >= RUNTIME_MAX_ATTEMPTS) {
		_append_message("System",
				String(U"❌ ") + TR(STR_WATCH_FAILED),
				Color(1.0f, 0.4f, 0.4f));
		AI_WARN("[Watch] Max fix attempts reached. Giving up.");
		runtime_fix_in_progress = false;
		runtime_fix_attempt = 0;
		return;
	}

	runtime_fix_attempt++;
	runtime_fix_in_progress = true;

	String attempt_str = itos(runtime_fix_attempt) + "/" + itos(RUNTIME_MAX_ATTEMPTS);
	AI_LOG("[Watch] Fix attempt " + attempt_str);
	_append_message("System",
			String(U"🔄 ") + TR(STR_WATCH_FIXING) + " (" + attempt_str + ")",
			Color(1.0f, 0.82f, 0.25f));

	// Clear both buffers after snapshotting.
	s_runtime_errors = "";
	if (error_monitor.is_valid()) {
		error_monitor->clear_debugger_errors();
	}

	// ── Parse unique res:// .gd file paths from the error text ─────────────
	Vector<String> error_lines = errors.split("\n");
	HashSet<String> seen_files;
	String scripts_section;

	for (int i = 0; i < error_lines.size(); i++) {
		const String &line = error_lines[i];
		int search_from = 0;
		while (true) {
			int res_pos = line.find("res://", search_from);
			if (res_pos < 0) {
				break;
			}
			// Read characters until a natural delimiter.
			int end = res_pos + 6;
			while (end < line.length()) {
				char32_t c = line[end];
				if (c == ' ' || c == '\t' || c == '"' || c == '\'' || c == ')' || c == ']') {
					break;
				}
				// "path.gd:42" → stop before the colon+digit that is the line number.
				if (c == ':' && end + 1 < line.length() && line[end + 1] >= '0' && line[end + 1] <= '9') {
					break;
				}
				end++;
			}
			String file_path = line.substr(res_pos, end - res_pos);

			if (file_path.ends_with(".gd") && !seen_files.has(file_path)) {
				seen_files.insert(file_path);
				Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ);
				if (f.is_valid()) {
					scripts_section += "\nFile: " + file_path + "\n```gdscript\n" + f->get_as_text() + "\n```\n";
				}
			}
			search_from = end;
		}
	}

	// ── Build AI prompt ─────────────────────────────────────────────────────
	String prompt =
			"RUNTIME ERROR AUTO-FIX: The following errors occurred when running the scene.\n\n"
			"ERRORS:\n" +
			errors + "\n\n";

	if (!scripts_section.is_empty()) {
		prompt += "ERRORED SCRIPTS:\n" + scripts_section + "\n\n";
	}

	prompt +=
			"Write EditorScript body code that fixes all the errors.\n"
			"Requirements:\n"
			"1. For each errored script: use FileAccess.open(path, FileAccess.WRITE) + store_line() to write the COMPLETE fixed script\n"
			"2. At the end, call: get_editor_interface().get_resource_filesystem().scan()\n"
			"3. Output ONLY the ```gdscript code block — no explanation before or after\n"
			"4. Write the ENTIRE fixed file contents (not just the changed lines)\n"
			"5. Keep the same file paths as the originals\n";

	_send_to_api(prompt);
}

// --- N8/N9: Asset Generation ---

void AIAssistantPanel::_handle_image_generation(const String &p_prompt, const String &p_save_path) {
	if (current_provider->get_api_key().is_empty()) {
		_append_message("System", "API key required for image generation.", Color(1.0, 0.4, 0.4));
		return;
	}

	_append_message("System", "Generating image: " + p_prompt + "...", Color(0.5, 0.8, 1.0));

	String body = AIImageGenerator::build_dalle_request(p_prompt);
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + current_provider->get_api_key());

	pending_asset_type = "image";
	pending_asset_path = p_save_path;

	asset_http_request->cancel_request();
	Error err = asset_http_request->request("https://api.openai.com/v1/images/generations", headers, HTTPClient::METHOD_POST, body);
	if (err != OK) {
		_append_message("System", "Failed to send image generation request.", Color(1.0, 0.4, 0.4));
	}
}

void AIAssistantPanel::_handle_audio_generation(const String &p_prompt, const String &p_save_path) {
	if (current_provider->get_api_key().is_empty()) {
		_append_message("System", "API key required for audio generation.", Color(1.0, 0.4, 0.4));
		return;
	}

	_append_message("System", "Generating audio: " + p_prompt + "...", Color(0.5, 0.8, 1.0));

	String body = AIAudioGenerator::build_tts_request(p_prompt);
	Vector<String> headers;
	headers.push_back("Content-Type: application/json");
	headers.push_back("Authorization: Bearer " + current_provider->get_api_key());

	pending_asset_type = "audio";
	pending_asset_path = p_save_path;

	asset_http_request->cancel_request();
	Error err = asset_http_request->request("https://api.openai.com/v1/audio/speech", headers, HTTPClient::METHOD_POST, body);
	if (err != OK) {
		_append_message("System", "Failed to send audio generation request.", Color(1.0, 0.4, 0.4));
	}
}

void AIAssistantPanel::_on_asset_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
		_append_message("System", "Asset generation failed (HTTP " + itos(p_response_code) + "): " + body_text.left(300), Color(1.0, 0.4, 0.4));
		return;
	}

	if (pending_asset_type == "image") {
		String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
		String b64 = AIImageGenerator::parse_dalle_response(body_text);
		if (!b64.is_empty()) {
			String path = pending_asset_path.is_empty() ? "res://ai_generated_image.png" : pending_asset_path;
			if (AIImageGenerator::save_base64_as_texture(b64, path)) {
				_append_message("System", "Image saved: " + path, Color(0.5, 1.0, 0.5));
			} else {
				_append_message("System", "Failed to save image.", Color(1.0, 0.4, 0.4));
			}
		} else {
			_append_message("System", "Failed to parse image response.", Color(1.0, 0.4, 0.4));
		}
	} else if (pending_asset_type == "audio") {
		String path = pending_asset_path.is_empty() ? "res://ai_generated_audio.mp3" : pending_asset_path;
		// Ensure .mp3 extension since we request mp3 format from TTS API.
		if (path.ends_with(".wav")) {
			path = path.get_basename() + ".mp3";
		}
		if (AIAudioGenerator::save_audio_bytes(p_body, path)) {
			_append_message("System", "Audio saved: " + path, Color(0.5, 1.0, 0.5));
		} else {
			_append_message("System", "Failed to save audio.", Color(1.0, 0.4, 0.4));
		}
	}

	pending_asset_type = "";
	pending_asset_path = "";
}

// --- Display Helpers ---

void AIAssistantPanel::_append_message(const String &p_sender, const String &p_text, const Color &p_color, bool p_bbcode) {
	chat_display->push_color(p_color);
	chat_display->push_bold();
	chat_display->add_text(p_sender + ": ");
	chat_display->pop();
	chat_display->pop();

	if (p_bbcode) {
		chat_display->append_text(p_text); // Parses BBCode (used for @mention pills).
	} else {
		chat_display->add_text(p_text);
	}
	chat_display->add_newline();
	chat_display->add_newline();
}

void AIAssistantPanel::_append_code_block(const String &p_code) {
	// N5: Store code block for save feature.
	int block_idx = displayed_code_blocks.size();
	displayed_code_blocks.push_back(p_code);

	chat_display->push_color(Color(0.4, 0.4, 0.4));
	chat_display->add_text("--- GDScript --- ");
	chat_display->pop();

	// N5: Add save link.
	chat_display->push_color(Color(0.4, 0.8, 1.0));
	chat_display->push_meta("save:" + itos(block_idx));
	chat_display->add_text("[Save]");
	chat_display->pop();
	chat_display->pop();

	chat_display->add_newline();

	chat_display->push_bgcolor(Color(0.15, 0.15, 0.2, 0.8));
	chat_display->push_color(Color(0.9, 0.85, 0.7));
	chat_display->push_font_size(13);

	Vector<String> lines = p_code.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		chat_display->add_text(lines[i]);
		if (i < lines.size() - 1) {
			chat_display->add_newline();
		}
	}

	chat_display->pop();
	chat_display->pop();
	chat_display->pop();
	chat_display->add_newline();
	chat_display->add_newline();
}

String AIAssistantPanel::_get_system_prompt(const String &p_current_message) const {
	String prompt = AISystemPrompt::get_base_prompt();

	// Mode-specific instructions.
	AIMode mode = _get_current_mode();
	if (mode == MODE_ASK) {
		prompt = "You are in ASK mode. Only answer questions with text explanations. Do NOT generate code blocks.\n\n" + prompt;
	} else if (mode == MODE_PLAN) {
		prompt = "You are in PLAN mode. First create a numbered step-by-step plan, then generate code to implement ALL steps in a single code block.\n\n" + prompt;
	}

	// N10: Detect UI request from current user message and add UI-specialized prompt.
	if (AIUIAgent::is_ui_request(p_current_message)) {
		prompt += AIUIAgent::get_ui_system_prompt();
	}

	// N11: Domain-specific prompt injection (Animation, TileMap, Navigation, etc.)
	{
		String domain_prompt = AIDomainPrompts::build_contextual_prompt(p_current_message);
		if (!domain_prompt.is_empty()) {
			prompt += domain_prompt;
		}
	}

	// N12: Lazy-loaded API docs + gotchas — inject compact references for any
	// Godot class or topic mentioned in the user's message. Only matching entries
	// are injected, keeping token cost proportional to what's being discussed.
	{
		String api_docs = AIAPIDocLoader::get_docs_for_message(p_current_message);
		if (!api_docs.is_empty()) {
			prompt += api_docs;
		}
		String gotchas = AIGotchasIndex::get_for_message(p_current_message);
		if (!gotchas.is_empty()) {
			prompt += "\n";
			prompt += gotchas;
		}
	}

	prompt += context_collector->build_context_prompt();

	// Performance snapshot — always include a brief one for context.
	{
		String perf = context_collector->get_performance_snapshot();
		if (!perf.is_empty()) {
			prompt += "\n--- " + perf + "---\n";
		}
	}

	// N7: Add profiler data if "Performance" preset was used.
	{
		String content = p_current_message.to_lower();
		if (content.find("performance") != -1 || content.find("optimize") != -1 ||
				content.find("fps") != -1 || content.find(String::utf8("性能")) != -1 ||
				content.find(String::utf8("优化")) != -1) {
			prompt += "\n--- PERFORMANCE SNAPSHOT ---\n";
			prompt += profiler_collector->collect_performance_snapshot();
			prompt += "--- END PERFORMANCE SNAPSHOT ---\n";
		}
	}

	// N1: Add recent console errors context.
	// Always include for error/fix/bug related queries; otherwise only if errors exist.
	if (error_monitor.is_valid()) {
		String content = p_current_message.to_lower();
		bool is_error_query = content.find("error") != -1 || content.find("fix") != -1 ||
				content.find("bug") != -1 || content.find("warning") != -1 ||
				content.find(String::utf8("错误")) != -1 || content.find(String::utf8("修复")) != -1 ||
				content.find(String::utf8("警告")) != -1 || content.find("debug") != -1;

		if (is_error_query || error_monitor->get_recent_error_count() > 0) {
			prompt += "\n--- RECENT CONSOLE ERRORS (editor, not caused by AI) ---\n";
			prompt += error_monitor->get_recent_console_errors(10);
			prompt += "\n--- END CONSOLE ERRORS ---\n";
		}

		// Include game runtime debugger errors (from running the game).
		if (is_error_query || error_monitor->get_debugger_error_count() > 0) {
			String dbg_errors = error_monitor->get_recent_debugger_errors(10);
			if (!dbg_errors.is_empty()) {
				prompt += "\n--- GAME RUNTIME ERRORS (from running the game, shown in debugger panel) ---\n";
				prompt += dbg_errors;
				prompt += "\n--- END GAME RUNTIME ERRORS ---\n";
			}
		}
	}

	return prompt;
}

// --- Presets ---

void AIAssistantPanel::_on_preset_pressed(const String &p_prompt) {
	if (is_waiting_response) {
		return;
	}
	if (history_visible) {
		_show_chat_view();
	}
	input_field->set_text(p_prompt);
	_on_send_pressed();
}

// --- History View ---

void AIAssistantPanel::_show_history_view() {
	_populate_history_list();
	chat_display->set_visible(false);
	input_bar->set_visible(false);
	history_scroll->set_visible(true);
	history_visible = true;
	history_button->set_text(TR(STR_BTN_BACK));
}

void AIAssistantPanel::_show_chat_view() {
	history_scroll->set_visible(false);
	chat_display->set_visible(true);
	input_bar->set_visible(true);
	history_visible = false;
	history_button->set_text(TR(STR_BTN_HISTORY));
}

// --- Chat History Persistence ---

String AIAssistantPanel::_get_chats_dir() const {
	return "user://ai_assistant_chats";
}

void AIAssistantPanel::_generate_chat_id() {
	Dictionary dt = Time::get_singleton()->get_datetime_dict_from_system();
	current_chat_id = "chat_" +
			itos((int)dt["year"]) +
			String::num_int64((int)dt["month"]).lpad(2, "0") +
			String::num_int64((int)dt["day"]).lpad(2, "0") + "_" +
			String::num_int64((int)dt["hour"]).lpad(2, "0") +
			String::num_int64((int)dt["minute"]).lpad(2, "0") +
			String::num_int64((int)dt["second"]).lpad(2, "0");
	AI_LOG("New chat ID: " + current_chat_id);
}

void AIAssistantPanel::_save_current_chat() {
	if (conversation_history.is_empty() || current_chat_id.is_empty()) {
		return;
	}

	String dir_path = _get_chats_dir();

	// Resolve the actual filesystem path and create directory.
	String absolute_dir = ProjectSettings::get_singleton()->globalize_path(dir_path);
	if (!DirAccess::dir_exists_absolute(absolute_dir)) {
		DirAccess::make_dir_recursive_absolute(absolute_dir);
	}

	// Use full_conversation_history for the title (first user message).
	String title = "Untitled";
	for (int i = 0; i < full_conversation_history.size(); i++) {
		Dictionary msg = full_conversation_history[i];
		if (String(msg["role"]) == "user") {
			title = String(msg["content"]).left(60);
			break;
		}
	}

	Dictionary chat_data;
	chat_data["id"] = current_chat_id;
	chat_data["title"] = title;
	// Always save the FULL history so old messages are never lost on reload.
	chat_data["message_count"] = full_conversation_history.size();
	chat_data["messages"] = full_conversation_history;
	// context_summary is still saved for API use on reload,
	// but display always comes from the full messages list.
	if (!context_summary.is_empty()) {
		chat_data["context_summary"] = context_summary;
	}

	String json_str = JSON::stringify(chat_data, "\t");

	String file_path = dir_path + "/" + current_chat_id + ".json";
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::WRITE);
	if (f.is_valid()) {
		f->store_string(json_str);
		AI_LOG("Chat saved: " + file_path);
	} else {
		AI_ERR("Failed to save chat: " + file_path);
	}
}

void AIAssistantPanel::_load_chat(const String &p_file_path) {
	Ref<FileAccess> f = FileAccess::open(p_file_path, FileAccess::READ);
	if (!f.is_valid()) {
		AI_ERR("Failed to open chat file: " + p_file_path);
		return;
	}

	String json_str = f->get_as_text();
	JSON json;
	Error err = json.parse(json_str);
	if (err != OK) {
		AI_ERR("Failed to parse chat file: " + p_file_path);
		return;
	}

	Dictionary chat_data = json.get_data();

	if (!conversation_history.is_empty()) {
		_save_current_chat();
	}

	current_chat_id = chat_data["id"];
	// Load full history (all messages, never trimmed).
	full_conversation_history = chat_data["messages"];
	// Also restore working copy — compression will re-trim on next API call if needed.
	conversation_history = full_conversation_history.duplicate();
	context_summary = chat_data.has("context_summary") ? String(chat_data["context_summary"]) : "";
	displayed_code_blocks.clear();

	// Disable scroll_follow while bulk-loading history so the scroll bar
	// doesn't chase every added paragraph and end up at the bottom.
	chat_display->set_scroll_follow(false);
	chat_display->clear();
	stored_thinking_texts.clear();
	for (int i = 0; i < conversation_history.size(); i++) {
		Dictionary msg = conversation_history[i];
		String role = msg["role"];
		String content = msg["content"];

		if (role == "user") {
			_append_message("You", content, Color(0.9, 0.9, 0.9));
		} else if (role == "assistant") {
			// Render thinking as collapsed clickable link if present.
			String thinking_text = msg.has("thinking") ? String(msg["thinking"]) : "";

			// Also handle <think>...</think> tags embedded in content by some models.
			String clean_content = content;
			if (clean_content.find("<think>") != -1) {
				int think_start = clean_content.find("<think>");
				int think_end = clean_content.find("</think>");
				if (think_end != -1) {
					if (thinking_text.is_empty()) {
						thinking_text = clean_content.substr(think_start + 7, think_end - think_start - 7).strip_edges();
					}
					clean_content = clean_content.substr(0, think_start) + clean_content.substr(think_end + 8);
					clean_content = clean_content.strip_edges();
				}
			}

			if (!thinking_text.is_empty()) {
				int thinking_id = stored_thinking_texts.size();
				stored_thinking_texts.push_back(thinking_text);

				chat_display->push_color(Color(0.5, 0.5, 0.6));
				chat_display->push_italics();
				chat_display->add_text("[");
				chat_display->push_meta("thinking:" + itos(thinking_id));
				chat_display->push_color(Color(0.4, 0.7, 1.0));
				chat_display->add_text(TR(STR_THINKING_COLLAPSED));
				chat_display->pop(); // color
				chat_display->pop(); // meta
				chat_display->add_text("]");
				chat_display->pop(); // italics
				chat_display->pop(); // color
				chat_display->add_newline();
			}

			Dictionary parsed_msg = response_parser->parse(clean_content);
			Array text_segs = parsed_msg["text_segments"];
			Array code_blks_raw = parsed_msg["code_blocks"];

			// Deduplicate consecutive identical code blocks.
			Array code_blks;
			for (int j = 0; j < code_blks_raw.size(); j++) {
				if (j == 0 || String(code_blks_raw[j]) != String(code_blks_raw[j - 1])) {
					code_blks.push_back(code_blks_raw[j]);
				}
			}

			if (!code_blks.is_empty()) {
				// Store full response for [▶ Details] popup.
				int detail_id = stored_detail_responses.size();
				stored_detail_responses.push_back(clean_content);

				// Store each code block for [Save].
				for (int j = 0; j < code_blks.size(); j++) {
					displayed_code_blocks.push_back(code_blks[j]);
				}
				int last_block_idx = displayed_code_blocks.size() - 1;

				// Show any non-empty text segments (brief descriptions).
				for (int j = 0; j < text_segs.size(); j++) {
					String seg = String(text_segs[j]).strip_edges();
					if (!seg.is_empty()) {
						_append_message("AI", seg, Color(0.7, 0.85, 1.0));
					}
				}

				// Compact format: "AI: [summary]  [▶ Details] [Save]"
				String summary = _extract_code_summary(code_blks[code_blks.size() - 1]);

				chat_display->push_color(Color(0.7, 0.85, 1.0));
				chat_display->push_bold();
				chat_display->add_text("AI: ");
				chat_display->pop();
				chat_display->pop();

				chat_display->push_color(Color(0.5, 1.0, 0.5));
				chat_display->add_text(summary);
				chat_display->pop();
				chat_display->add_text("  ");

				chat_display->push_color(Color(0.6, 0.6, 0.8));
				chat_display->push_meta("details:" + itos(detail_id));
				chat_display->add_text(String::utf8("[\xe2\x96\xb6 Details]"));
				chat_display->pop();
				chat_display->pop();
				chat_display->add_text(" ");

				chat_display->push_color(Color(0.4, 0.8, 1.0));
				chat_display->push_meta("save:" + itos(last_block_idx));
				chat_display->add_text("[Save]");
				chat_display->pop();
				chat_display->pop();

				chat_display->add_newline();
				chat_display->add_newline();
			} else {
				// Text-only response: render normally.
				for (int j = 0; j < text_segs.size(); j++) {
					_append_message("AI", text_segs[j], Color(0.7, 0.85, 1.0));
				}
			}
		}
	}

	AI_LOG("Loaded chat: " + current_chat_id + " (" + itos(conversation_history.size()) + " messages)");

	// Scroll to the top so the user sees the conversation from the beginning,
	// then re-enable scroll_follow for new incoming messages.
	chat_display->scroll_to_line(0);
	chat_display->set_scroll_follow(true);
}

void AIAssistantPanel::_populate_history_list() {
	while (history_container->get_child_count() > 0) {
		Node *child = history_container->get_child(0);
		history_container->remove_child(child);
		memdelete(child);
	}
	history_file_list.clear();

	String dir_path = _get_chats_dir();
	Ref<DirAccess> dir = DirAccess::open(dir_path);
	if (!dir.is_valid()) {
		Label *empty_label = memnew(Label);
		empty_label->set_text(TR(STR_SYS_NO_HISTORY));
		empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		history_container->add_child(empty_label);
		return;
	}

	Vector<String> files;
	dir->list_dir_begin();
	String fname = dir->get_next();
	while (!fname.is_empty()) {
		if (!dir->current_is_dir() && fname.ends_with(".json")) {
			files.push_back(fname);
		}
		fname = dir->get_next();
	}
	dir->list_dir_end();

	files.sort();
	files.reverse();

	if (files.is_empty()) {
		Label *empty_label = memnew(Label);
		empty_label->set_text(TR(STR_SYS_NO_HISTORY));
		empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		history_container->add_child(empty_label);
		return;
	}

	for (int i = 0; i < files.size(); i++) {
		String full_path = dir_path + "/" + files[i];
		Ref<FileAccess> f = FileAccess::open(full_path, FileAccess::READ);
		if (!f.is_valid()) {
			continue;
		}

		String json_str = f->get_as_text();
		JSON json;
		if (json.parse(json_str) != OK) {
			continue;
		}

		Dictionary chat_data = json.get_data();
		String title = chat_data.get("title", "Untitled");
		int msg_count = chat_data.get("message_count", 0);

		HBoxContainer *row = memnew(HBoxContainer);
		history_container->add_child(row);

		Button *title_btn = memnew(Button);
		title_btn->set_text(title + "  (" + itos(msg_count) + " msgs)");
		title_btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		title_btn->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		title_btn->set_flat(true);
		title_btn->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_history_item_clicked).bind(full_path));
		row->add_child(title_btn);

		Button *del_btn = memnew(Button);
		del_btn->set_text("X");
		del_btn->set_tooltip_text("Delete this chat");
		del_btn->set_custom_minimum_size(Size2(28, 0));
		del_btn->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_history_item_deleted).bind(full_path));
		row->add_child(del_btn);

		history_file_list.push_back(full_path);
	}
}

void AIAssistantPanel::_on_history_item_clicked(const String &p_file_path) {
	_load_chat(p_file_path);
	_show_chat_view();
}

void AIAssistantPanel::_on_history_item_deleted(const String &p_file_path) {
	bool is_current = !current_chat_id.is_empty() && p_file_path.contains(current_chat_id);

	Ref<DirAccess> dir = DirAccess::open(_get_chats_dir());
	if (dir.is_valid()) {
		dir->remove(p_file_path.get_file());
		AI_LOG("Deleted chat: " + p_file_path);
	}

	if (is_current) {
		chat_display->clear();
		conversation_history.clear();
		full_conversation_history.clear();
		pending_code = "";
		pending_plan_code = "";
		current_chat_id = "";
		_append_message("System", TR(STR_SYS_CHAT_DELETED), Color(0.6, 0.8, 1.0));
	}

	callable_mp(this, &AIAssistantPanel::_populate_history_list).call_deferred();
}

// --- Constructor ---

AIAssistantPanel::AIAssistantPanel() {
	set_name("Godot AI");

	response_parser.instantiate();
	script_executor.instantiate();
	context_collector.instantiate();
	error_monitor.instantiate();
	checkpoint_manager.instantiate();
	permission_manager.instantiate();
	image_generator.instantiate();
	audio_generator.instantiate();
	profiler_collector.instantiate();

	// --- Toolbar ---
	HBoxContainer *toolbar = memnew(HBoxContainer);
	add_child(toolbar);

	title_label = memnew(Label);
	title_label->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	title_label->set_text(TR(STR_PANEL_TITLE));
	title_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	toolbar->add_child(title_label);

	new_chat_button = memnew(Button);
	new_chat_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	new_chat_button->set_text(TR(STR_BTN_NEW));
	new_chat_button->set_tooltip_text(TR(STR_TIP_NEW_CHAT));
	new_chat_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_new_chat_pressed));
	toolbar->add_child(new_chat_button);

	history_button = memnew(Button);
	history_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	history_button->set_text(TR(STR_BTN_HISTORY));
	history_button->set_tooltip_text(TR(STR_TIP_HISTORY));
	history_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_history_pressed));
	toolbar->add_child(history_button);

	screenshot_button = memnew(Button);
	screenshot_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	screenshot_button->set_text(TR(STR_BTN_SCREENSHOT));
	screenshot_button->set_tooltip_text(TR(STR_TIP_SCREENSHOT));
	screenshot_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_screenshot_pressed));
	toolbar->add_child(screenshot_button);

	settings_button = memnew(Button);
	settings_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	settings_button->set_text(TR(STR_BTN_SETTINGS));
	settings_button->set_tooltip_text(TR(STR_TIP_SETTINGS));
	settings_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_settings_pressed));
	toolbar->add_child(settings_button);

	// Mode selector.
	mode_selector = memnew(OptionButton);
	mode_selector->add_item(TR(STR_MODE_ASK));
	mode_selector->add_item(TR(STR_MODE_AGENT));
	mode_selector->add_item(TR(STR_MODE_PLAN));
	mode_selector->select(1); // Default: AGENT
	mode_selector->set_tooltip_text(TR(STR_MODE_TOOLTIP));
	mode_selector->connect("item_selected", callable_mp(this, &AIAssistantPanel::_on_mode_changed));
	toolbar->add_child(mode_selector);

	// Runtime poll timer (200 ms, continuous) — always-on, no toggle button needed.
	runtime_poll_timer = memnew(Timer);
	runtime_poll_timer->set_wait_time(0.2);
	runtime_poll_timer->set_one_shot(false);
	runtime_poll_timer->set_autostart(false); // Started in NOTIFICATION_READY.
	runtime_poll_timer->connect("timeout", callable_mp(this, &AIAssistantPanel::_on_runtime_poll_timeout));
	add_child(runtime_poll_timer);

	// Error collect timer (2 s, one-shot — stops scene after first error).
	runtime_collect_timer = memnew(Timer);
	runtime_collect_timer->set_wait_time(2.0);
	runtime_collect_timer->set_one_shot(true);
	runtime_collect_timer->set_autostart(false);
	runtime_collect_timer->connect("timeout", callable_mp(this, &AIAssistantPanel::_on_runtime_collect_timeout));
	add_child(runtime_collect_timer);

	// --- Chat Display ---
	chat_display = memnew(RichTextLabel);
	chat_display->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	chat_display->set_scroll_follow(true);
	chat_display->set_selection_enabled(true);
	chat_display->set_context_menu_enabled(true);
	chat_display->set_use_bbcode(true);
	chat_display->connect("meta_clicked", callable_mp(this, &AIAssistantPanel::_on_meta_clicked));
	add_child(chat_display);

	// --- Preset Buttons ---
	preset_bar = memnew(HBoxContainer);
	add_child(preset_bar);

	struct PresetInfo {
		AILocalization::StringID label_id;
		const char *prompt;
	};
	// Only keep high-frequency presets: Fix Errors and Performance.
	PresetInfo presets[] = {
		{ AILocalization::STR_PRESET_FIX_ERRORS, "Analyze the recent errors and warnings from both the editor console and the game debugger panel, then fix them" },
		{ AILocalization::STR_PRESET_PERFORMANCE, "Analyze the current performance metrics and suggest optimizations" },
	};

	for (int i = 0; i < 2; i++) {
		Button *btn = memnew(Button);
		btn->set_text(AILocalization::get(presets[i].label_id));
		btn->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		btn->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_preset_pressed).bind(String(presets[i].prompt)));
		preset_bar->add_child(btn);
		preset_buttons.push_back(btn);
	}

	// --- History View ---
	history_scroll = memnew(ScrollContainer);
	history_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	history_scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	history_scroll->set_visible(false);
	add_child(history_scroll);

	history_container = memnew(VBoxContainer);
	history_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	history_scroll->add_child(history_container);

	// --- Input Area ---
	input_bar = memnew(HBoxContainer);
	add_child(input_bar);

	// N4: File attach button — opens EditorFileDialog to pick a text/script file.
	attach_button = memnew(Button);
	attach_button->set_text("+");
	attach_button->set_tooltip_text(TTR("Attach file (.md, .mermaid, .txt, .json, .yaml, .gd ...)"));
	attach_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_attach_pressed));
	input_bar->add_child(attach_button);

	// @ Mention button — inserts @NodeName for currently selected scene node.
	mention_button = memnew(Button);
	mention_button->set_text("@");
	mention_button->set_tooltip_text(TTR("Insert mention of selected scene node (@NodeName)"));
	mention_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_mention_button_pressed));
	input_bar->add_child(mention_button);

	input_field = memnew(AIMentionTextEdit);
	input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_ENTER));
	input_field->set_custom_minimum_size(Size2(0, 60));
	input_field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	// Do NOT set SIZE_EXPAND_FILL vertically — height is controlled manually.
	input_field->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	input_field->set_line_wrapping_mode(TextEdit::LineWrappingMode::LINE_WRAPPING_BOUNDARY);
	input_field->connect("gui_input", callable_mp(this, &AIAssistantPanel::_on_input_gui_input));
	input_field->connect("text_changed", callable_mp(this, &AIAssistantPanel::_on_input_text_changed));
	input_bar->add_child(input_field);

	// SyntaxHighlighter that hides PUA placeholder characters (rendered as chips).
	mention_highlighter = Ref<AIMentionHighlighter>(memnew(AIMentionHighlighter));
	input_field->set_syntax_highlighter(mention_highlighter);

	// Autocomplete overlay panel — a plain Control child of the editor root.
	// Avoids OS-window focus stealing that PopupPanel causes.
	// Added to EditorInterface::get_base_control() in NOTIFICATION_READY.
	autocomplete_panel = memnew(Panel);
	autocomplete_panel->set_mouse_filter(Control::MOUSE_FILTER_STOP);
	autocomplete_panel->hide();
	autocomplete_list = memnew(ItemList);
	autocomplete_list->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	autocomplete_list->set_focus_mode(Control::FOCUS_NONE);
	// Single-click on a list item commits the autocomplete.
	autocomplete_list->connect("item_selected",
			callable_mp(this, &AIAssistantPanel::_commit_mention_autocomplete));
	autocomplete_panel->add_child(autocomplete_list);

	send_button = memnew(Button);
	send_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	send_button->set_text(TR(STR_BTN_SEND));
	send_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_send_pressed));
	input_bar->add_child(send_button);

	stop_button = memnew(Button);
	stop_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	stop_button->set_text(U"\u25A0 Stop");
	stop_button->set_visible(false);
	stop_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_stop_pressed));
	input_bar->add_child(stop_button);

	// --- HTTP Request Nodes ---
	http_request = memnew(HTTPRequest);
	http_request->set_use_threads(true);
	http_request->set_timeout(120.0);
	http_request->connect("request_completed", callable_mp(this, &AIAssistantPanel::_on_request_completed));
	add_child(http_request);

	// N8/N9: Asset generation HTTP request.
	asset_http_request = memnew(HTTPRequest);
	asset_http_request->set_use_threads(true);
	asset_http_request->set_timeout(120.0);
	asset_http_request->connect("request_completed", callable_mp(this, &AIAssistantPanel::_on_asset_request_completed));
	add_child(asset_http_request);

	// Web search HTTP request.
	web_search.instantiate();
	web_http_request = memnew(HTTPRequest);
	web_http_request->set_use_threads(true);
	web_http_request->set_timeout(30.0);
	web_http_request->connect("request_completed", callable_mp(this, &AIAssistantPanel::_on_web_request_completed));
	add_child(web_http_request);

	// --- Stream Poll Timer ---
	stream_poll_timer = memnew(Timer);
	stream_poll_timer->set_one_shot(false);
	stream_poll_timer->set_wait_time(0.05);
	stream_poll_timer->connect("timeout", callable_mp(this, &AIAssistantPanel::_on_stream_poll));
	add_child(stream_poll_timer);

	// --- N1: Deferred Error Check Timer ---
	deferred_error_timer = memnew(Timer);
	deferred_error_timer->set_one_shot(true);
	deferred_error_timer->set_wait_time(0.5);
	deferred_error_timer->connect("timeout", callable_mp(this, &AIAssistantPanel::_on_deferred_error_check));
	add_child(deferred_error_timer);

	// --- N6: Permission Confirmation Dialog ---
	permission_dialog = memnew(ConfirmationDialog);
	permission_dialog->set_title("AI Code Execution");
	permission_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_on_permission_confirmed));
	permission_dialog->connect("canceled", callable_mp(this, &AIAssistantPanel::_on_permission_cancelled));
	add_child(permission_dialog);

	// --- Details Popup Dialog ---
	details_dialog = memnew(AcceptDialog);
	details_dialog->set_title("AI Response Details");
	details_dialog->set_ok_button_text("Close");
	add_child(details_dialog);

	details_rtl = memnew(RichTextLabel);
	details_rtl->set_custom_minimum_size(Size2(650, 400));
	details_rtl->set_selection_enabled(true);
	details_rtl->set_use_bbcode(true);
	details_rtl->set_scroll_follow(false);
	details_dialog->add_child(details_rtl);

	// --- File Attachment Dialog ---
	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_OPEN_FILE);
	file_dialog->set_title(TTR("Attach File to AI Assistant"));
	// AI-friendly text / diagram / data / script formats.
	file_dialog->add_filter("*.md,*.mmd,*.mermaid", TTR("Markdown / Diagram"));
	file_dialog->add_filter("*.txt,*.json,*.yaml,*.yml,*.toml,*.csv", TTR("Text / Data"));
	file_dialog->add_filter("*.gd,*.gdscript", TTR("GDScript"));
	file_dialog->connect("file_selected",
			callable_mp(this, &AIAssistantPanel::_on_file_attach_selected));
	add_child(file_dialog);

	// --- Settings Dialog ---
	settings_dialog = memnew(AISettingsDialog);
	add_child(settings_dialog);
	settings_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_update_provider));
	settings_dialog->connect("confirmed", callable_mp(this, &AIAssistantPanel::_refresh_ui_texts), CONNECT_DEFERRED);
	settings_dialog->connect("language_changed", callable_mp(this, &AIAssistantPanel::_refresh_ui_texts), CONNECT_DEFERRED);

	// --- Register default settings ---
	EditorSettings *es = EditorSettings::get_singleton();
	if (es) {
		if (!es->has_setting("ai_assistant/provider")) {
			es->set_setting("ai_assistant/provider", "anthropic");
		}
		if (!es->has_setting("ai_assistant/api_key")) {
			es->set_setting("ai_assistant/api_key", "");
		}
		if (!es->has_setting("ai_assistant/model")) {
			es->set_setting("ai_assistant/model", "");
		}
		if (!es->has_setting("ai_assistant/api_endpoint")) {
			es->set_setting("ai_assistant/api_endpoint", "");
		}
		if (!es->has_setting("ai_assistant/max_tokens")) {
			es->set_setting("ai_assistant/max_tokens", 4096);
		}
		if (!es->has_setting("ai_assistant/temperature")) {
			es->set_setting("ai_assistant/temperature", 0.7);
		}
		if (!es->has_setting("ai_assistant/send_on_enter")) {
			es->set_setting("ai_assistant/send_on_enter", true);
		}
		if (!es->has_setting("ai_assistant/autorun")) {
			es->set_setting("ai_assistant/autorun", true);
		}
		if (!es->has_setting("ai_assistant/restore_last_chat")) {
			es->set_setting("ai_assistant/restore_last_chat", true);
		}
		if (!es->has_setting("ai_assistant/api_key_glm")) {
			es->set_setting("ai_assistant/api_key_glm", "");
		}
		if (!es->has_setting("ai_assistant/api_key_deepseek")) {
			es->set_setting("ai_assistant/api_key_deepseek", "");
		}
	}
}

// --- Web Search ---

void AIAssistantPanel::_handle_web_search(const String &p_query, const String &p_user_message) {
	if (!web_search.is_valid() || !web_http_request) return;

	pending_web_action = "search";
	pending_web_query = p_query;
	pending_web_user_message = p_user_message;

	String url = web_search->build_search_url(p_query);
	AI_LOG("Web search: " + p_query);
	AI_LOG("  URL: " + url);

	_append_message("System", String::utf8("🔍 Searching: ") + p_query + "...", Color(0.6, 0.8, 1.0));

	Vector<String> headers;
	headers.push_back("User-Agent: GodotAI/1.0");
	web_http_request->request(url, headers, HTTPClient::METHOD_GET);
}

void AIAssistantPanel::_handle_fetch_url(const String &p_url, const String &p_user_message) {
	if (!web_search.is_valid() || !web_http_request) return;

	pending_web_action = "fetch";
	pending_web_query = p_url;
	pending_web_user_message = p_user_message;

	String url = web_search->sanitize_url(p_url);
	AI_LOG("Fetching URL: " + url);

	_append_message("System", String::utf8("🌐 Fetching: ") + url + "...", Color(0.6, 0.8, 1.0));

	Vector<String> headers;
	headers.push_back("User-Agent: GodotAI/1.0");
	headers.push_back("Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
	web_http_request->request(url, headers, HTTPClient::METHOD_GET);
}

// --- Cross-session learning ---

void AIAssistantPanel::_handle_write_memory(const String &p_content) {
	// Only write when a project is open (res:// is valid).
	if (!ProjectSettings::get_singleton()->is_project_loaded()) {
		return;
	}

	const String path = "res://godot_ai.md";
	const String section_header = "## AI-Discovered Quirks";

	// Read existing file (may not exist yet).
	String existing_text;
	Ref<FileAccess> rf = FileAccess::open(path, FileAccess::READ);
	if (rf.is_valid()) {
		existing_text = rf->get_as_text();
	}

	// Build timestamped entry.
	Dictionary dt = Time::get_singleton()->get_datetime_dict_from_system();
	String timestamp = vformat("%04d-%02d-%02d",
		(int)dt["year"], (int)dt["month"], (int)dt["day"]);
	String new_entry = "- [" + timestamp + "] " + p_content + "\n";

	String new_text;
	int section_pos = existing_text.find(section_header);
	if (section_pos >= 0) {
		// Insert after the section header line.
		int line_end = existing_text.find("\n", section_pos);
		if (line_end >= 0) {
			new_text = existing_text.substr(0, line_end + 1) + new_entry +
			           existing_text.substr(line_end + 1);
		} else {
			new_text = existing_text + "\n" + new_entry;
		}
	} else {
		// Section doesn't exist — append it.
		if (!existing_text.is_empty() && !existing_text.ends_with("\n")) {
			existing_text += "\n";
		}
		new_text = existing_text + "\n" + section_header + "\n" +
		           "_Quirks recorded automatically by Godot AI during your sessions._\n\n" +
		           new_entry;
	}

	Ref<FileAccess> wf = FileAccess::open(path, FileAccess::WRITE);
	if (wf.is_valid()) {
		wf->store_string(new_text);
		AI_LOG("WRITE_MEMORY: Appended quirk to " + path);
	} else {
		AI_WARN("WRITE_MEMORY: Could not write to " + path);
	}
}

void AIAssistantPanel::_on_web_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		AI_ERR("Web request failed: result=" + itos(p_result) + " code=" + itos(p_response_code));
		_append_message("System", "Web request failed (HTTP " + itos(p_response_code) + "). The AI will respond without web data.", Color(1.0, 0.7, 0.4));
		return;
	}

	String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());

	String web_context;
	if (pending_web_action == "search") {
		web_context = web_search->parse_search_results(body_text);
		AI_LOG("Search results parsed: " + itos(web_context.length()) + " chars");
	} else if (pending_web_action == "fetch") {
		// Check if it's HTML or plain text.
		bool is_html = body_text.strip_edges().begins_with("<") || body_text.find("<html") >= 0;
		if (is_html) {
			web_context = web_search->extract_text_from_html(body_text);
		} else {
			web_context = body_text;
			if (web_context.length() > 8000) {
				web_context = web_context.left(8000) + "\n\n[Content truncated at 8000 characters]";
			}
		}
		AI_LOG("URL content extracted: " + itos(web_context.length()) + " chars");
	}

	if (web_context.is_empty()) {
		_append_message("System", "No useful content found.", Color(1.0, 0.7, 0.4));
		return;
	}

	// Feed web results back to AI as a follow-up message.
	String follow_up = "Here are the web results for your reference:\n\n";
	follow_up += "--- WEB " + pending_web_action.to_upper() + " RESULTS ---\n";
	if (pending_web_action == "search") {
		follow_up += "Query: " + pending_web_query + "\n\n";
	} else {
		follow_up += "URL: " + pending_web_query + "\n\n";
	}
	follow_up += web_context;
	follow_up += "\n--- END WEB RESULTS ---\n\n";
	follow_up += "Based on these web results, please provide a comprehensive answer to the user's question. Summarize the key information concisely.";

	_append_message("System", String::utf8("✅ Web results received. Analyzing..."), Color(0.6, 0.8, 1.0));

	// Send to AI with web context.
	_send_to_api(follow_up);
}

// --- Context Compression ---

int AIAssistantPanel::_estimate_tokens(const String &p_text) const {
	// Rough estimate: ~4 characters per token for English, ~2 for CJK.
	// Use a conservative 3 chars/token average.
	return MAX(1, p_text.length() / 3);
}

String AIAssistantPanel::_build_message_summary(const Dictionary &p_msg) const {
	String role = p_msg["role"];
	String content = p_msg["content"];

	if (role == "user") {
		// Extract the core request — first line or first 150 chars.
		String first_line = content.get_slice("\n", 0).strip_edges();
		// Remove attachment context for summary.
		int attach_pos = first_line.find("--- ATTACHED CONTEXT ---");
		if (attach_pos >= 0) {
			first_line = first_line.left(attach_pos).strip_edges();
		}
		if (first_line.length() > 150) {
			first_line = first_line.left(150) + "...";
		}
		return "User: " + first_line;
	} else if (role == "assistant") {
		// Extract what was done — look for print() output or first meaningful line.
		// Check for code blocks — summarize as action.
		if (content.find("```") >= 0) {
			// Has code — extract the last print() content as summary.
			String summary_line;
			int search_from = 0;
			while (true) {
				int print_pos = content.find("print(\"", search_from);
				if (print_pos < 0) break;
				int str_start = print_pos + 7;
				int str_end = content.find("\"", str_start);
				if (str_end > str_start) {
					summary_line = content.substr(str_start, str_end - str_start);
				}
				search_from = str_end + 1;
			}
			if (!summary_line.is_empty()) {
				return "AI: [Code] " + summary_line;
			}
			return "AI: [Executed code block]";
		}
		// Text-only response — first 150 chars.
		String trimmed = content.strip_edges();
		if (trimmed.length() > 150) {
			trimmed = trimmed.left(150) + "...";
		}
		return "AI: " + trimmed;
	}
	return "";
}

void AIAssistantPanel::_compress_context_if_needed() {
	// Estimate total tokens in conversation history.
	int total_tokens = _estimate_tokens(context_summary);
	for (int i = 0; i < conversation_history.size(); i++) {
		Dictionary msg = conversation_history[i];
		total_tokens += _estimate_tokens(String(msg["content"]));
	}

	// Calculate threshold based on model's context length.
	// Reserve space for: system prompt (~3K tokens) + max output tokens + safety margin.
	int context_length = 8192;
	int output_tokens = 4096;
	if (current_provider.is_valid()) {
		context_length = current_provider->get_model_context_length();
		output_tokens = current_provider->get_recommended_max_tokens();
	}
	// Use 70% of (context_length - output_tokens) as the compression threshold.
	// This leaves 30% headroom for system prompt, attachments, etc.
	int TOKEN_THRESHOLD = (int)((context_length - output_tokens) * 0.7);
	// Clamp to reasonable range: minimum 4000, maximum 500000.
	TOKEN_THRESHOLD = CLAMP(TOKEN_THRESHOLD, 4000, 500000);
	const int KEEP_RECENT = 6; // Keep last 6 messages (3 exchanges) intact.

	if (total_tokens <= TOKEN_THRESHOLD || conversation_history.size() <= KEEP_RECENT) {
		return;
	}

	AI_LOG("Context compression triggered: ~" + itos(total_tokens) + " tokens, " + itos(conversation_history.size()) + " messages.");

	// Compress older messages into summary.
	int compress_count = conversation_history.size() - KEEP_RECENT;
	String new_summary;

	// Start with existing summary.
	if (!context_summary.is_empty()) {
		new_summary = context_summary + "\n";
	}

	new_summary += "--- Compressed messages ---\n";
	for (int i = 0; i < compress_count; i++) {
		Dictionary msg = conversation_history[i];
		String line = _build_message_summary(msg);
		if (!line.is_empty()) {
			new_summary += line + "\n";
		}
	}

	// Remove compressed messages from history.
	for (int i = 0; i < compress_count; i++) {
		conversation_history.remove_at(0);
	}

	context_summary = new_summary;

	// Post-compaction file restoration (inspired by Claude Code).
	// Re-inject currently open scripts so the AI doesn't "forget" the files
	// it was working on immediately after context compression.
	{
		String scripts_snapshot = context_collector->get_open_scripts_snapshot(3, 4000);
		if (!scripts_snapshot.is_empty()) {
			context_summary += scripts_snapshot;
			AI_LOG("Post-compaction: injected open scripts snapshot into context summary.");
		}
	}

	int new_tokens = _estimate_tokens(context_summary);
	for (int i = 0; i < conversation_history.size(); i++) {
		Dictionary msg = conversation_history[i];
		new_tokens += _estimate_tokens(String(msg["content"]));
	}

	AI_LOG("Context compressed: " + itos(compress_count) + " messages -> summary (" + itos(context_summary.length()) + " chars). New total: ~" + itos(new_tokens) + " tokens, " + itos(conversation_history.size()) + " messages remaining.");
}

// Parses OpenAI / Anthropic "context too long" error messages to extract the
// exact token gap, then drops the minimum number of conversation messages
// needed to cover it and retries the request automatically.
//
// Supported error formats:
//   Anthropic: "prompt is too long: 195432 tokens > 180000 maximum"
//   OpenAI:    "This model's maximum context length is 128000 tokens. However,
//               your messages resulted in 130500 tokens."
//
// Returns true if a retry was launched (caller must return immediately).
bool AIAssistantPanel::_try_reactive_compress(const String &p_error_msg) {
	// Guard: never retry a retry (prevents infinite loops).
	if (reactive_retry_pending) {
		reactive_retry_pending = false;
		return false;
	}

	// We need at least the last user message in history to retry.
	if (conversation_history.is_empty()) {
		return false;
	}

	// ── Parse token counts ───────────────────────────────────────────────────
	int actual_tokens = -1;
	int limit_tokens  = -1;

	// Anthropic: "Y tokens > X maximum"
	{
		// Look for pattern like "195432 tokens > 180000"
		int gt_pos = p_error_msg.find(" tokens > ");
		if (gt_pos >= 0) {
			// actual is the number before " tokens >"
			int num_start = gt_pos;
			while (num_start > 0 && is_digit(p_error_msg[num_start - 1])) {
				num_start--;
			}
			String actual_str = p_error_msg.substr(num_start, gt_pos - num_start);
			// limit is the number after "> "
			int limit_start = gt_pos + 10; // skip " tokens > "
			int limit_end = limit_start;
			while (limit_end < p_error_msg.length() && is_digit(p_error_msg[limit_end])) {
				limit_end++;
			}
			String limit_str = p_error_msg.substr(limit_start, limit_end - limit_start);
			if (actual_str.is_valid_int() && limit_str.is_valid_int()) {
				actual_tokens = actual_str.to_int();
				limit_tokens  = limit_str.to_int();
			}
		}
	}

	// OpenAI: "maximum context length is X tokens. ...resulted in Y tokens"
	if (actual_tokens < 0) {
		int max_pos = p_error_msg.find("maximum context length is ");
		int res_pos = p_error_msg.find("resulted in ");
		if (max_pos >= 0 && res_pos >= 0) {
			int max_num_start = max_pos + 26;
			int max_num_end = max_num_start;
			while (max_num_end < p_error_msg.length() && is_digit(p_error_msg[max_num_end])) {
				max_num_end++;
			}
			int res_num_start = res_pos + 12;
			int res_num_end = res_num_start;
			while (res_num_end < p_error_msg.length() && is_digit(p_error_msg[res_num_end])) {
				res_num_end++;
			}
			String max_str = p_error_msg.substr(max_num_start, max_num_end - max_num_start);
			String res_str = p_error_msg.substr(res_num_start, res_num_end - res_num_start);
			if (max_str.is_valid_int() && res_str.is_valid_int()) {
				limit_tokens  = max_str.to_int();
				actual_tokens = res_str.to_int();
			}
		}
	}

	// Fallback: if error mentions "context" or "too long" but no numbers, treat
	// as a generic overflow and try dropping half the history.
	bool generic_overflow = (actual_tokens < 0) &&
		(p_error_msg.find("context_length_exceeded") >= 0 ||
		 p_error_msg.find("too long") >= 0 ||
		 p_error_msg.find("maximum context") >= 0);

	if (actual_tokens < 0 && !generic_overflow) {
		return false; // Not a context-length error.
	}

	int gap_tokens = (actual_tokens > 0 && limit_tokens > 0)
		? (actual_tokens - limit_tokens + 500) // +500 safety margin
		: _estimate_tokens(context_summary);   // generic fallback

	if (gap_tokens <= 0) {
		return false;
	}

	// ── Recover the current user message ────────────────────────────────────
	// It was appended to conversation_history just before the streaming call.
	Dictionary last_user_msg = conversation_history[conversation_history.size() - 1];
	conversation_history.resize(conversation_history.size() - 1);

	// ── Drop oldest message groups until the gap is covered ─────────────────
	int freed = 0;
	int dropped_count = 0;
	while (!conversation_history.is_empty() && freed < gap_tokens) {
		Dictionary msg = conversation_history[0];
		int msg_tokens = _estimate_tokens(String(msg["content"]));
		freed += msg_tokens;
		String summary_line = _build_message_summary(msg);
		if (!context_summary.is_empty()) {
			context_summary += "\n";
		}
		context_summary += "[reactive-compacted] " + summary_line;
		conversation_history.remove_at(0);
		dropped_count++;
	}

	if (freed == 0) {
		// Nothing to drop — restore and give up.
		conversation_history.push_back(last_user_msg);
		return false;
	}

	AI_LOG(vformat("Reactive compress: dropped %d messages (~%d tokens, gap was %d). Retrying...",
		dropped_count, freed, gap_tokens));

	String notice = vformat(
		U"⚠ Context too long — dropped %d messages (~%d tokens) and retrying automatically.",
		dropped_count, freed);
	_append_message("System", notice, Color(1.0f, 0.75f, 0.2f));

	// ── Retry ────────────────────────────────────────────────────────────────
	reactive_retry_pending = true;
	// Restore UI so _send_to_api can re-arm it cleanly.
	is_waiting_response = false;
	send_button->set_visible(true);
	stop_button->set_visible(false);

	String user_content = String(last_user_msg["content"]);
	_send_to_api(user_content);
	return true;
}

AIAssistantPanel::~AIAssistantPanel() {
	if (stream_thread.is_started()) {
		stream_thread.wait_to_finish();
	}
	if (!conversation_history.is_empty()) {
		_save_current_chat();
	}
	// Must remove error handler before panel is destroyed to avoid dangling callback.
	_remove_runtime_error_handler();
}

#endif // TOOLS_ENABLED
