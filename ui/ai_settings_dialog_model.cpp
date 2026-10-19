#ifdef TOOLS_ENABLED

// AISettingsDialog: showing the provider, its API key and its models. What is
// being edited lives in AIProviderSettings; this file puts it on screen and
// reports what the user does.

#include "ai_settings_dialog.h"

#include "../common/ai_log.h"
#include "../network/ai_model_list_fetcher.h"
#include "ai_model_selector.h"
#include "ai_theme.h"

#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/check_box.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"

static const Color COLOR_STATUS_MUTED(0.65, 0.65, 0.65);
static const Color COLOR_STATUS_OK(0.5, 1.0, 0.5);
static const Color COLOR_STATUS_WARNING(1.0, 0.6, 0.3);

void AISettingsDialog::_show_provider() {
	api_key_edit->set_text(provider_settings.get_typed_key());
	_update_api_key_state();
	endpoint_edit->set_text(provider_settings.get_endpoint());
	endpoint_edit->set_placeholder(provider_settings.get_default_endpoint());

	// The models known from models.dev, until (or unless) the live list arrives.
	model_selector->set_models(provider_settings.get_known_models());
	_show_model_facts();
}

void AISettingsDialog::_on_provider_changed(int p_index) {
	provider_settings.select_provider(p_index);
	_show_provider();
	_fetch_models();
}

void AISettingsDialog::_on_endpoint_changed(const String &p_text) {
	provider_settings.set_endpoint(p_text);
}

// =============================================================================
// API key
// =============================================================================

void AISettingsDialog::_on_api_key_changed(const String &p_text) {
	provider_settings.type_key(p_text);
	_update_api_key_state();
}

// Only what is being typed can be shown; a stored key never enters the field.
void AISettingsDialog::_on_show_key_toggled(bool p_shown) {
	api_key_edit->set_secret(!p_shown);
	show_key_button->set_text(p_shown ? AI_L("Hide", "隐藏") : AI_L("Show", "显示"));
}

void AISettingsDialog::_on_clear_key_pressed() {
	provider_settings.request_key_removal();
	api_key_edit->set_text(String());
	_update_api_key_state();
}

// An empty key field can mean several things; the placeholder says which.
void AISettingsDialog::_update_api_key_state() {
	switch (provider_settings.get_key_state()) {
		case AIProviderSettings::KEY_FROM_ENVIRONMENT:
			api_key_edit->set_placeholder(AI_L("Using environment variable ", "正在使用环境变量 ") + provider_settings.get_key_environment_variable());
			break;
		case AIProviderSettings::KEY_REMOVAL_PENDING:
			api_key_edit->set_placeholder(AI_L("The saved key will be removed when you press OK", "点击“确定”后将删除已保存的密钥"));
			break;
		case AIProviderSettings::KEY_STORED:
			api_key_edit->set_placeholder(AI_L("Saved (leave empty to keep it; type a new key to replace it)", "已保存（留空则保持不变，输入新密钥可替换）"));
			break;
		case AIProviderSettings::KEY_NONE:
			api_key_edit->set_placeholder(TR(STR_SETTINGS_API_KEY_PLACEHOLDER));
			break;
	}
	clear_key_button->set_disabled(!provider_settings.can_remove_key());
}

// =============================================================================
// What the model allows
// =============================================================================

// The provider's words for a thinking setting, with what they mean.
static String _thinking_caption(const String &p_choice) {
	struct Caption {
		const char *choice;
		const char *english;
		const char *chinese;
	};
	static const Caption CAPTIONS[] = {
		{ AIProvider::THINKING_OFF, "Off", "关闭" },
		{ "none", "None", "不思考" },
		{ "minimal", "Minimal", "极少" },
		{ "low", "Low", "低" },
		{ "medium", "Medium", "中" },
		{ "high", "High", "高" },
		{ "xhigh", "Extra high", "很高" },
		{ "max", "Max", "最高" },
	};
	if (AIProvider::is_thinking_budget(p_choice)) {
		return vformat(AI_L("Budget: %s tokens", "预算 %s tokens"), p_choice);
	}
	for (const Caption &caption : CAPTIONS) {
		if (p_choice == caption.choice) {
			return AI_L(caption.english, caption.chinese) + " (" + p_choice + ")";
		}
	}
	return p_choice;
}

void AISettingsDialog::_set_max_tokens(int p_value) {
	// Some models go beyond the spin box's default ceiling.
	max_tokens_spin->set_max(MAX(p_value, (int)max_tokens_spin->get_max()));
	max_tokens_spin->set_value(p_value);
}

