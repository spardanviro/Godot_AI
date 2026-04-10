#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "editor/settings/editor_settings.h"

// Simple localization for Godot AI assistant UI.
// Supports: "en" (English), "zh" (Chinese).

class AILocalization {
public:
	enum StringID {
		// Panel toolbar.
		STR_PANEL_TITLE,
		STR_BTN_NEW,
		STR_BTN_HISTORY,
		STR_BTN_BACK,
		STR_BTN_SETTINGS,
		STR_BTN_SEND,

		// Mode selector.
		STR_MODE_ASK,
		STR_MODE_AGENT,
		STR_MODE_PLAN,
		STR_MODE_TOOLTIP,

		// Tooltips.
		STR_TIP_NEW_CHAT,
		STR_TIP_HISTORY,
		STR_TIP_SETTINGS,
		STR_TIP_ATTACH,

		// Input field.
		STR_INPUT_PLACEHOLDER_ENTER,
		STR_INPUT_PLACEHOLDER_CTRL,

		// System messages.
		STR_SYS_READY,
		STR_SYS_NEW_CONVERSATION,
		STR_SYS_NO_SELECTION,
		STR_SYS_NO_NODES_SELECTED,
		STR_SYS_NO_PROVIDER,
		STR_SYS_NO_API_KEY,
		STR_SYS_EXECUTING_CODE,
		STR_SYS_EXEC_CANCELLED,
		STR_SYS_EXEC_FAILED,
		STR_SYS_AUTO_RETRY,
		STR_SYS_AUTO_RECOVER_FAILED,
		STR_SYS_CHAT_DELETED,
		STR_SYS_NO_HISTORY,

		// Permission dialog.
		STR_PERM_DIALOG_TITLE,
		STR_PERM_PROCEED,
		STR_PERM_EXECUTE_CONFIRM,

		// Auto-retry for missing code block.
		STR_NO_CODE_BLOCK_RETRY,

		// Preset buttons.
		STR_PRESET_DESCRIBE,
		STR_PRESET_ADD_PLAYER,
		STR_PRESET_ADD_LIGHT,
		STR_PRESET_ADD_UI,
		STR_PRESET_FIX_ERRORS,
		STR_PRESET_PERFORMANCE,

		// Settings dialog.
		STR_SETTINGS_TITLE,
		STR_SETTINGS_PROVIDER,
		STR_SETTINGS_API_KEY,
		STR_SETTINGS_API_KEY_PLACEHOLDER,
		STR_SETTINGS_MODEL,
		STR_SETTINGS_ENDPOINT,
		STR_SETTINGS_ENDPOINT_PLACEHOLDER,
		STR_SETTINGS_MAX_TOKENS,
		STR_SETTINGS_TEMPERATURE,
		STR_SETTINGS_SEND_ON_ENTER,
		STR_SETTINGS_SEND_ON_ENTER_DESC,
		STR_SETTINGS_AUTO_EXECUTE,
		STR_SETTINGS_AUTO_EXECUTE_DESC,
		STR_SETTINGS_RESTORE_LAST_CHAT,
		STR_SETTINGS_RESTORE_LAST_CHAT_DESC,
		STR_SETTINGS_LANGUAGE,

		// Permission settings.
		STR_SETTINGS_PERMISSIONS,
		STR_PERM_CREATE_NODES,
		STR_PERM_DELETE_NODES,
		STR_PERM_MODIFY_PROPERTIES,
		STR_PERM_MODIFY_SCRIPTS,
		STR_PERM_PROJECT_SETTINGS,
		STR_PERM_WRITE_FILES,
		STR_PERM_DELETE_FILES,
		STR_PERM_ALLOW,
		STR_PERM_ASK,
		STR_PERM_DENY,

		// Fetch status.
		STR_FETCH_FETCHING,
		STR_FETCH_FAILED,
		STR_FETCH_DEFAULT,

		// Web search.
		STR_WEB_SEARCHING,
		STR_WEB_FETCHING,
		STR_WEB_FAILED,
		STR_WEB_NO_CONTENT,
		STR_WEB_RESULTS_RECEIVED,

		// Asset generation.
		STR_ASSET_API_KEY_REQUIRED,
		STR_ASSET_GENERATING_IMAGE,
		STR_ASSET_GENERATING_AUDIO,

		// Thinking display.
		STR_THINKING_LABEL,
		STR_THINKING_COLLAPSED,

