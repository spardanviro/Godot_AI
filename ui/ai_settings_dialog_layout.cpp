#ifdef TOOLS_ENABLED

// The widgets of AISettingsDialog and the text on them. What the widgets do is
// in ai_settings_dialog.cpp and ai_settings_dialog_model.cpp.

#include "ai_settings_dialog.h"

#include "../assets/ai_audio_generator.h"
#include "../assets/ai_image_generator.h"
#include "../common/ai_log.h"
#include "ai_model_selector.h"
#include "../providers/ai_provider_catalog.h"

#include "core/object/callable_mp.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_container.h"

static const int CAPTION_WIDTH = 130;
static const Color COLOR_SECTION(0.7, 0.7, 0.7);
static const Color COLOR_HINT(0.6, 0.6, 0.65);

enum {
	TAB_MODEL,
	TAB_GENERAL,
	TAB_PERMISSIONS,
	TAB_ADVANCED,
};

static const AILocalization::StringID PERMISSION_CAPTIONS[AIPermissionManager::PERM_MAX] = {
	AILocalization::STR_PERM_CREATE_NODES,
	AILocalization::STR_PERM_DELETE_NODES,
	AILocalization::STR_PERM_MODIFY_PROPERTIES,
	AILocalization::STR_PERM_MODIFY_SCRIPTS,
	AILocalization::STR_PERM_PROJECT_SETTINGS,
	AILocalization::STR_PERM_WRITE_FILES,
	AILocalization::STR_PERM_DELETE_FILES,
};

// Adds a "caption | field" row and returns the caption, whose text is set by
// _update_ui_texts().
static Label *_add_row(VBoxContainer *p_parent, Control *p_field, bool p_expand = true) {
	HBoxContainer *row = memnew(HBoxContainer);
	p_parent->add_child(row);

	Label *caption = memnew(Label);
	caption->set_custom_minimum_size(Size2(CAPTION_WIDTH, 0));
	row->add_child(caption);

	if (p_expand) {
		p_field->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	}
	row->add_child(p_field);
	return caption;
}

static Label *_add_section_title(VBoxContainer *p_parent) {
	Label *title = memnew(Label);
	title->add_theme_color_override("font_color", COLOR_SECTION);
	p_parent->add_child(title);
	return title;
}

static Label *_add_hint(VBoxContainer *p_parent) {
	Label *hint = memnew(Label);
	hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	hint->add_theme_color_override("font_color", COLOR_HINT);
	p_parent->add_child(hint);
	return hint;
}

static SpinBox *_make_spin_box(double p_min, double p_max, double p_step, double p_value) {
	SpinBox *spin_box = memnew(SpinBox);
	spin_box->set_min(p_min);
	spin_box->set_max(p_max);
	spin_box->set_step(p_step);
	spin_box->set_value(p_value);
	return spin_box;
}

static CheckBox *_make_check_box(bool p_pressed) {
	CheckBox *check_box = memnew(CheckBox);
	check_box->set_pressed(p_pressed);
	return check_box;
}

// A page of the dialog. Each scrolls on its own, so the dialog fits on a
// small screen whatever a tab holds.
VBoxContainer *AISettingsDialog::_add_tab(const String &p_name) {
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_name(p_name);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	tabs->add_child(scroll);

	VBoxContainer *page = memnew(VBoxContainer);
	page->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	page->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(page);
	return page;
}