void AISettingsDialog::_on_model_picked() {
	_show_model_facts();
}

void AISettingsDialog::_show_model_facts() {
	const Ref<AIProvider> provider = provider_settings.make_model_provider(model_selector->get_model());
	const AIModelSpec spec = provider->get_model_spec();

	if (!custom_max_tokens_check->is_pressed()) {
		_set_max_tokens(provider->get_recommended_max_tokens());
	}
	max_tokens_spin->set_tooltip_text(spec.found
					? vformat(AI_L("This model: up to %d tokens per reply, %d tokens of context.", "该模型：每次回复最多 %d tokens，上下文 %d tokens。"), spec.output, spec.context)
					: AI_L("Nothing is known about this model; the value is a guess.", "没有该模型的资料，这个值是估计的。"));

	const bool takes_temperature = provider->supports_temperature();
	custom_temperature_check->set_disabled(!takes_temperature);
	temperature_spin->set_editable(takes_temperature && custom_temperature_check->is_pressed());
	custom_temperature_check->set_tooltip_text(takes_temperature
					? AI_L("Off: the model's own default is used, which its maker recommends.", "关闭时使用模型自己的默认值，也就是厂商推荐的值。")
					: AI_L("This model does not take a temperature; its default is always used.", "该模型不接受温度参数，始终使用它的默认值。"));

	const PackedStringArray options = provider->get_thinking_options();
	thinking_option->clear();
	thinking_option->add_item(AI_L("Model default", "模型默认"));
	thinking_option->set_item_metadata(0, String());
	for (const String &option : options) {
		thinking_option->add_item(_thinking_caption(option));
		thinking_option->set_item_metadata(thinking_option->get_item_count() - 1, option);
	}
	const int chosen = options.find(thinking_choice);
	thinking_option->select(chosen < 0 ? 0 : chosen + 1);
	thinking_option->set_disabled(options.is_empty());
	thinking_option->set_tooltip_text(options.is_empty()
					? AI_L("This model has no thinking setting to choose.", "该模型没有可选的思考强度。")
					: AI_L("How much the model thinks before it answers. More is slower and spends more tokens; the levels are the model's own.", "模型回答前思考的多少。越高越慢、消耗的 token 越多；档位是该模型自己提供的。"));
}

void AISettingsDialog::_on_thinking_selected(int p_index) {
	thinking_choice = thinking_option->get_item_metadata(p_index);
}

// =============================================================================
// Model list
// =============================================================================

void AISettingsDialog::_set_fetch_status(const String &p_text, const Color &p_color) {
	fetch_status_label->set_text(p_text);
	fetch_status_label->set_tooltip_text(p_text);
	fetch_status_label->add_theme_color_override("font_color", ai_themed(p_color));
}

void AISettingsDialog::_fetch_models() {
	const String api_key = provider_settings.get_usable_key();
	if (api_key.is_empty()) {
		model_list_fetcher->cancel();
		_set_fetch_status(AI_L("Enter an API key to fetch the model list.", "输入 API 密钥后可获取模型列表。"), COLOR_STATUS_MUTED);
		return;
	}
	if (model_list_fetcher->fetch(provider_settings.make_provider(api_key))) {
		_set_fetch_status(TR(STR_FETCH_FETCHING), COLOR_STATUS_MUTED);
	} else {
		_set_fetch_status(TR(STR_FETCH_FAILED), COLOR_STATUS_WARNING);
	}
}

void AISettingsDialog::_on_models_fetched(const PackedStringArray &p_models, const String &p_best) {
	model_selector->update_models(p_models, p_best);
	const String count = itos(p_models.size()) + AI_L(" models", " 个模型");
	if (p_models.has(p_best)) {
		_set_fetch_status(AI_L("Connected. Latest: ", "连接正常。最新：") + p_best + " (" + count + ")", COLOR_STATUS_OK);
	} else {
		// Another service's models: nothing to recommend, pick or type one.
		_set_fetch_status(AI_L("Connected: ", "连接正常：") + count + AI_L(". Pick one from the list or type its name.", "。可从列表选择，或直接填写模型名。"), COLOR_STATUS_OK);
	}

	// The list may have moved the selection to another model.
	_show_model_facts();
}

void AISettingsDialog::_on_models_fetch_failed(const String &p_message) {
	_set_fetch_status(p_message, COLOR_STATUS_WARNING);
}

#endif // TOOLS_ENABLED
