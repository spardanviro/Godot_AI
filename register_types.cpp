#include "register_types.h"

#include "ai_assistant_plugin.h"
#include "ai_assistant_panel.h"
#include "ai_settings_dialog.h"
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
#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/gemini_provider.h"

#include "core/object/class_db.h"
#include "editor/plugins/editor_plugin.h"

void initialize_ai_assistant_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_ABSTRACT_CLASS(AIProvider);
		GDREGISTER_CLASS(OpenAIProvider);
		GDREGISTER_CLASS(AnthropicProvider);
		GDREGISTER_CLASS(GeminiProvider);
		GDREGISTER_CLASS(AIResponseParser);
		GDREGISTER_CLASS(AIScriptExecutor);
		GDREGISTER_CLASS(AIContextCollector);
		GDREGISTER_CLASS(AIErrorMonitor);
		GDREGISTER_CLASS(AICheckpointManager);
		GDREGISTER_CLASS(AIPermissionManager);
		GDREGISTER_CLASS(AIImageGenerator);
		GDREGISTER_CLASS(AIAudioGenerator);
		GDREGISTER_CLASS(AIProfilerCollector);
		GDREGISTER_CLASS(AIUIAgent);
		GDREGISTER_CLASS(AIWebSearch);
	}

	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(AISettingsDialog);
		GDREGISTER_CLASS(AIAssistantPanel);
		GDREGISTER_CLASS(AIAssistantPlugin);
		EditorPlugins::add_by_type<AIAssistantPlugin>();
	}
}

void uninitialize_ai_assistant_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_EDITOR) {
		return;
	}
}