		// Runtime error auto-fix.
		STR_BTN_WATCH,
		STR_TIP_WATCH,
		STR_WATCH_ENABLED,
		STR_WATCH_DISABLED,
		STR_WATCH_DETECTING,
		STR_WATCH_FIXING,
		STR_WATCH_RESTARTING,
		STR_WATCH_SUCCESS,
		STR_WATCH_FAILED,

		// Screenshot.
		STR_BTN_SCREENSHOT,
		STR_TIP_SCREENSHOT,
		STR_SCREENSHOT_SAVED,
		STR_SCREENSHOT_FAILED,

		STR_MAX
	};

	static String get(StringID p_id) {
		String lang = _get_language();
		if (lang == "zh") {
			return _get_zh(p_id);
		}
		return _get_en(p_id);
	}

	static String get_language() {
		return _get_language();
	}

private:
	static String _get_language() {
		EditorSettings *es = EditorSettings::get_singleton();
		if (es && es->has_setting("ai_assistant/language")) {
			return es->get_setting("ai_assistant/language");
		}
		return "en";
	}

	static String _get_en(StringID p_id) {
		switch (p_id) {
			// Panel toolbar.
			case STR_PANEL_TITLE: return "Godot AI";
			case STR_BTN_NEW: return "New";
			case STR_BTN_HISTORY: return "History";
			case STR_BTN_BACK: return "Back";
			case STR_BTN_SETTINGS: return "Settings";
			case STR_BTN_SEND: return "Send";

			// Mode selector.
			case STR_MODE_ASK: return "ASK";
			case STR_MODE_AGENT: return "AGENT";
			case STR_MODE_PLAN: return "PLAN";
			case STR_MODE_TOOLTIP: return "AI Mode: ASK (text only), AGENT (auto-execute), PLAN (plan then execute)";

			// Tooltips.
			case STR_TIP_NEW_CHAT: return "Start a new conversation";
			case STR_TIP_HISTORY: return "View conversation history";
			case STR_TIP_SETTINGS: return "AI Settings";
			case STR_TIP_ATTACH: return "Attach selected nodes to context";

			// Input field.
			case STR_INPUT_PLACEHOLDER_ENTER: return "Type your message... (Enter to send, Shift+Enter for newline)";
			case STR_INPUT_PLACEHOLDER_CTRL: return "Type your message... (Ctrl+Enter to send)";

			// System messages.
			case STR_SYS_READY: return "Godot AI ready. Configure your API key in Settings to get started.";
			case STR_SYS_NEW_CONVERSATION: return "New conversation started.";
			case STR_SYS_NO_SELECTION: return "No selection available.";
			case STR_SYS_NO_NODES_SELECTED: return "No nodes selected. Select nodes in the scene tree first.";
			case STR_SYS_NO_PROVIDER: return "Error: No AI provider configured.";
			case STR_SYS_NO_API_KEY: return "Error: API key not set. Click Settings to configure.";
			case STR_SYS_EXECUTING_CODE: return "Executing code...";
			case STR_SYS_EXEC_CANCELLED: return "Code execution cancelled by user.";
			case STR_SYS_EXEC_FAILED: return "Execution failed: ";
			case STR_SYS_AUTO_RETRY: return "Auto-retrying...";
			case STR_SYS_AUTO_RECOVER_FAILED: return "Auto-recovery failed after max attempts.";
			case STR_SYS_CHAT_DELETED: return "Chat deleted. New conversation started.";
			case STR_SYS_NO_HISTORY: return "No chat history yet.";

			// Permission dialog.
			case STR_PERM_DIALOG_TITLE: return "AI Code Execution";
			case STR_PERM_PROCEED: return "\n\nProceed?";
			case STR_PERM_EXECUTE_CONFIRM: return "Execute the generated code?\n\nAutorun is disabled. Click OK to proceed.";
			case STR_NO_CODE_BLOCK_RETRY: return "No code block found — requesting code from AI";

			// Preset buttons.
			case STR_PRESET_DESCRIBE: return "Describe Scene";
			case STR_PRESET_ADD_PLAYER: return "Add Player";
			case STR_PRESET_ADD_LIGHT: return "Add Light";
			case STR_PRESET_ADD_UI: return "Add UI";
			case STR_PRESET_FIX_ERRORS: return "Fix Errors";
			case STR_PRESET_PERFORMANCE: return "Performance";

			// Settings dialog.
			case STR_SETTINGS_TITLE: return "Godot AI Settings";
			case STR_SETTINGS_PROVIDER: return "Provider:";
			case STR_SETTINGS_API_KEY: return "API Key:";
			case STR_SETTINGS_API_KEY_PLACEHOLDER: return "Enter your API key...";
			case STR_SETTINGS_MODEL: return "Model:";
			case STR_SETTINGS_ENDPOINT: return "API Endpoint:";
			case STR_SETTINGS_ENDPOINT_PLACEHOLDER: return "Leave empty for default";
			case STR_SETTINGS_MAX_TOKENS: return "Max Tokens:";
			case STR_SETTINGS_TEMPERATURE: return "Temperature:";
			case STR_SETTINGS_SEND_ON_ENTER: return "Send on Enter:";
			case STR_SETTINGS_SEND_ON_ENTER_DESC: return "Enter to send (Shift+Enter for newline)";
			case STR_SETTINGS_AUTO_EXECUTE: return "Auto Execute:";
			case STR_SETTINGS_AUTO_EXECUTE_DESC: return "Automatically execute AI-generated code";
			case STR_SETTINGS_RESTORE_LAST_CHAT: return "Restore Chat:";
			case STR_SETTINGS_RESTORE_LAST_CHAT_DESC: return "Restore last conversation on editor startup";
			case STR_SETTINGS_LANGUAGE: return "Language:";

			// Permission settings.
			case STR_SETTINGS_PERMISSIONS: return "--- Permissions ---";
			case STR_PERM_CREATE_NODES: return "Create Nodes:";
			case STR_PERM_DELETE_NODES: return "Delete Nodes:";
			case STR_PERM_MODIFY_PROPERTIES: return "Modify Properties:";
			case STR_PERM_MODIFY_SCRIPTS: return "Modify Scripts:";
			case STR_PERM_PROJECT_SETTINGS: return "Project Settings:";
			case STR_PERM_WRITE_FILES: return "Write Files:";
			case STR_PERM_DELETE_FILES: return "Delete Files:";
			case STR_PERM_ALLOW: return "Allow";
			case STR_PERM_ASK: return "Ask";
			case STR_PERM_DENY: return "Deny";

			// Fetch status.
			case STR_FETCH_FETCHING: return "Fetching latest models...";
			case STR_FETCH_FAILED: return "Failed to fetch models.";
			case STR_FETCH_DEFAULT: return "Using default model.";

			// Web search.
			case STR_WEB_SEARCHING: return "Searching: ";
			case STR_WEB_FETCHING: return "Fetching: ";
			case STR_WEB_FAILED: return "Web request failed. The AI will respond without web data.";
			case STR_WEB_NO_CONTENT: return "No useful content found.";
			case STR_WEB_RESULTS_RECEIVED: return "Web results received. Analyzing...";

			// Asset generation.
			case STR_ASSET_API_KEY_REQUIRED: return "API key required for asset generation.";
			case STR_ASSET_GENERATING_IMAGE: return "Generating image: ";
			case STR_ASSET_GENERATING_AUDIO: return "Generating audio: ";

			// Thinking display.
			case STR_THINKING_LABEL: return "Thinking";
			case STR_THINKING_COLLAPSED: return "Thinking process (click to view)";

			// Runtime error auto-fix.
			case STR_BTN_WATCH: return "Watch";
			case STR_TIP_WATCH: return "Auto-fix runtime errors while playing the scene";
			case STR_WATCH_ENABLED: return "Runtime watch enabled — play your scene to auto-fix errors.";
			case STR_WATCH_DISABLED: return "Runtime watch disabled.";
			case STR_WATCH_DETECTING: return "Runtime error detected — collecting errors...";
			case STR_WATCH_FIXING: return "Auto-fixing runtime errors...";
			case STR_WATCH_RESTARTING: return "Fix applied — restarting scene...";
			case STR_WATCH_SUCCESS: return "Runtime errors fixed! Scene ran without errors.";
			case STR_WATCH_FAILED: return "Could not auto-fix runtime errors after max attempts. Please fix manually.";

			// Screenshot.
			case STR_BTN_SCREENSHOT: return "Screenshot";
			case STR_TIP_SCREENSHOT: return "Capture editor viewport screenshot for AI context";
			case STR_SCREENSHOT_SAVED: return "Screenshot captured and attached to next message.";
			case STR_SCREENSHOT_FAILED: return "Failed to capture screenshot.";

			default: return "";
		}
	}

