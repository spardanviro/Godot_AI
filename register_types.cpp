#include "register_types.h"

#include "ui/ai_assistant_plugin.h"
#include "ui/ai_assistant_panel.h"
#include "ui/ai_settings_dialog.h"
#include "ui/ai_mention_highlighter.h"
#include "ui/ai_mention_text_edit.h"
#include "providers/ai_provider.h"
#include "agent/ai_response_parser.h"
#include "execution/ai_script_executor.h"
#include "prompt/ai_context_collector.h"
#include "execution/ai_error_monitor.h"
#include "execution/ai_checkpoint_manager.h"
#include "execution/ai_permission_manager.h"
#include "assets/ai_image_generator.h"
#include "assets/ai_audio_generator.h"
#include "prompt/ai_profiler_collector.h"
#include "network/ai_web_search.h"
#include "agent/ai_agent_controller.h"
#include "assets/ai_asset_service.h"
#include "ui/ai_chat_view.h"
#include "agent/ai_history_summarizer.h"
#include "ui/ai_mention_autocomplete.h"
#include "network/ai_model_list_fetcher.h"
#include "ui/ai_model_selector.h"
#include "ui/ai_details_dialog.h"
#include "ui/ai_history_list.h"
#include "network/ai_web_requester.h"
#include "prompt/ai_knowledge.h"
#include "agent/ai_runtime_watch.h"
#include "providers/openai_compatible_provider.h"
#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/gemini_provider.h"
#include "providers/glm_provider.h"
#include "providers/deepseek_provider.h"

#include "core/object/class_db.h"
#include "editor/plugins/editor_plugin.h"

void initialize_ai_assistant_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_ABSTRACT_CLASS(AIProvider);
		GDREGISTER_ABSTRACT_CLASS(OpenAICompatibleProvider);
		GDREGISTER_CLASS(OpenAIProvider);
		GDREGISTER_CLASS(AnthropicProvider);
		GDREGISTER_CLASS(GeminiProvider);
		GDREGISTER_CLASS(GLMProvider);
		GDREGISTER_CLASS(DeepSeekProvider);
		GDREGISTER_CLASS(AIResponseParser);
		GDREGISTER_CLASS(AIScriptExecutor);
		GDREGISTER_CLASS(AIContextCollector);
		GDREGISTER_CLASS(AIErrorMonitor);
		GDREGISTER_CLASS(AICheckpointManager);
		GDREGISTER_CLASS(AIPermissionManager);
		GDREGISTER_CLASS(AIImageGenerator);
		GDREGISTER_CLASS(AIAudioGenerator);
		GDREGISTER_CLASS(AIProfilerCollector);
		GDREGISTER_CLASS(AIWebSearch);
	}

	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(AIMentionHighlighter);
		GDREGISTER_CLASS(AIMentionTextEdit);
		// Internal helper nodes: not part of the scripting API.
		GDREGISTER_INTERNAL_CLASS(AIAssetService);
		GDREGISTER_INTERNAL_CLASS(AIHistorySummarizer);
		GDREGISTER_INTERNAL_CLASS(AIWebRequester);
		GDREGISTER_INTERNAL_CLASS(AIRuntimeWatch);
		GDREGISTER_INTERNAL_CLASS(AIChatView);
		GDREGISTER_INTERNAL_CLASS(AIMentionAutocomplete);
		GDREGISTER_INTERNAL_CLASS(AIModelListFetcher);
		GDREGISTER_INTERNAL_CLASS(AIModelSelector);
		GDREGISTER_INTERNAL_CLASS(AIDetailsDialog);
		GDREGISTER_INTERNAL_CLASS(AIHistoryList);
		// Exposed so headless scripts (the eval runner) can drive a conversation.
		GDREGISTER_CLASS(AIAgentController);
		GDREGISTER_CLASS(AISettingsDialog);
		GDREGISTER_CLASS(AIAssistantPanel);
		GDREGISTER_CLASS(AIContextMenuPlugin);
		GDREGISTER_CLASS(AIAssistantPlugin);
		EditorPlugins::add_by_type<AIAssistantPlugin>();
	}
}

void uninitialize_ai_assistant_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}
	AIKnowledge::free_singleton();
}