void AISettingsDialog::_build_model_tab(VBoxContainer *p_parent) {
	provider_option = memnew(OptionButton);
	provider_option->set_name("ProviderOption");
	for (int i = 0; i < AIProviderCatalog::get_count(); i++) {
		provider_option->add_item(AIProviderCatalog::get_display_name(i));
	}
	provider_option->connect("item_selected", callable_mp(this, &AISettingsDialog::_on_provider_changed));
	label_provider = _add_row(p_parent, provider_option);

	HBoxContainer *key_row = memnew(HBoxContainer);
	api_key_edit = memnew(LineEdit);
	api_key_edit->set_name("ApiKeyEdit");
	api_key_edit->set_secret(true);
	api_key_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	api_key_edit->connect("text_changed", callable_mp(this, &AISettingsDialog::_on_api_key_changed));
	key_row->add_child(api_key_edit);
	show_key_button = memnew(Button);
	show_key_button->set_name("ShowKeyButton");
	show_key_button->set_toggle_mode(true);
	show_key_button->connect("toggled", callable_mp(this, &AISettingsDialog::_on_show_key_toggled));
	key_row->add_child(show_key_button);
	clear_key_button = memnew(Button);
	clear_key_button->set_name("ClearKeyButton");
	clear_key_button->connect("pressed", callable_mp(this, &AISettingsDialog::_on_clear_key_pressed));
	key_row->add_child(clear_key_button);
	label_api_key = _add_row(p_parent, key_row);

	model_selector = memnew(AIModelSelector);
	model_selector->set_name("ModelSelector");
	model_selector->connect("model_picked", callable_mp(this, &AISettingsDialog::_on_model_picked));
	label_model = _add_row(p_parent, model_selector);

	// What the model list request said, and a way to ask again: the quickest
	// check that key, endpoint and proxy work.
	HBoxContainer *status_row = memnew(HBoxContainer);
	fetch_status_label = memnew(Label);
	fetch_status_label->set_name("FetchStatusLabel");
	fetch_status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	fetch_status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	status_row->add_child(fetch_status_label);
	refresh_models_button = memnew(Button);
	refresh_models_button->set_name("RefreshModelsButton");
	refresh_models_button->connect("pressed", callable_mp(this, &AISettingsDialog::_fetch_models));
	status_row->add_child(refresh_models_button);
	_add_row(p_parent, status_row);

	endpoint_edit = memnew(LineEdit);
	endpoint_edit->set_name("EndpointEdit");
	endpoint_edit->connect("text_changed", callable_mp(this, &AISettingsDialog::_on_endpoint_changed));
	label_endpoint = _add_row(p_parent, endpoint_edit);

	max_tokens_spin = _make_spin_box(256, 65536, 256, 4096);
	max_tokens_spin->set_name("MaxTokensSpin");
	max_tokens_spin->connect("value_changed", callable_mp(this, &AISettingsDialog::_on_max_tokens_changed));
	label_max_tokens = _add_row(p_parent, max_tokens_spin);

	HBoxContainer *temperature_row = memnew(HBoxContainer);
	custom_temperature_check = _make_check_box(false);
	custom_temperature_check->set_name("CustomTemperatureCheck");
	custom_temperature_check->connect("toggled", callable_mp(this, &AISettingsDialog::_on_custom_temperature_toggled));
	temperature_row->add_child(custom_temperature_check);
	temperature_spin = _make_spin_box(0.0, 2.0, 0.1, 0.7);
	temperature_spin->set_name("TemperatureSpin");
	temperature_spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	temperature_row->add_child(temperature_spin);
	label_temperature = _add_row(p_parent, temperature_row);
}

void AISettingsDialog::_build_general_tab(VBoxContainer *p_parent) {
	language_option = memnew(OptionButton);
	language_option->set_name("LanguageOption");
	language_option->add_item("English");
	language_option->add_item(String::utf8("\xe4\xb8\xad\xe6\x96\x87")); // 中文
	language_option->connect("item_selected", callable_mp(this, &AISettingsDialog::_on_language_changed));
	label_language = _add_row(p_parent, language_option);

	send_on_enter_check = _make_check_box(true);
	label_send_on_enter = _add_row(p_parent, send_on_enter_check, false);

	autorun_check = _make_check_box(true);
	label_auto_execute = _add_row(p_parent, autorun_check, false);

	restore_last_chat_check = _make_check_box(true);
	label_restore_last_chat = _add_row(p_parent, restore_last_chat_check, false);

	runtime_fix_check = _make_check_box(true);
	runtime_fix_check->set_name("RuntimeFixCheck");
	label_runtime_fix = _add_row(p_parent, runtime_fix_check, false);

	use_tools_check = _make_check_box(true);
	use_tools_check->set_name("UseToolsCheck");
	label_use_tools = _add_row(p_parent, use_tools_check, false);

	label_compression = _add_section_title(p_parent);

	// Step 2: messages come in user/assistant pairs, so an even count keeps
	// the retained history starting on a user turn.
	compression_keep_recent_spin = _make_spin_box(2, 20, 2, 6);
	label_compression_keep_recent = _add_row(p_parent, compression_keep_recent_spin);

	// Percentage of the context window.
	compression_threshold_spin = _make_spin_box(30, 95, 5, 70);
	label_compression_threshold = _add_row(p_parent, compression_threshold_spin);

	compression_ai_summary_check = _make_check_box(false);
	label_compression_ai_summary = _add_row(p_parent, compression_ai_summary_check, false);
}

