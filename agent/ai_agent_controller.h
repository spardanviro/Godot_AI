#pragma once

#ifdef TOOLS_ENABLED

#include "../common/ai_notice.h"
#include "../execution/ai_code_runner.h"
#include "ai_plan.h"
#include "../prompt/ai_prompt_builder.h"
#include "../providers/ai_provider.h"
#include "ai_response_analysis.h"
#include "ai_response_parser.h"
#include "ai_session.h"
#include "../network/ai_stream_client.h"

#include "core/templates/hash_set.h"
#include "scene/main/node.h"

class AIAssetService;
class AIHistorySummarizer;
class AIWebRequester;
class Timer;

// Drives a conversation with the model: sends requests, decides what to do
// with each reply (run its code, ask for a fix, continue a plan, fetch a web
// page) and reports what happened through signals. It owns no UI, so the same
// controller serves the dock panel and the headless eval runner.
//
// Everything in flight belongs to one Run and the controller is in exactly one
// State. Whether a request may start, whether code may execute and what Stop
// means are all answered by those two values — there are no independent
// "in progress" flags that could disagree with each other or outlive the work
// that set them.
//
//   IDLE ──submit / plan step / runtime fix──▶ STREAMING
//   STREAMING ──reply has code──▶ (AWAITING_PERMISSION ──ok──▶) execute ──▶ VERIFYING
//   STREAMING ──reply needs a fix / execution failed──▶ STREAMING   (bounded retries)
//   VERIFYING ──late errors──▶ STREAMING     VERIFYING ──clean──▶ settle
//   settle ──web marker──▶ WEB_REQUEST ──result──▶ STREAMING
//   settle ──next plan step──▶ STREAMING     settle ──otherwise──▶ IDLE
//   any state ──stop / error / denied──▶ IDLE
class AIAgentController : public Node {
	GDCLASS(AIAgentController, Node);

public:
	enum State {
		STATE_IDLE,
		STATE_STREAMING, // A request is in flight.
		STATE_AWAITING_PERMISSION, // Waiting for the user to allow the code.
		STATE_VERIFYING, // Code ran; collecting errors it raised afterwards.
		STATE_WEB_REQUEST, // Fetching a page or search results the model asked for.
	};

	enum Mode {
		MODE_ASK, // Answer in text; never run code.
		MODE_AGENT, // Run the code the model writes.
		MODE_PLAN, // Produce a numbered plan first; run it step by step on request.
	};

	enum RunKind {
		RUN_USER_TURN, // The user sent a message.
		RUN_PLAN_STEP, // One step of an approved plan.
		RUN_RUNTIME_FIX, // Fixing errors raised by the running game.
	};

	enum RunOutcome {
		OUTCOME_COMPLETED,
		OUTCOME_STOPPED, // Stopped or cancelled by the user.
		OUTCOME_FAILED, // API error, blocked code, or retries exhausted.
	};

	// The parts below the controller report with AINoticeLevel directly.
	enum NoticeLevel {
		NOTICE_INFO = AI_NOTICE_INFO,
		NOTICE_SUCCESS = AI_NOTICE_SUCCESS,
		NOTICE_WARNING = AI_NOTICE_WARNING,
		NOTICE_ERROR = AI_NOTICE_ERROR,
	};

	static const int MAX_AUTO_RETRIES = 3;
	static const int MAX_WEB_REQUESTS_PER_RUN = 3;

	// --- Actions ---
	// Starts a turn. p_message is what the model receives (mentions expanded);
	// p_raw_input is what the user typed. Returns false while busy.
	bool submit_user_message(const String &p_message, const String &p_raw_input, Mode p_mode);
	// Ends whatever is in flight. Nothing further is started on its behalf.
	void stop();
	// Runs the steps of the last generated plan. Returns false if there is none
	// or the controller is busy.
	bool execute_plan();
	// Answer to permission_requested.
	void confirm_permission(bool p_allowed);
	// Asks the model to fix runtime errors, then re-runs p_scene_path.
	bool start_runtime_fix(const String &p_errors, const String &p_scene_path);
	void post_notice(NoticeLevel p_level, const String &p_text);

