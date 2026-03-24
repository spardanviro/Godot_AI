#ifdef TOOLS_ENABLED

#include "ai_assistant_plugin.h"
#include "ai_assistant_panel.h"

#include "core/input/shortcut.h"

void AIAssistantPlugin::_bind_methods() {
}

AIAssistantPlugin::AIAssistantPlugin() {
	panel = memnew(AIAssistantPanel);
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, panel);
}

AIAssistantPlugin::~AIAssistantPlugin() {
}

#endif // TOOLS_ENABLED