	static String _get_zh(StringID p_id) {
		switch (p_id) {
			// Panel toolbar.
			case STR_PANEL_TITLE: return "Godot AI";
			case STR_BTN_NEW: return String::utf8("新建");
			case STR_BTN_HISTORY: return String::utf8("历史");
			case STR_BTN_BACK: return String::utf8("返回");
			case STR_BTN_SETTINGS: return String::utf8("设置");
			case STR_BTN_SEND: return String::utf8("发送");

			// Mode selector.
			case STR_MODE_ASK: return String::utf8("问答");
			case STR_MODE_AGENT: return String::utf8("代理");
			case STR_MODE_PLAN: return String::utf8("规划");
			case STR_MODE_TOOLTIP: return String::utf8("AI 模式：问答（仅文字）、代理（自动执行）、规划（先规划再执行）");

			// Tooltips.
			case STR_TIP_NEW_CHAT: return String::utf8("开始新对话");
			case STR_TIP_HISTORY: return String::utf8("查看对话历史");
			case STR_TIP_SETTINGS: return String::utf8("AI 设置");
			case STR_TIP_ATTACH: return String::utf8("附加选中的节点到上下文");

			// Input field.
			case STR_INPUT_PLACEHOLDER_ENTER: return String::utf8("输入消息... （Enter 发送，Shift+Enter 换行）");
			case STR_INPUT_PLACEHOLDER_CTRL: return String::utf8("输入消息... （Ctrl+Enter 发送）");

			// System messages.
			case STR_SYS_READY: return String::utf8("Godot AI 已就绪。请在设置中配置 API Key 开始使用。");
			case STR_SYS_NEW_CONVERSATION: return String::utf8("已开始新对话。");
			case STR_SYS_NO_SELECTION: return String::utf8("没有可用的选择。");
			case STR_SYS_NO_NODES_SELECTED: return String::utf8("未选中节点。请先在场景树中选择节点。");
			case STR_SYS_NO_PROVIDER: return String::utf8("错误：未配置 AI 提供商。");
			case STR_SYS_NO_API_KEY: return String::utf8("错误：未设置 API Key。点击设置进行配置。");
			case STR_SYS_EXECUTING_CODE: return String::utf8("正在执行代码...");
			case STR_SYS_EXEC_CANCELLED: return String::utf8("代码执行已被用户取消。");
			case STR_SYS_EXEC_FAILED: return String::utf8("执行失败：");
			case STR_SYS_AUTO_RETRY: return String::utf8("正在自动重试...");
			case STR_SYS_AUTO_RECOVER_FAILED: return String::utf8("自动恢复在达到最大尝试次数后失败。");
			case STR_SYS_CHAT_DELETED: return String::utf8("对话已删除。已开始新对话。");
			case STR_SYS_NO_HISTORY: return String::utf8("暂无对话历史。");

			// Permission dialog.
			case STR_PERM_DIALOG_TITLE: return String::utf8("AI 代码执行");
			case STR_PERM_PROCEED: return String::utf8("\n\n是否继续？");
			case STR_PERM_EXECUTE_CONFIRM: return String::utf8("是否执行生成的代码？\n\n自动执行已禁用，点击确定继续。");
			case STR_NO_CODE_BLOCK_RETRY: return String::utf8("响应中未找到代码块 — 正在向 AI 请求代码");

			// Preset buttons.
			case STR_PRESET_DESCRIBE: return String::utf8("描述场景");
			case STR_PRESET_ADD_PLAYER: return String::utf8("添加玩家");
			case STR_PRESET_ADD_LIGHT: return String::utf8("添加灯光");
			case STR_PRESET_ADD_UI: return String::utf8("添加 UI");
			case STR_PRESET_FIX_ERRORS: return String::utf8("修复错误");
			case STR_PRESET_PERFORMANCE: return String::utf8("性能分析");

			// Settings dialog.
			case STR_SETTINGS_TITLE: return String::utf8("Godot AI 设置");
			case STR_SETTINGS_PROVIDER: return String::utf8("提供商：");
			case STR_SETTINGS_API_KEY: return String::utf8("API 密钥：");
			case STR_SETTINGS_API_KEY_PLACEHOLDER: return String::utf8("请输入 API 密钥...");
			case STR_SETTINGS_MODEL: return String::utf8("模型：");
			case STR_SETTINGS_ENDPOINT: return String::utf8("API 端点：");
			case STR_SETTINGS_ENDPOINT_PLACEHOLDER: return String::utf8("留空使用默认值");
			case STR_SETTINGS_MAX_TOKENS: return String::utf8("最大 Token 数：");
			case STR_SETTINGS_TEMPERATURE: return String::utf8("温度：");
			case STR_SETTINGS_SEND_ON_ENTER: return String::utf8("回车发送：");
			case STR_SETTINGS_SEND_ON_ENTER_DESC: return String::utf8("Enter 发送（Shift+Enter 换行）");
			case STR_SETTINGS_AUTO_EXECUTE: return String::utf8("自动执行：");
			case STR_SETTINGS_AUTO_EXECUTE_DESC: return String::utf8("自动执行 AI 生成的代码");
			case STR_SETTINGS_RESTORE_LAST_CHAT: return String::utf8("恢复对话：");
			case STR_SETTINGS_RESTORE_LAST_CHAT_DESC: return String::utf8("启动编辑器时自动恢复上次对话记录");
			case STR_SETTINGS_LANGUAGE: return String::utf8("语言：");

			// Permission settings.
			case STR_SETTINGS_PERMISSIONS: return String::utf8("--- 权限设置 ---");
			case STR_PERM_CREATE_NODES: return String::utf8("创建节点：");
			case STR_PERM_DELETE_NODES: return String::utf8("删除节点：");
			case STR_PERM_MODIFY_PROPERTIES: return String::utf8("修改属性：");
			case STR_PERM_MODIFY_SCRIPTS: return String::utf8("修改脚本：");
			case STR_PERM_PROJECT_SETTINGS: return String::utf8("项目设置：");
			case STR_PERM_WRITE_FILES: return String::utf8("写入文件：");
			case STR_PERM_DELETE_FILES: return String::utf8("删除文件：");
			case STR_PERM_ALLOW: return String::utf8("允许");
			case STR_PERM_ASK: return String::utf8("询问");
			case STR_PERM_DENY: return String::utf8("拒绝");

			// Fetch status.
			case STR_FETCH_FETCHING: return String::utf8("正在获取最新模型...");
			case STR_FETCH_FAILED: return String::utf8("获取模型失败。");
			case STR_FETCH_DEFAULT: return String::utf8("使用默认模型。");

			// Web search.
			case STR_WEB_SEARCHING: return String::utf8("正在搜索：");
			case STR_WEB_FETCHING: return String::utf8("正在获取：");
			case STR_WEB_FAILED: return String::utf8("网络请求失败。AI 将在无网络数据的情况下回复。");
			case STR_WEB_NO_CONTENT: return String::utf8("未找到有用内容。");
			case STR_WEB_RESULTS_RECEIVED: return String::utf8("已收到网络结果，正在分析...");

			// Asset generation.
			case STR_ASSET_API_KEY_REQUIRED: return String::utf8("资源生成需要 API 密钥。");
			case STR_ASSET_GENERATING_IMAGE: return String::utf8("正在生成图片：");
			case STR_ASSET_GENERATING_AUDIO: return String::utf8("正在生成音频：");

			// Thinking display.
			case STR_THINKING_LABEL: return String::utf8("思考中");
			case STR_THINKING_COLLAPSED: return String::utf8("思考过程（点击查看）");

			// Runtime error auto-fix.
			case STR_BTN_WATCH: return String::utf8("监控");
			case STR_TIP_WATCH: return String::utf8("运行场景时自动检测并修复运行时错误");
			case STR_WATCH_ENABLED: return String::utf8("运行时监控已开启 — 运行场景后将自动修复错误。");
			case STR_WATCH_DISABLED: return String::utf8("运行时监控已关闭。");
			case STR_WATCH_DETECTING: return String::utf8("检测到运行时错误 — 正在收集错误信息...");
			case STR_WATCH_FIXING: return String::utf8("正在自动修复运行时错误...");
			case STR_WATCH_RESTARTING: return String::utf8("修复已应用 — 正在重新运行场景...");
			case STR_WATCH_SUCCESS: return String::utf8("运行时错误已修复！场景运行正常。");
			case STR_WATCH_FAILED: return String::utf8("已达最大尝试次数，无法自动修复运行时错误，请手动修复。");

			// Screenshot.
			case STR_BTN_SCREENSHOT: return String::utf8("截图");
			case STR_TIP_SCREENSHOT: return String::utf8("截取编辑器视口画面作为 AI 上下文");
			case STR_SCREENSHOT_SAVED: return String::utf8("截图已捕获，将附加到下次消息中。");
			case STR_SCREENSHOT_FAILED: return String::utf8("截图失败。");

			default: return _get_en(p_id);
		}
	}
};

// Convenience macro.
#define TR(id) AILocalization::get(AILocalization::id)

#endif // TOOLS_ENABLED