	State get_state() const { return state; }
	bool is_busy() const { return state != STATE_IDLE; }
	bool has_pending_plan() const { return !plan.is_empty() && !plan.is_running(); }
	// What the most recently finished run did (see run_finished).
	Dictionary get_last_run_report() const { return last_run_report; }

	// --- Conversation ---
	AISession &get_session() { return session; }
	// All of these refuse (return false) while busy.
	bool new_session();
	bool load_session(const String &p_file_path);
	bool delete_chat(const String &p_file_path);
	void save_session();

	// Queues file content for the next message. Returns the text that will be sent.
	String add_attachment(const String &p_path, const String &p_content);
	void set_pending_screenshot(const String &p_image_path) { pending_screenshot_path = p_image_path; }

	// --- Configuration ---
	// Re-reads provider, model, compression and permission settings.
	void reload_settings();
	// Overrides the provider settings for this controller (headless runs).
	void configure_provider(const String &p_provider_id, const String &p_model, const String &p_endpoint, const String &p_api_key);
	// Answers every permission prompt with "allow". Denied categories and
	// blocked code are still refused. For unattended runs.
	// For the headless eval runner only: both refuse to work unless the
	// editor was started as an eval run (see is_eval_run()). They are bound
	// to scripting, and generated code runs in the same editor.
	void set_auto_approve(bool p_enabled);
	// True when this process was launched by evals/run_evals.py. Decided
	// once, when the first controller is created.
	static bool is_eval_run();

	Ref<AIProvider> get_provider() const { return provider; }
	Ref<AIResponseParser> get_response_parser() const { return response_parser; }
	Ref<AICheckpointManager> get_checkpoint_manager() const { return code_runner.get_checkpoint_manager(); }
	Ref<AIErrorMonitor> get_error_monitor() const { return code_runner.get_error_monitor(); }

	AIAgentController();
	~AIAgentController();

protected:
	static void _bind_methods();

private:
	// Everything that belongs to the work currently in flight. Reset as a whole
	// when the run ends, so nothing can leak into the next one.
	struct Run {
		RunKind kind = RUN_USER_TURN;
		Mode mode = MODE_AGENT;
		String user_input; // What the user typed; empty for automatic runs.
		String restart_scene_path; // RUN_RUNTIME_FIX: scene to play again afterwards.

		// Stop was pressed. From then on nothing is started or executed for
		// this run, whatever the request in flight turns out to have returned.
		bool stop_requested = false;
		int retries = 0; // Follow-ups asked of the model without user involvement.
		bool overflow_retry_used = false; // One "context too long" recovery per request.
		int web_requests = 0;
		String request_image_b64; // Screenshot sent with the last request; kept for a resend.
		String untrusted_reason; // Why the reply being handled may not be executed; empty if it may.
		// How the last request appears in the transcript; kept for a resend.
		String request_display;
		bool request_automatic = false;

		String response; // The reply being handled.
		String code; // Code waiting for permission.
		AIResponseAnalysis::Markers markers; // Side effects the reply asked for.
		bool plan_step_marked = false; // RUN_PLAN_STEP: step already ticked off.
		bool explanation_invited = false; // The model was told it may explain instead of writing code.
		bool no_code_nudged = false; // It was asked once for code after answering in text.
		bool settled_clean = false; // Code ran and raised nothing afterwards.

		// Bookkeeping reported in run_finished.
		int requests = 0;
		int executions = 0;
		bool last_execution_ok = false;
		String last_error;
	};

	State state = STATE_IDLE;
	Run run;
	// Changes whenever a run begins or ends. Code that emits a signal in the
	// middle of a transition compares it afterwards: a listener may have
	// stopped the run (or started another) from inside the signal.
	uint64_t run_serial = 0;
	Dictionary last_run_report;
	AISession session;
	AIPlan plan; // Survives between runs until replaced.

