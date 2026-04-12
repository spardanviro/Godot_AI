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
#include "ai_domain_prompts.h"
#include "ai_api_doc_loader.h"
#include "ai_web_search.h"
#include "ai_mention_highlighter.h"
#include "ai_mention_text_edit.h"

#include "scene/gui/box_container.h"
#include "scene/gui/item_list.h"
#include "core/os/thread.h"
#include "core/os/mutex.h"
#include "core/error/error_macros.h"

class RichTextLabel;
class Button;
class HTTPRequest;
class AISettingsDialog;
class ScrollContainer;
class Timer;
class AcceptDialog;
class ConfirmationDialog;
class Panel;
class EditorFileDialog;

class OptionButton;

class AIAssistantPanel : public VBoxContainer {
	GDCLASS(AIAssistantPanel, VBoxContainer);

public:
	// Public struct used by autocomplete and external code.
	struct NodeInfo {
		String name;
		String path;
		String type;
	};

private:
	enum AIMode { MODE_ASK, MODE_AGENT, MODE_PLAN };

	// UI elements — chat view.
	RichTextLabel *chat_display = nullptr;
	AIMentionTextEdit *input_field = nullptr;
	Button *send_button = nullptr;
	Button *stop_button = nullptr;
	HBoxContainer *input_bar = nullptr;
	HBoxContainer *preset_bar = nullptr;

	// Toolbar buttons.
	Label *title_label = nullptr;
	Button *new_chat_button = nullptr;
	Button *history_button = nullptr;
	Button *screenshot_button = nullptr;
	Button *settings_button = nullptr;
	OptionButton *mode_selector = nullptr;
	Vector<Button *> preset_buttons;

	// Screenshot state.
	String pending_screenshot_path;

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

	// N4: File attachment.
	Vector<String> pending_attachments;
	Button *attach_button = nullptr;
	EditorFileDialog *file_dialog = nullptr;

	// Paste store: large attached file content is stored here by ID so it can be
	// de-duplicated across turns. Keyed by file path to detect re-attachments.
	// Within a session, re-attaching the same file just inserts a back-reference.
	static const int PASTE_INLINE_LIMIT = 1500; // chars below which no dedup needed
	HashMap<String, int> paste_path_to_id;  // file path → paste ID
	HashMap<int, String> paste_id_to_content; // paste ID → full content
	int paste_next_id = 1;
	String _make_paste_ref(int p_id, const String &p_path, int p_char_count) const;
	String _build_attachment_content(const String &p_path, const String &p_raw_content);

	// N5: Saved code blocks for "Save" feature.
	Vector<String> displayed_code_blocks;

	// Collapse: store full AI responses for [Details] popup.
	String last_full_response;
	Vector<String> stored_detail_responses;  // Full AI responses indexed by detail_id.
	Vector<String> stored_thinking_texts;    // Thinking texts indexed by thinking_id.
	AcceptDialog *details_dialog = nullptr;
	RichTextLabel *details_rtl = nullptr;

	// State.
	Array conversation_history;      // Working copy sent to AI — may be compressed.
	Array full_conversation_history; // Complete record — never trimmed, used for display & save.
	String context_summary; // Compressed summary of older messages.
	String pending_code;
	String pending_plan_code;    // Code from PLAN mode response, waiting for user to click Execute.
	String pending_plan_md_path; // Path of plan MD file saved when plan was generated.
	String active_plan_md_path;  // Plan MD being executed right now (copied from pending at click time).
	bool is_waiting_response = false;
	String current_chat_id;
	String last_user_input; // Raw text of the most recent user-originated message (not auto-retry prompts).
	int auto_retry_count = 0;
	static const int MAX_AUTO_RETRIES = 3; // Increased from 2 to 3.

	// N1: Deferred error check timer.
	Timer *deferred_error_timer = nullptr;

	// --- Runtime Error Auto-Fix (always-on, passive) ---
	bool runtime_was_playing = false;
	bool runtime_fix_in_progress = false;   // AI response should be auto-executed
	bool runtime_restart_after_fix = false; // Restart scene after successful execution
	int runtime_fix_attempt = 0;
	static const int RUNTIME_MAX_ATTEMPTS = 5;
	String runtime_last_play_path;          // Scene path recorded when play started
	Timer *runtime_poll_timer = nullptr;    // 200 ms — detects play start/stop
	Timer *runtime_collect_timer = nullptr; // 2 s one-shot — stop scene after first error
	bool runtime_eh_installed = false;
	ErrorHandlerList runtime_eh;
	static String s_runtime_errors; // Captured error text (main-thread only)

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
	bool stream_inside_think_tag = false; // Tracking <think>...</think> in content.
	String stream_tag_buffer;          // Buffer for partial tag detection.
	bool stream_active = false;
	bool stream_finished = false;
	bool stream_error = false;
	bool stream_stop_requested = false;
	String stream_error_msg;
	int stream_response_code = 0;
	Timer *stream_poll_timer = nullptr;
	bool stream_display_started = false;