void AISettingsDialog::_build_permissions_tab(VBoxContainer *p_parent) {
	permissions_hint_label = _add_hint(p_parent);

	HBoxContainer *presets = memnew(HBoxContainer);
	p_parent->add_child(presets);
	allow_all_button = memnew(Button);
	allow_all_button->set_name("AllowAllButton");
	allow_all_button->connect("pressed", callable_mp(this, &AISettingsDialog::_set_all_permissions).bind((int)AIPermissionManager::LEVEL_ALLOW));
	presets->add_child(allow_all_button);
	ask_all_button = memnew(Button);
	ask_all_button->set_name("AskAllButton");
	ask_all_button->connect("pressed", callable_mp(this, &AISettingsDialog::_set_all_permissions).bind((int)AIPermissionManager::LEVEL_ASK));
	presets->add_child(ask_all_button);
	default_permissions_button = memnew(Button);
	default_permissions_button->set_name("DefaultPermissionsButton");
	default_permissions_button->connect("pressed", callable_mp(this, &AISettingsDialog::_reset_permissions));
	presets->add_child(default_permissions_button);

	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		OptionButton *option = memnew(OptionButton);
		option->set_name("Permission" + itos(i));
		// In the order of AIPermissionManager::Level; captions come later.
		option->add_item(String());
		option->add_item(String());
		option->add_item(String());
		option->select(AIPermissionManager::get_default_level((AIPermissionManager::Category)i));
		permission_options[i] = option;
		permission_labels[i] = _add_row(p_parent, option);
	}
}

void AISettingsDialog::_build_advanced_tab(VBoxContainer *p_parent) {
	label_proxy = _add_section_title(p_parent);
	proxy_mode_option = memnew(OptionButton);
	proxy_mode_option->set_name("ProxyModeOption");
	// In the order of PROXY_MODES in ai_settings_dialog.cpp.
	proxy_mode_option->add_item(String());
	proxy_mode_option->add_item(String());
	proxy_mode_option->add_item(String());
	proxy_mode_option->connect("item_selected", callable_mp(this, &AISettingsDialog::_on_proxy_mode_selected));
	label_proxy_mode = _add_row(p_parent, proxy_mode_option);
	proxy_address_edit = memnew(LineEdit);
	proxy_address_edit->set_name("ProxyAddressEdit");
	proxy_address_edit->set_placeholder("127.0.0.1:7890");
	proxy_address_edit->connect("text_changed", callable_mp(this, &AISettingsDialog::_on_proxy_address_changed));
	label_proxy_address = _add_row(p_parent, proxy_address_edit);
	proxy_status_label = _add_hint(p_parent);
	proxy_status_label->set_name("ProxyStatusLabel");

	label_assets = _add_section_title(p_parent);
	image_model_edit = memnew(LineEdit);
	image_model_edit->set_name("ImageModelEdit");
	image_model_edit->set_placeholder(AIImageGenerator::DEFAULT_MODEL);
	label_image_model = _add_row(p_parent, image_model_edit);
	speech_model_edit = memnew(LineEdit);
	speech_model_edit->set_name("SpeechModelEdit");
	speech_model_edit->set_placeholder(AIAudioGenerator::DEFAULT_MODEL);
	label_speech_model = _add_row(p_parent, speech_model_edit);

	label_knowledge = _add_section_title(p_parent);
	HBoxContainer *knowledge_row = memnew(HBoxContainer);
	knowledge_dir_edit = memnew(LineEdit);
	knowledge_dir_edit->set_name("KnowledgeDirEdit");
	knowledge_dir_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	knowledge_row->add_child(knowledge_dir_edit);
	open_knowledge_dir_button = memnew(Button);
	open_knowledge_dir_button->connect("pressed", callable_mp(this, &AISettingsDialog::_on_open_knowledge_dir_pressed));
	knowledge_row->add_child(open_knowledge_dir_button);
	label_knowledge_dir = _add_row(p_parent, knowledge_row);
}

