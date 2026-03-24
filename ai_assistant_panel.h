#pragma once

#ifdef TOOLS_ENABLED

#include "ai_provider.h"
#include "ai_response_parser.h"
#include "ai_script_executor.h"
#include "ai_context_collector.h"
#include "ai_error_monitor.h"
#include "ai_checkpoint_manager.h"
#include "ai_permission_manager.h"
#include "ai_image_generator.h"
#include "ai_audio_generator.h"
#include "ai_profiler_collector.h"
#include "ai_ui_agent.h"
#include "ai_web_search.h"

#include "scene/gui/box_container.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"

class RichTextLabel;
class TextEdit;
class Button;
class HTTPRequest;
class AISettingsDialog;
class ScrollContainer;
class Timer;
class AcceptDialog;
class ConfirmationDialog;

class OptionButton;

class AIAssistantPanel : public VBoxContainer {
	GDCLASS(AIAssistantPanel, VBoxContainer);

	enum AIMode { MODE_ASK, MODE_AGENT, MODE_PLAN };

	// UI elements — chat view.
	RichTextLabel *chat_display = nullptr;
	TextEdit *input_field = nullptr;
	Button *send_button = nullptr;
	HBoxContainer *input_bar = nullptr;
	HBoxContainer *preset_bar = nullptr;

	// Toolbar buttons.
	Label *title_label = nullptr;
	Button *new_chat_button = nullptr;
	Button *history_button = nullptr;
	Button *settings_button = nullptr;
	OptionButton *mode_selector = nullptr;
	Vector<Button *> preset_buttons;

	// History view (inline, replaces chat view when visible).
	ScrollContainer *history_scroll = nullptr;
	VBoxContainer *history_container = nullptr;
	bool history_visible = false;
	Vector<String> history_file_list;

	// Components.
	HTTPRequest *http_request = nullptr;
	Ref<AIProvider> current_provider;
	Ref<AIResponseParser> response_parser;
	Ref<AIScriptExecutor> script_executor;
	Ref<AIContextCollector> context_collector;
	AISettingsDialog *settings_dialog = nullptr;

	// N1: Error monitor.
	Ref<AIErrorMonitor> error_monitor;

	// N3: Checkpoint manager.
	Ref<AICheckpointManager> checkpoint_manager;

	// N6: Permission manager.
	Ref<AIPermissionManager> permission_manager;
	ConfirmationDialog *permission_dialog = nullptr;
	String pending_permission_code; // Code waiting for permission confirmation.

	// N7: Profiler collector.
	Ref<AIProfilerCollector> profiler_collector;

	// N8/N9: Asset generators.
	Ref<AIImageGenerator> image_generator;
	Ref<AIAudioGenerator> audio_generator;
	HTTPRequest *asset_http_request = nullptr; // For image/audio generation API calls.
	String pending_asset_type;   // "image" or "audio"
	String pending_asset_path;   // Where to save the generated asset.

	// Web search.
	Ref<AIWebSearch> web_search;
	HTTPRequest *web_http_request = nullptr;
	String pending_web_action; // "search" or "fetch"
	String pending_web_query;  // Original query or URL.
	String pending_web_user_message; // Original user message to re-send with results.

	// N4: Object attachment.
	Vector<String> pending_attachments;
	Button *attach_button = nullptr;

	// N5: Saved code blocks for "Save" feature.
	Vector<String> displayed_code_blocks;

	// Collapse: store full AI responses for [Details] popup.
	String last_full_response;
	Vector<String> stored_detail_responses;  // Full AI responses indexed by detail_id.
	Vector<String> stored_thinking_texts;    // Thinking texts indexed by thinking_id.
	AcceptDialog *details_dialog = nullptr;
	RichTextLabel *details_rtl = nullptr;