	// Collaborators.
	Ref<AIProvider> provider;
	Ref<AIResponseParser> response_parser;
	AICodeRunner code_runner;
	AIPromptBuilder prompt_builder;
	AIStreamClient stream;
	Timer *stream_poll_timer = nullptr;
	Timer *verify_timer = nullptr;
	AIWebRequester *web_requester = nullptr;
	// These two run beside the conversation and are not part of the state machine.
	AIHistorySummarizer *history_summarizer = nullptr;
	AIAssetService *asset_service = nullptr;

	// Settings.
	AISession::CompressionConfig compression_config;
	bool compression_use_ai_summary = false;
	bool auto_approve = false;
	bool provider_overridden = false;
	// "provider/model" pairs that rejected an explicit temperature this session.
	HashSet<String> models_without_temperature;

	// Input queued for the next request.
	Vector<String> pending_attachments;
	String pending_screenshot_path;

	// Size of the last system prompt, so compression knows the window left for history.
	int last_system_prompt_tokens = 0;

	// --- State machine ---
	void _set_state(State p_state);
	void _begin_run(RunKind p_kind, Mode p_mode, const String &p_user_input);
	// Ends the run and returns to IDLE.
	void _end_run(RunOutcome p_outcome);
	// Reports the run and clears it without leaving the busy state (the caller
	// starts the next run immediately).
	void _report_run(RunOutcome p_outcome);
	void _fail_run(const String &p_error = String());
	// True (after ending the run) if Stop arrived while a reply was being handled.
	bool _stopped_meanwhile();
	// Asks the model for a corrected reply, or fails the run once the retry
	// budget is spent. Returns true if a retry was sent.
	bool _retry_or_fail(const String &p_prompt_id, const Dictionary &p_values, const String &p_retry_notice, const String &p_failure_notice);
	// The reply has been dealt with: decide what, if anything, follows.
	void _settle();

	// --- Requests ---
	// p_resend: p_message is a request that already went out once (it was
	// rejected and is being sent again), with its attachments in the text.
	bool _send_request(const String &p_message, bool p_resend = false);
	String _get_mode_prompt_id() const;
	void _on_stream_poll();
	void _handle_stream_error(const String &p_error);
	bool _try_overflow_retry(const String &p_error);
	bool _try_retry_without_temperature(const String &p_error);

	// --- Replies (ai_agent_controller_replies.cpp) ---
	// A one-entry dictionary of template values.
	static Dictionary _prompt_values(const String &p_key, const Variant &p_value);
	// p_trusted: the reply is complete and is the model's answer (not cut
	// off, not withheld, not reasoning standing in for an answer). Only a
	// trusted reply has its code executed.
	void _handle_response(const String &p_response, const String &p_thinking, bool p_trusted);
	void _handle_code(const String &p_code);
	void _handle_text_only(const PackedStringArray &p_text_segments);
	void _handle_plan(const PackedStringArray &p_text_segments);
	void _execute(const String &p_code);
	void _on_verify_timeout();
	void _dispatch_side_effects();
	void _emit_response(const PackedStringArray &p_text_segments, const Vector<String> &p_code_blocks, bool p_show_text, bool p_stopped);
	Vector<String> _parse_reply(const String &p_response, PackedStringArray &r_text_segments) const;

	// --- Plan ---
	void _start_plan_step();
	void _mark_plan_step_done();

	// --- Web ---
	bool _start_web_request();
	void _on_web_request_finished(const String &p_content);

	void _compress_if_needed();
	void _reset_conversation();
	void _on_child_notice(int p_level, const String &p_text);
};

VARIANT_ENUM_CAST(AIAgentController::State);
VARIANT_ENUM_CAST(AIAgentController::Mode);
VARIANT_ENUM_CAST(AIAgentController::NoticeLevel);

#endif // TOOLS_ENABLED