void AISettingsDialog::_update_ui_texts() {
	set_title(TR(STR_SETTINGS_TITLE));
	tabs->set_tab_title(TAB_MODEL, AI_L("Model", "模型"));
	tabs->set_tab_title(TAB_GENERAL, AI_L("General", "常规"));
	tabs->set_tab_title(TAB_PERMISSIONS, AI_L("Permissions", "权限"));
	tabs->set_tab_title(TAB_ADVANCED, AI_L("Advanced", "高级"));

	// Model.
	label_provider->set_text(TR(STR_SETTINGS_PROVIDER));
	label_api_key->set_text(TR(STR_SETTINGS_API_KEY));
	label_model->set_text(TR(STR_SETTINGS_MODEL));
	model_selector->set_placeholder(AI_L("Any model name, e.g. deepseek-ai/DeepSeek-V3", "可填写任意模型名，例如 deepseek-ai/DeepSeek-V3"));
	model_selector->set_list_tooltip(AI_L("Pick from the models known for this provider. Any other name can be typed in the field.", "从该服务商的已知模型中选择；其他模型名可直接在输入框填写。"));
	label_endpoint->set_text(TR(STR_SETTINGS_ENDPOINT));
	label_max_tokens->set_text(TR(STR_SETTINGS_MAX_TOKENS));
	label_temperature->set_text(TR(STR_SETTINGS_TEMPERATURE));
	show_key_button->set_text(show_key_button->is_pressed() ? AI_L("Hide", "隐藏") : AI_L("Show", "显示"));
	clear_key_button->set_text(AI_L("Remove saved", "清除已存"));
	clear_key_button->set_tooltip_text(AI_L("Remove the key saved for this provider when OK is pressed.", "点击“确定”后，删除为该服务商保存的密钥。"));
	refresh_models_button->set_text(AI_L("Test / refresh", "测试 / 刷新"));
	refresh_models_button->set_tooltip_text(AI_L("Ask the provider for its model list with the key and endpoint above. It answers only if both work.", "用上面的密钥和端点向服务商请求模型列表；两者都正确才会成功。"));
	custom_temperature_check->set_text(AI_L("Custom", "自定义"));
	custom_temperature_check->set_tooltip_text(AI_L("Off: the model's own default is used. Many current reasoning models reject a custom temperature.", "关闭时使用模型自己的默认值。目前不少推理模型不接受自定义温度。"));
	_update_api_key_state();

	// General.
	label_language->set_text(TR(STR_SETTINGS_LANGUAGE));
	label_send_on_enter->set_text(TR(STR_SETTINGS_SEND_ON_ENTER));
	send_on_enter_check->set_text(TR(STR_SETTINGS_SEND_ON_ENTER_DESC));
	label_auto_execute->set_text(TR(STR_SETTINGS_AUTO_EXECUTE));
	autorun_check->set_text(TR(STR_SETTINGS_AUTO_EXECUTE_DESC));
	label_restore_last_chat->set_text(TR(STR_SETTINGS_RESTORE_LAST_CHAT));
	restore_last_chat_check->set_text(TR(STR_SETTINGS_RESTORE_LAST_CHAT_DESC));
	label_runtime_fix->set_text(AI_L("Runtime fix:", "运行时修复："));
	label_use_tools->set_text(AI_L("Tools:", "工具："));
	use_tools_check->set_text(AI_L("Let the model work with tools instead of writing code to run", "让模型用工具操作，而不是写代码去执行"));
	use_tools_check->set_tooltip_text(AI_L("The model inspects and changes the project with typed tools: look at the scene and files, add and change nodes, write scripts, run the scene. Each change can be undone and follows your permission settings. In ASK and PLAN modes only the tools that look are offered. Turned off automatically for a model or endpoint that rejects tool calls; with it off, the model writes code blocks that are run, as before.",
			"模型通过有类型的工具查看和修改项目：查看场景与文件、增改节点、写脚本、运行场景。每次修改都可撤销，并遵循你的权限设置。ASK 与 PLAN 模式下只提供查看类工具。模型或端点不支持工具调用时会自动停用；关闭后，模型像以前一样写代码块来执行。"));
	runtime_fix_check->set_text(AI_L("Fix errors of the running game automatically", "游戏运行报错时自动修复"));
	runtime_fix_check->set_tooltip_text(AI_L("When the running game reports script errors, stop it, ask the model for a fix and run the game again. With auto-execute off, each fix waits for your approval.", "运行中的游戏报出脚本错误时，停止运行、请模型修复并重新运行。关闭“自动执行”时，每次修复都会先等你确认。"));
	label_compression->set_text(AI_L("Context compression", "上下文压缩"));
	label_compression_keep_recent->set_text(TR(STR_SETTINGS_COMPRESSION_KEEP_RECENT));
	label_compression_threshold->set_text(TR(STR_SETTINGS_COMPRESSION_THRESHOLD));
	label_compression_ai_summary->set_text(TR(STR_SETTINGS_COMPRESSION_AI_SUMMARY));
	compression_ai_summary_check->set_text(TR(STR_SETTINGS_COMPRESSION_AI_SUMMARY_DESC));

	// Permissions.
	permissions_hint_label->set_text(AI_L(
			"What generated code may do. \"Ask\" shows the code and waits for your approval.",
			"生成的代码可以做什么。“询问”会先显示代码并等待你确认。"));
	allow_all_button->set_text(AI_L("Allow all", "全部允许"));
	ask_all_button->set_text(AI_L("Ask for all", "全部询问"));
	default_permissions_button->set_text(AI_L("Defaults", "恢复默认"));
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		permission_labels[i]->set_text(AILocalization::get(PERMISSION_CAPTIONS[i]));
		permission_options[i]->set_item_text(AIPermissionManager::LEVEL_ALLOW, TR(STR_PERM_ALLOW));
		permission_options[i]->set_item_text(AIPermissionManager::LEVEL_ASK, TR(STR_PERM_ASK));
		permission_options[i]->set_item_text(AIPermissionManager::LEVEL_DENY, TR(STR_PERM_DENY));
	}

	// Advanced.
	label_proxy->set_text(AI_L("Network proxy", "网络代理"));
	label_proxy_mode->set_text(AI_L("Proxy:", "代理："));
	proxy_mode_option->set_item_text(0, AI_L("Follow the system", "跟随系统"));
	proxy_mode_option->set_item_text(1, AI_L("None (connect directly)", "不使用（直连）"));
	proxy_mode_option->set_item_text(2, AI_L("Custom", "自定义"));
	label_proxy_address->set_text(AI_L("Address:", "地址："));
	_on_proxy_changed();
	label_assets->set_text(AI_L("Image and speech generation (OpenAI)", "图像与语音生成（OpenAI）"));
	label_image_model->set_text(AI_L("Image model:", "图像模型："));
	label_speech_model->set_text(AI_L("Speech model:", "语音模型："));
	label_knowledge->set_text(AI_L("Prompts and knowledge", "提示词与知识库"));
	label_knowledge_dir->set_text(AI_L("Override folder:", "覆盖目录："));
	knowledge_dir_edit->set_tooltip_text(AI_L(
			"Markdown files in this folder replace the built-in prompts and knowledge. Empty: the default folder.",
			"该目录中的 Markdown 文件会替换内置的提示词和知识。留空则使用默认目录。"));
	open_knowledge_dir_button->set_text(AI_L("Open", "打开"));
}

#endif // TOOLS_ENABLED