	// State.
	Array conversation_history;
	String context_summary; // Compressed summary of older messages.
	String pending_code;
	bool is_waiting_response = false;
	String current_chat_id;
	int auto_retry_count = 0;
	static const int MAX_AUTO_RETRIES = 3; // Increased from 2 to 3.

	// N1: Deferred error check timer.
	Timer *deferred_error_timer = nullptr;

	// --- Streaming state ---
	struct StreamChunk {
		String text;
		bool is_thinking = false;
	};

	Thread stream_thread;
	Mutex stream_mutex;
	Vector<StreamChunk> stream_chunk_queue;
	String stream_accumulated;         // Content text only.
	String stream_thinking_accumulated; // Thinking text only.
	bool stream_active = false;
	bool stream_finished = false;
	bool stream_error = false;
	String stream_error_msg;
	int stream_response_code = 0;
	Timer *stream_poll_timer = nullptr;
	bool stream_display_started = false;

	// Thinking display state.
	bool thinking_phase_active = false;    // Currently receiving thinking tokens.
	bool thinking_collapsed = false;       // Thinking has been collapsed into a link.
	bool ai_prefix_shown = false;          // Whether "AI: " prefix was added.
	int thinking_display_para_start = 0;   // Paragraph index where thinking display starts.

	String _stream_url;
	Vector<String> _stream_headers;
	String _stream_body;
	Ref<AIProvider> _stream_provider;

	// Methods.
	void _on_send_pressed();
	void _on_new_chat_pressed();
	void _on_history_pressed();
	void _on_settings_pressed();
	void _on_preset_pressed(const String &p_prompt);
	void _on_input_gui_input(const Ref<class InputEvent> &p_event);
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	void _send_to_api(const String &p_message);
	void _handle_ai_response(const String &p_response);
	void _append_message(const String &p_sender, const String &p_text, const Color &p_color);
	void _append_code_block(const String &p_code);
	void _update_provider();

	String _get_system_prompt(const String &p_current_message) const;

	// Streaming methods.
	void _start_streaming(const String &p_url, const Vector<String> &p_headers, const String &p_body);
	static void _stream_thread_func_static(void *p_userdata);
	void _stream_thread_func();
	void _on_stream_poll();
	void _finish_streaming();
	void _collapse_thinking_display();

	// N1: Self-healing deferred error check.
	void _execute_code_with_monitoring(const String &p_code);
	void _on_deferred_error_check();
	String _extract_code_summary(const String &p_code) const;

	// N4: Attachment methods.
	void _on_attach_pressed();
	void _attach_selected_nodes();

	// N5: Code save.
	void _on_meta_clicked(const Variant &p_meta);

	// N6: Permission confirmation.
	void _on_permission_confirmed();
	void _on_permission_cancelled();

	// N8/N9: Asset generation.
	void _handle_image_generation(const String &p_prompt, const String &p_save_path);
	void _handle_audio_generation(const String &p_prompt, const String &p_save_path);
	void _on_asset_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	// Chat history persistence.
	String _get_chats_dir() const;
	void _generate_chat_id();
	void _save_current_chat();
	void _load_chat(const String &p_file_path);
	void _show_history_view();
	void _show_chat_view();
	void _populate_history_list();
	void _on_history_item_clicked(const String &p_file_path);
	void _on_history_item_deleted(const String &p_file_path);

	void _refresh_ui_texts();
	bool _get_send_on_enter() const;
	bool _get_autorun() const;
	AIMode _get_current_mode() const;
	void _on_mode_changed(int p_index);

	// Web search.
	void _handle_web_search(const String &p_query, const String &p_user_message);
	void _handle_fetch_url(const String &p_url, const String &p_user_message);
	void _on_web_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	// Context compression.
	void _compress_context_if_needed();
	String _build_message_summary(const Dictionary &p_msg) const;
	int _estimate_tokens(const String &p_text) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	AIAssistantPanel();
	~AIAssistantPanel();
};

#endif // TOOLS_ENABLED
