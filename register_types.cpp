#include "register_types.h"

#include "ai_assistant_plugin.h"
#include "ai_assistant_panel.h"
#include "ai_settings_dialog.h"
#include "ai_mention_highlighter.h"
#include "ai_mention_text_edit.h"
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
#include "ai_web_search.h"
#include "ai_agent_controller.h"
#include "ai_asset_service.h"
#include "ai_chat_view.h"
#include "ai_history_summarizer.h"
#include "ai_mention_autocomplete.h"
#include "ai_model_list_fetcher.h"
#include "ai_model_selector.h"
#include "ai_web_requester.h"
#include "ai_knowledge.h"
#include "ai_runtime_watch.h"
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
