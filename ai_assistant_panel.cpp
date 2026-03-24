#ifdef TOOLS_ENABLED

#include "ai_assistant_panel.h"
#include "ai_settings_dialog.h"
#include "ai_system_prompt.h"
#include "ai_localization.h"

#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/gemini_provider.h"

#include "core/config/project_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/editor_interface.h"
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
#include "scene/main/http_request.h"
#include "scene/main/timer.h"
#include "core/input/input_event.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/http_client.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/crypto/crypto.h"

#define AI_LOG(msg) print_line(String("[Godot AI] ") + msg)
#define AI_ERR(msg) ERR_PRINT(String("[Godot AI] ") + msg)
#define AI_WARN(msg) WARN_PRINT(String("[Godot AI] ") + msg)

void AIAssistantPanel::_bind_methods() {
}

void AIAssistantPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_update_provider();
			_append_message("System", TR(STR_SYS_READY), Color(0.6, 0.8, 1.0));
			AI_LOG("Panel initialized and ready.");
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
		attach_button->set_tooltip_text(TR(STR_TIP_ATTACH));
	}
	if (input_field) {
		if (_get_send_on_enter()) {
			input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_ENTER));
		} else {
			input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_CTRL));
		}
	}

	// Update preset buttons.
	AILocalization::StringID preset_ids[] = {
		AILocalization::STR_PRESET_DESCRIBE,
		AILocalization::STR_PRESET_ADD_PLAYER,
		AILocalization::STR_PRESET_ADD_LIGHT,
		AILocalization::STR_PRESET_ADD_UI,
		AILocalization::STR_PRESET_FIX_ERRORS,
		AILocalization::STR_PRESET_PERFORMANCE,
	};
	for (int i = 0; i < preset_buttons.size() && i < 6; i++) {
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
	String api_key_val = es->get_setting("ai_assistant/api_key");
	String model_val = es->get_setting("ai_assistant/model");
	String endpoint_val = es->get_setting("ai_assistant/api_endpoint");
	int max_tokens_val = es->get_setting("ai_assistant/max_tokens");
	float temperature_val = es->get_setting("ai_assistant/temperature");

	if (provider_name == "anthropic") {
		current_provider = Ref<AnthropicProvider>(memnew(AnthropicProvider));
	} else if (provider_name == "openai" || provider_name == "deepseek") {
		current_provider = Ref<OpenAIProvider>(memnew(OpenAIProvider));
		if (provider_name == "deepseek" && endpoint_val.is_empty()) {
			endpoint_val = "https://api.deepseek.com/v1/chat/completions";
		}
	} else if (provider_name == "gemini") {
		current_provider = Ref<GeminiProvider>(memnew(GeminiProvider));
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

	_append_message("You", text, Color(0.9, 0.9, 0.9));
	input_field->set_text("");

	_send_to_api(text);
}

void AIAssistantPanel::_on_new_chat_pressed() {
	if (!conversation_history.is_empty()) {
		_save_current_chat();
	}

	chat_display->clear();
	conversation_history.clear();
	context_summary = "";
	pending_code = "";
	current_chat_id = "";
	pending_attachments.clear();
	displayed_code_blocks.clear();
	stored_detail_responses.clear();
	stored_thinking_texts.clear();

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

void AIAssistantPanel::_on_settings_pressed() {
	if (settings_dialog) {
		settings_dialog->refresh();
		settings_dialog->popup_centered();
	}
}

void AIAssistantPanel::_on_input_gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && !key->is_echo()) {
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
}

// --- N4: Attachment ---

void AIAssistantPanel::_on_attach_pressed() {
	_attach_selected_nodes();
}

void AIAssistantPanel::_attach_selected_nodes() {
#ifdef TOOLS_ENABLED
	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	if (!selection) {
		_append_message("System", TR(STR_SYS_NO_SELECTION), Color(1.0, 0.7, 0.3));
		return;
	}

	TypedArray<Node> selected = selection->get_selected_nodes();
	if (selected.is_empty()) {
		_append_message("System", TR(STR_SYS_NO_NODES_SELECTED), Color(1.0, 0.7, 0.3));
		return;
	}

	for (int i = 0; i < selected.size(); i++) {
		Node *node = Object::cast_to<Node>(selected[i]);
		if (node) {
			String dump = context_collector->get_detailed_node_dump(node);
			pending_attachments.push_back(dump);
			_append_message("System", "Attached: " + node->get_name() + " (" + node->get_class() + ")", Color(0.5, 0.8, 1.0));
		}
	}
#endif
}

// --- N5: Code Save ---

void AIAssistantPanel::_on_meta_clicked(const Variant &p_meta) {
	String meta = p_meta;
	if (meta.begins_with("save:")) {
		int idx = meta.substr(5).to_int();
		if (idx >= 0 && idx < displayed_code_blocks.size()) {
			String code = displayed_code_blocks[idx];
			String filename = "ai_generated_" + itos(idx) + ".gd";

			// Try to derive a better filename from code content.
			Vector<String> lines = code.split("\n");
			for (int i = 0; i < lines.size(); i++) {
				if (lines[i].strip_edges().begins_with("extends ")) {
					String base = lines[i].strip_edges().substr(8).strip_edges();
					filename = base.to_lower() + "_script.gd";
					break;
				}
			}

			String save_path = "res://" + filename;
			Ref<FileAccess> f = FileAccess::open(save_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(code);
				_append_message("System", "Code saved to: " + save_path, Color(0.5, 1.0, 0.5));
				AI_LOG("Code saved to: " + save_path);

				// Trigger rescan.
				EditorInterface::get_singleton()->get_resource_file_system()->scan();
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
	send_button->set_disabled(true);

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
	stream_active = true;
	stream_finished = false;
	stream_error = false;
	stream_error_msg = "";
	stream_response_code = 0;
	stream_display_started = false;
	stream_mutex.unlock();

	// Reset thinking display state.
	thinking_phase_active = false;
	thinking_collapsed = false;
	ai_prefix_shown = false;
	thinking_display_para_start = 0;

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
						StreamChunk chunk;
						chunk.text = content_delta;
						chunk.is_thinking = false;
						stream_chunk_queue.push_back(chunk);
						stream_accumulated += content_delta;
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
			// Show thinking in ALL modes (gives feedback that AI is working).
			if (!thinking_phase_active) {
				// Start thinking display.
				thinking_phase_active = true;
				thinking_display_para_start = chat_display->get_paragraph_count();

				chat_display->push_color(Color(0.55, 0.55, 0.65));
				chat_display->push_italics();
				chat_display->add_text(TR(STR_THINKING_LABEL) + ": ");
			}
			chat_display->add_text(chunks[i].text);

		} else {
			// --- Content delta ---
			// If thinking was active, collapse it now.
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

		// If thinking was still active at stream end (no content followed), collapse it.
		if (thinking_phase_active && !thinking_collapsed) {
			_collapse_thinking_display();
		}

		if (error) {
			chat_display->add_newline();
			chat_display->add_newline();
			AI_ERR("Streaming error: " + error_msg);
			_append_message("System", "Error: " + error_msg, Color(1.0, 0.4, 0.4));
			is_waiting_response = false;
			send_button->set_disabled(false);
			return;
		}

		if (mode == MODE_ASK) {
			chat_display->add_newline();
			chat_display->add_newline();
		}

		String full_response = stream_accumulated;
		AI_LOG("Step 6: Streaming complete. Response length: " + itos(full_response.length()) + " chars");

		_stream_provider.unref();

		is_waiting_response = false;
		send_button->set_disabled(false);

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

void AIAssistantPanel::_handle_ai_response(const String &p_response) {
	Dictionary assistant_msg;
	assistant_msg["role"] = "assistant";
	assistant_msg["content"] = p_response;
	conversation_history.push_back(assistant_msg);

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

		// In ASK mode, don't execute code blocks - just display them.
		if (_get_current_mode() == MODE_ASK) {
			AI_LOG("  ASK mode: skipping code execution.");
			auto_retry_count = 0;
		} else {
			pending_code = code_blocks[code_blocks.size() - 1];

			AI_LOG("Step 8: Safety check...");
			String safety_error = script_executor->check_safety(pending_code);
			if (!safety_error.is_empty()) {
				AI_WARN("Safety check BLOCKED: " + safety_error);
				_append_message("System", safety_error, Color(1.0, 0.4, 0.4));
				pending_code = "";
			} else {
				// N6: Permission check.
				AIPermissionManager::PermissionResult perm = permission_manager->check_code_permissions(pending_code);
				if (!perm.allowed) {
					AI_WARN("Permission DENIED: " + perm.description);
					_append_message("System", perm.description, Color(1.0, 0.4, 0.4));
					pending_code = "";
				} else if (perm.needs_confirmation) {
					// Show confirmation dialog.
					pending_permission_code = pending_code;
					pending_code = "";
					permission_dialog->set_text(perm.description + TR(STR_PERM_PROCEED));
					permission_dialog->popup_centered();
				} else {
					// N1/N3: Execute with monitoring and checkpoint.
					if (_get_autorun()) {
						_execute_code_with_monitoring(pending_code);
						pending_code = "";
					} else {
						// Autorun disabled - ask for confirmation.
						pending_permission_code = pending_code;
						pending_code = "";
						permission_dialog->set_text(TR(STR_PERM_EXECUTE_CONFIRM));
						permission_dialog->popup_centered();
					}
				}
			}
		}
	} else {
		AI_LOG("  No code blocks found. Text-only response.");
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
	String first_line = p_code.get_slice("\n", 0).strip_edges().left(50);
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

		if (compact) {
			// Store detail response for popup.
			int detail_id = stored_detail_responses.size();
			stored_detail_responses.push_back(last_full_response);

			// Extract descriptive summary from code.
			String summary = _extract_code_summary(p_code);

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
	}

	error_monitor->clear_ai_errors();
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

void AIAssistantPanel::_append_message(const String &p_sender, const String &p_text, const Color &p_color) {
	chat_display->push_color(p_color);
	chat_display->push_bold();
	chat_display->add_text(p_sender + ": ");
	chat_display->pop();
	chat_display->pop();

	chat_display->add_text(p_text);
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

	prompt += context_collector->build_context_prompt();

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
				content.find(String::utf8("错误")) != -1 || content.find(String::utf8("修复")) != -1;

		if (is_error_query || error_monitor->get_recent_error_count() > 0) {
			prompt += "\n--- RECENT CONSOLE ERRORS (not caused by AI) ---\n";
			prompt += error_monitor->get_recent_console_errors(10);
			prompt += "\n--- END CONSOLE ERRORS ---\n";
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

	String title = "Untitled";
	for (int i = 0; i < conversation_history.size(); i++) {
		Dictionary msg = conversation_history[i];
		if (String(msg["role"]) == "user") {
			title = String(msg["content"]).left(60);
			break;
		}
	}

	Dictionary chat_data;
	chat_data["id"] = current_chat_id;
	chat_data["title"] = title;
	chat_data["message_count"] = conversation_history.size();
	chat_data["messages"] = conversation_history;
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
	conversation_history = chat_data["messages"];
	context_summary = chat_data.has("context_summary") ? String(chat_data["context_summary"]) : "";
	displayed_code_blocks.clear();

	chat_display->clear();
	for (int i = 0; i < conversation_history.size(); i++) {
		Dictionary msg = conversation_history[i];
		String role = msg["role"];
		String content = msg["content"];

		if (role == "user") {
			_append_message("You", content, Color(0.9, 0.9, 0.9));
		} else if (role == "assistant") {
			Dictionary parsed_msg = response_parser->parse(content);
			Array text_segs = parsed_msg["text_segments"];
			Array code_blks = parsed_msg["code_blocks"];

			for (int j = 0; j < text_segs.size(); j++) {
				_append_message("AI", text_segs[j], Color(0.7, 0.85, 1.0));
			}
			for (int j = 0; j < code_blks.size(); j++) {
				_append_code_block(code_blks[j]);
			}
		}
	}

	AI_LOG("Loaded chat: " + current_chat_id + " (" + itos(conversation_history.size()) + " messages)");
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
		pending_code = "";
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
	PresetInfo presets[] = {
		{ AILocalization::STR_PRESET_DESCRIBE, "Describe the current scene structure and suggest improvements" },
		{ AILocalization::STR_PRESET_ADD_PLAYER, "Create a CharacterBody3D player with a collision shape and a camera" },
		{ AILocalization::STR_PRESET_ADD_LIGHT, "Add a DirectionalLight3D as sunlight and a WorldEnvironment with sky" },
		{ AILocalization::STR_PRESET_ADD_UI, "Create a basic HUD with a score Label and a health ProgressBar" },
		{ AILocalization::STR_PRESET_FIX_ERRORS, "Analyze the recent console errors and fix them" },
		{ AILocalization::STR_PRESET_PERFORMANCE, "Analyze the current performance metrics and suggest optimizations" },
	};

	for (int i = 0; i < 6; i++) {
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

	// N4: Attach button.
	attach_button = memnew(Button);
	attach_button->set_text("+");
	attach_button->set_tooltip_text(TR(STR_TIP_ATTACH));
	attach_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_attach_pressed));
	input_bar->add_child(attach_button);

	input_field = memnew(TextEdit);
	input_field->set_placeholder(TR(STR_INPUT_PLACEHOLDER_ENTER));
	input_field->set_custom_minimum_size(Size2(0, 60));
	input_field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	input_field->set_line_wrapping_mode(TextEdit::LineWrappingMode::LINE_WRAPPING_BOUNDARY);
	input_field->connect("gui_input", callable_mp(this, &AIAssistantPanel::_on_input_gui_input));
	input_bar->add_child(input_field);

	send_button = memnew(Button);
	send_button->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	send_button->set_text(TR(STR_BTN_SEND));
	send_button->connect("pressed", callable_mp(this, &AIAssistantPanel::_on_send_pressed));
	input_bar->add_child(send_button);

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

	int new_tokens = _estimate_tokens(context_summary);
	for (int i = 0; i < conversation_history.size(); i++) {
		Dictionary msg = conversation_history[i];
		new_tokens += _estimate_tokens(String(msg["content"]));
	}

	AI_LOG("Context compressed: " + itos(compress_count) + " messages -> summary (" + itos(context_summary.length()) + " chars). New total: ~" + itos(new_tokens) + " tokens, " + itos(conversation_history.size()) + " messages remaining.");
}

AIAssistantPanel::~AIAssistantPanel() {
	if (stream_thread.is_started()) {
		stream_thread.wait_to_finish();
	}
	if (!conversation_history.is_empty()) {
		_save_current_chat();
	}
}

#endif // TOOLS_ENABLED