	// Thinking display state.
	bool thinking_phase_active = false;    // Currently receiving thinking tokens.
	bool thinking_collapsed = false;       // Thinking has been collapsed into a link.
	bool ai_prefix_shown = false;          // Whether "AI: " prefix was added.
	int thinking_display_para_start = 0;   // Paragraph index where thinking display starts.
	bool generic_thinking_shown = false;   // Generic "Thinking..." shown for non-thinking models.

	String _stream_url;
	Vector<String> _stream_headers;
	String _stream_body;
	Ref<AIProvider> _stream_provider;

	// Methods.
	void _on_send_pressed();
	void _on_stop_pressed();
	void _on_new_chat_pressed();
	void _on_history_pressed();
	void _on_screenshot_pressed();
	void _on_settings_pressed();
	void _on_preset_pressed(const String &p_prompt);
	void _on_input_gui_input(const Ref<class InputEvent> &p_event);
	void _on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	void _send_to_api(const String &p_message);
	// p_is_stopped_partial: true when the user pressed Stop and the response is
	// incomplete.  In that case code blocks are NOT executed and the auto-retry
	// loop is suppressed — we never want Stop to trigger new API requests.
	void _handle_ai_response(const String &p_response, bool p_is_stopped_partial = false);
	void _append_message(const String &p_sender, const String &p_text, const Color &p_color, bool p_bbcode = false);
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

	// Node mention system (chip-based via inline objects).
	Ref<AIMentionHighlighter> mention_highlighter; // Hides PUA placeholder chars.
	Panel *autocomplete_panel = nullptr;
	ItemList *autocomplete_list = nullptr;
	int autocomplete_at_line = -1;
	int autocomplete_at_col = -1;
	bool _suppress_autocomplete = false; // Prevents re-opening during commit.
	Button *mention_button = nullptr;

	void _update_input_height();
	void _on_input_text_changed();
	void _show_mention_autocomplete(int p_line, int p_col, const String &p_partial);
	void _hide_mention_autocomplete();
	void _commit_mention_autocomplete(int p_item_idx);
	void _on_mention_button_pressed();
	Vector<String> _get_all_scene_node_names() const;

	Vector<NodeInfo> _get_all_scene_node_infos() const;
	Vector<NodeInfo> autocomplete_node_infos; // Cached during autocomplete show.

	// N4: Attachment methods.
	void _on_attach_pressed();
	void _on_file_attach_selected(const String &p_path);

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

	// Runtime error auto-fix.
	static void _runtime_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_message, bool p_editor_notify, ErrorHandlerType p_type);
	void _on_runtime_poll_timeout();
	void _on_runtime_collect_timeout();
	String _save_plan_to_md(const Vector<String> &p_steps, const String &p_full_plan_text);
	void _mark_plan_steps_done(const String &p_md_path);

public:
	// Called by AIAssistantPlugin when the debugger session starts (game connected).
	// Clears stale error buffers before any new-run errors can arrive, eliminating
	// the race condition where _ready() errors arrive before the 200 ms poll fires.
	void on_game_session_started();
private:
	void _install_runtime_error_handler();
	void _remove_runtime_error_handler();
	void _trigger_runtime_fix();

	// Web search.
	void _handle_web_search(const String &p_query, const String &p_user_message);
	void _handle_fetch_url(const String &p_url, const String &p_user_message);
	void _on_web_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	// Cross-session learning — appends a discovered quirk to res://godot_ai.md.
	void _handle_write_memory(const String &p_content);

	// Context compression.
	void _compress_context_if_needed();
	// Reactive compression: parses "context too long" API errors, extracts the
	// exact token gap, drops the minimum number of message groups to cover it,
	// then retries the request. Returns true if a retry was initiated.
	bool _try_reactive_compress(const String &p_error_msg);
	bool reactive_retry_pending = false; // Guard against recursive retries.
	String _build_message_summary(const Dictionary &p_msg) const;
	int _estimate_tokens(const String &p_text) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Called by AIAssistantPlugin when "Mention in AI Assistant" is picked
	// from the scene-tree right-click menu.
	void insert_mention_of_selected_node();

	AIAssistantPanel();
	~AIAssistantPanel();
};

#endif // TOOLS_ENABLED
