#ifdef TOOLS_ENABLED

#include "ai_settings_dialog.h"

#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/gemini_provider.h"

#include "editor/settings/editor_settings.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/main/http_request.h"

void AISettingsDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("language_changed"));
}

void AISettingsDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		_load_settings();
	}
}

String AISettingsDialog::_get_default_model_for(int p_provider_index) const {
	switch (p_provider_index) {
		case 0: return "claude-opus-4-6";          // Anthropic
		case 1: return "gpt-5.4";                  // OpenAI
		case 2: return "deepseek-reasoner";         // DeepSeek
		case 3: return "gemini-2.5-pro";            // Gemini
		default: return "claude-opus-4-6";
	}
}

bool AISettingsDialog::_is_known_default(const String &p_model) const {
	// Returns true if the model looks like a default that was auto-selected,
	// so we can safely replace it when provider changes or a new best model is fetched.
	// User custom models (not matching these patterns) won't be overwritten.
	return p_model.is_empty() ||
			p_model.begins_with("claude-") ||
			p_model.begins_with("gpt-") ||
			p_model.begins_with("deepseek-") ||
			p_model.begins_with("gemini-");
}

void AISettingsDialog::_load_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	String provider = es->get_setting("ai_assistant/provider");
	if (provider == "anthropic") {
		provider_option->select(0);
	} else if (provider == "openai") {
		provider_option->select(1);
	} else if (provider == "deepseek") {
		provider_option->select(2);
	} else if (provider == "gemini") {
		provider_option->select(3);
	}

	api_key_edit->set_text(es->get_setting("ai_assistant/api_key"));

	String saved_model = es->get_setting("ai_assistant/model");
	int idx = provider_option->get_selected();
	if (saved_model.is_empty()) {
		model_edit->set_text(_get_default_model_for(idx));
	} else {
		model_edit->set_text(saved_model);
	}

	endpoint_edit->set_text(es->get_setting("ai_assistant/api_endpoint"));
	max_tokens_spin->set_value(es->get_setting("ai_assistant/max_tokens"));
	temperature_spin->set_value(es->get_setting("ai_assistant/temperature"));

	if (es->has_setting("ai_assistant/send_on_enter")) {
		send_on_enter_check->set_pressed(es->get_setting("ai_assistant/send_on_enter"));
	} else {
		send_on_enter_check->set_pressed(true);
	}

	if (es->has_setting("ai_assistant/autorun")) {
		autorun_check->set_pressed(es->get_setting("ai_assistant/autorun"));
	} else {
		autorun_check->set_pressed(true);
	}

	// Load language.
	if (es->has_setting("ai_assistant/language")) {
		String lang = es->get_setting("ai_assistant/language");
		if (lang == "zh") {
			language_option->select(1);
		} else {
			language_option->select(0);
		}
	}

	// N6: Load permission settings.
	{
		const char *perm_suffixes[] = {
			"create_nodes", "delete_nodes", "modify_properties",
			"modify_scripts", "project_settings", "file_write", "file_delete"
		};
		for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
			String key = "ai_assistant/permissions/" + String(perm_suffixes[i]);
			if (es->has_setting(key)) {
				int val = es->get_setting(key);
				if (val >= 0 && val <= 2) {
					perm_options[i]->select(val);
				}
			}
		}
	}

	_on_provider_changed(idx);
}

void AISettingsDialog::refresh() {
	_load_settings();
	_auto_fetch_models();
}

void AISettingsDialog::_on_language_changed(int p_index) {
	// Immediately save language so TR() picks it up.
	EditorSettings *es = EditorSettings::get_singleton();
	if (es) {
		String langs[] = { "en", "zh" };
		if (p_index >= 0 && p_index < 2) {
			es->set_setting("ai_assistant/language", langs[p_index]);
		}
	}
	_update_ui_texts();
	emit_signal("language_changed");
}

void AISettingsDialog::_update_ui_texts() {
	set_title(TR(STR_SETTINGS_TITLE));

	if (label_language) label_language->set_text(TR(STR_SETTINGS_LANGUAGE));
	if (label_provider) label_provider->set_text(TR(STR_SETTINGS_PROVIDER));
	if (label_api_key) label_api_key->set_text(TR(STR_SETTINGS_API_KEY));
	if (label_model) label_model->set_text(TR(STR_SETTINGS_MODEL));
	if (label_endpoint) label_endpoint->set_text(TR(STR_SETTINGS_ENDPOINT));
	if (label_max_tokens) label_max_tokens->set_text(TR(STR_SETTINGS_MAX_TOKENS));
	if (label_temperature) label_temperature->set_text(TR(STR_SETTINGS_TEMPERATURE));
	if (label_send_on_enter) label_send_on_enter->set_text(TR(STR_SETTINGS_SEND_ON_ENTER));
	if (label_auto_execute) label_auto_execute->set_text(TR(STR_SETTINGS_AUTO_EXECUTE));
	if (label_permissions) label_permissions->set_text(TR(STR_SETTINGS_PERMISSIONS));

	if (api_key_edit) api_key_edit->set_placeholder(TR(STR_SETTINGS_API_KEY_PLACEHOLDER));
	if (endpoint_edit) {
		// Keep provider-specific placeholder, but update default text.
		int idx = provider_option ? provider_option->get_selected() : 0;
		if (endpoint_edit->get_text().is_empty()) {
			_on_provider_changed(idx);
		}
	}
	if (send_on_enter_check) send_on_enter_check->set_text(TR(STR_SETTINGS_SEND_ON_ENTER_DESC));
	if (autorun_check) autorun_check->set_text(TR(STR_SETTINGS_AUTO_EXECUTE_DESC));

	// Update permission labels and option items.
	AILocalization::StringID perm_str_ids[] = {
		AILocalization::STR_PERM_CREATE_NODES,
		AILocalization::STR_PERM_DELETE_NODES,
		AILocalization::STR_PERM_MODIFY_PROPERTIES,
		AILocalization::STR_PERM_MODIFY_SCRIPTS,
		AILocalization::STR_PERM_PROJECT_SETTINGS,
		AILocalization::STR_PERM_WRITE_FILES,
		AILocalization::STR_PERM_DELETE_FILES,
	};
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		if (perm_labels[i]) {
			perm_labels[i]->set_text(AILocalization::get(perm_str_ids[i]));
		}
		if (perm_options[i]) {
			int sel = perm_options[i]->get_selected();
			perm_options[i]->set_item_text(0, TR(STR_PERM_ALLOW));
			perm_options[i]->set_item_text(1, TR(STR_PERM_ASK));
			perm_options[i]->set_item_text(2, TR(STR_PERM_DENY));
			perm_options[i]->select(sel);
		}
	}
}

void AISettingsDialog::_on_provider_changed(int p_index) {
	switch (p_index) {
		case 0:
			endpoint_edit->set_placeholder("https://api.anthropic.com/v1/messages");
			break;
		case 1:
			endpoint_edit->set_placeholder("https://api.openai.com/v1/chat/completions");
			break;
		case 2:
			endpoint_edit->set_placeholder("https://api.deepseek.com/v1/chat/completions");
			break;
		case 3:
			endpoint_edit->set_placeholder("https://generativelanguage.googleapis.com/v1beta/models/");
			break;
	}

	// Auto-switch model when provider changes (only if current model is a known default).
	String current_model = model_edit->get_text().strip_edges();
	if (_is_known_default(current_model)) {
		model_edit->set_text(_get_default_model_for(p_index));
	}

	// Auto-fetch to get the real best model.
	_auto_fetch_models();
}

Ref<AIProvider> AISettingsDialog::_create_provider_for_index(int p_index, const String &p_api_key, const String &p_endpoint) const {
	Ref<AIProvider> provider;

	switch (p_index) {
		case 0:
			provider = Ref<AnthropicProvider>(memnew(AnthropicProvider));
			break;
		case 1:
			provider = Ref<OpenAIProvider>(memnew(OpenAIProvider));
			break;
		case 2:
			provider = Ref<OpenAIProvider>(memnew(OpenAIProvider));
			if (p_endpoint.is_empty()) {
				provider->set_api_endpoint("https://api.deepseek.com/v1/chat/completions");
			}
			break;
		case 3:
			provider = Ref<GeminiProvider>(memnew(GeminiProvider));
			break;
		default:
			provider = Ref<AnthropicProvider>(memnew(AnthropicProvider));
			break;
	}

	provider->set_api_key(p_api_key);
	if (!p_endpoint.is_empty()) {
		provider->set_api_endpoint(p_endpoint);
	}

	return provider;
}

void AISettingsDialog::_auto_fetch_models() {
	String current_api_key = api_key_edit->get_text().strip_edges();
	if (current_api_key.is_empty() || is_fetching) {
		return;
	}

	int provider_idx = provider_option->get_selected();
	String current_endpoint = endpoint_edit->get_text().strip_edges();

	fetch_provider = _create_provider_for_index(provider_idx, current_api_key, current_endpoint);

	String url = fetch_provider->get_models_list_url();
	if (url.is_empty()) {
		return;
	}

	Vector<String> headers = fetch_provider->get_models_list_headers();

	fetch_status_label->set_text(TR(STR_FETCH_FETCHING));
	fetch_status_label->add_theme_color_override("font_color", Color(0.7, 0.7, 0.7));
	is_fetching = true;

	models_http_request->cancel_request();
	Error err = models_http_request->request(url, headers, HTTPClient::METHOD_GET);
	if (err != OK) {
		fetch_status_label->set_text(TR(STR_FETCH_FAILED));
		fetch_status_label->add_theme_color_override("font_color", Color(1.0, 0.4, 0.4));
		is_fetching = false;
	}
}

void AISettingsDialog::_on_models_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	is_fetching = false;

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		fetch_status_label->set_text(TR(STR_FETCH_DEFAULT));
		fetch_status_label->add_theme_color_override("font_color", Color(1.0, 0.7, 0.3));
		return;
	}

	String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());

	if (!fetch_provider.is_valid()) {
		return;
	}

	PackedStringArray models = fetch_provider->parse_models_list(body_text);
	if (models.is_empty()) {
		fetch_status_label->set_text(TR(STR_FETCH_DEFAULT));
		fetch_status_label->add_theme_color_override("font_color", Color(1.0, 0.7, 0.3));
		return;
	}

	// Let the provider pick the strongest stable model.
	String best = fetch_provider->select_best_model(models);

	if (!best.is_empty()) {
		// Only auto-update if user hasn't set a custom model.
		String current = model_edit->get_text().strip_edges();
		if (_is_known_default(current)) {
			model_edit->set_text(best);
		}

		fetch_status_label->set_text("Latest: " + best + " (from " + itos(models.size()) + " models)");
		fetch_status_label->add_theme_color_override("font_color", Color(0.5, 1.0, 0.5));
	} else {
		fetch_status_label->set_text(TR(STR_FETCH_DEFAULT));
		fetch_status_label->add_theme_color_override("font_color", Color(0.7, 0.7, 0.7));
	}
}

void AISettingsDialog::_on_confirmed() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	String providers[] = { "anthropic", "openai", "deepseek", "gemini" };
	int idx = provider_option->get_selected();
	if (idx >= 0 && idx < 4) {
		es->set_setting("ai_assistant/provider", providers[idx]);
	}

	es->set_setting("ai_assistant/api_key", api_key_edit->get_text());
	es->set_setting("ai_assistant/model", model_edit->get_text().strip_edges());
	es->set_setting("ai_assistant/api_endpoint", endpoint_edit->get_text());
	es->set_setting("ai_assistant/max_tokens", (int)max_tokens_spin->get_value());
	es->set_setting("ai_assistant/temperature", (float)temperature_spin->get_value());
	es->set_setting("ai_assistant/send_on_enter", send_on_enter_check->is_pressed());
	es->set_setting("ai_assistant/autorun", autorun_check->is_pressed());

	// Save language.
	{
		String langs[] = { "en", "zh" };
		int lang_idx = language_option->get_selected();
		if (lang_idx >= 0 && lang_idx < 2) {
			es->set_setting("ai_assistant/language", langs[lang_idx]);
		}
	}

	// N6: Save permission settings.
	{
		const char *perm_keys[] = {
			"ai_assistant/permissions/create_nodes",
			"ai_assistant/permissions/delete_nodes",
			"ai_assistant/permissions/modify_properties",
			"ai_assistant/permissions/modify_scripts",
			"ai_assistant/permissions/project_settings",
			"ai_assistant/permissions/file_write",
			"ai_assistant/permissions/file_delete",
		};
		for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
			es->set_setting(perm_keys[i], perm_options[i]->get_selected());
		}
	}

	es->save();

	// Always notify panel to refresh UI texts when settings confirmed.
	emit_signal("language_changed");
}

AISettingsDialog::AISettingsDialog() {
	set_title(TR(STR_SETTINGS_TITLE));
	set_min_size(Size2(480, 600));

	VBoxContainer *vbox = memnew(VBoxContainer);
	add_child(vbox);

	// Language.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_language = memnew(Label);
		label_language->set_text(TR(STR_SETTINGS_LANGUAGE));
		label_language->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_language);

		language_option = memnew(OptionButton);
		language_option->add_item("English");
		language_option->add_item(String::utf8("\xe4\xb8\xad\xe6\x96\x87")); // 中文
		language_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		language_option->connect("item_selected", callable_mp(this, &AISettingsDialog::_on_language_changed));
		hbox->add_child(language_option);
	}

	// Provider.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_provider = memnew(Label);
		label_provider->set_text(TR(STR_SETTINGS_PROVIDER));
		label_provider->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_provider);

		provider_option = memnew(OptionButton);
		provider_option->add_item("Anthropic (Claude)");
		provider_option->add_item("OpenAI (ChatGPT)");
		provider_option->add_item("DeepSeek");
		provider_option->add_item("Google (Gemini)");
		provider_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		provider_option->connect("item_selected", callable_mp(this, &AISettingsDialog::_on_provider_changed));
		hbox->add_child(provider_option);
	}

	// API Key.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_api_key = memnew(Label);
		label_api_key->set_text(TR(STR_SETTINGS_API_KEY));
		label_api_key->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_api_key);

		api_key_edit = memnew(LineEdit);
		api_key_edit->set_secret(true);
		api_key_edit->set_placeholder(TR(STR_SETTINGS_API_KEY_PLACEHOLDER));
		api_key_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		hbox->add_child(api_key_edit);
	}

	// Model.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_model = memnew(Label);
		label_model->set_text(TR(STR_SETTINGS_MODEL));
		label_model->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_model);

		model_edit = memnew(LineEdit);
		model_edit->set_text("claude-opus-4-6");
		model_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		hbox->add_child(model_edit);
	}

	// Fetch status.
	{
		fetch_status_label = memnew(Label);
		fetch_status_label->set_text("");
		fetch_status_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		vbox->add_child(fetch_status_label);
	}

	// Endpoint.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_endpoint = memnew(Label);
		label_endpoint->set_text(TR(STR_SETTINGS_ENDPOINT));
		label_endpoint->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_endpoint);

		endpoint_edit = memnew(LineEdit);
		endpoint_edit->set_placeholder(TR(STR_SETTINGS_ENDPOINT_PLACEHOLDER));
		endpoint_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		hbox->add_child(endpoint_edit);
	}

	// Max Tokens.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_max_tokens = memnew(Label);
		label_max_tokens->set_text(TR(STR_SETTINGS_MAX_TOKENS));
		label_max_tokens->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_max_tokens);

		max_tokens_spin = memnew(SpinBox);
		max_tokens_spin->set_min(256);
		max_tokens_spin->set_max(32768);
		max_tokens_spin->set_step(256);
		max_tokens_spin->set_value(4096);
		max_tokens_spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		hbox->add_child(max_tokens_spin);
	}

	// Temperature.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_temperature = memnew(Label);
		label_temperature->set_text(TR(STR_SETTINGS_TEMPERATURE));
		label_temperature->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_temperature);

		temperature_spin = memnew(SpinBox);
		temperature_spin->set_min(0.0);
		temperature_spin->set_max(2.0);
		temperature_spin->set_step(0.1);
		temperature_spin->set_value(0.7);
		temperature_spin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		hbox->add_child(temperature_spin);
	}

	// Send on Enter.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_send_on_enter = memnew(Label);
		label_send_on_enter->set_text(TR(STR_SETTINGS_SEND_ON_ENTER));
		label_send_on_enter->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_send_on_enter);

		send_on_enter_check = memnew(CheckBox);
		send_on_enter_check->set_text(TR(STR_SETTINGS_SEND_ON_ENTER_DESC));
		send_on_enter_check->set_pressed(true);
		hbox->add_child(send_on_enter_check);
	}

	// Autorun.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_auto_execute = memnew(Label);
		label_auto_execute->set_text(TR(STR_SETTINGS_AUTO_EXECUTE));
		label_auto_execute->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_auto_execute);

		autorun_check = memnew(CheckBox);
		autorun_check->set_text(TR(STR_SETTINGS_AUTO_EXECUTE_DESC));
		autorun_check->set_pressed(true);
		hbox->add_child(autorun_check);
	}

	// N6: Permission settings section.
	{
		label_permissions = memnew(Label);
		label_permissions->set_text(TR(STR_SETTINGS_PERMISSIONS));
		label_permissions->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		label_permissions->add_theme_color_override("font_color", Color(0.7, 0.7, 0.7));
		vbox->add_child(label_permissions);

		AILocalization::StringID perm_str_ids[] = {
			AILocalization::STR_PERM_CREATE_NODES,
			AILocalization::STR_PERM_DELETE_NODES,
			AILocalization::STR_PERM_MODIFY_PROPERTIES,
			AILocalization::STR_PERM_MODIFY_SCRIPTS,
			AILocalization::STR_PERM_PROJECT_SETTINGS,
			AILocalization::STR_PERM_WRITE_FILES,
			AILocalization::STR_PERM_DELETE_FILES,
		};

		// Default permission levels: Delete Files defaults to Deny for safety.
		const int perm_defaults[] = { 0, 0, 0, 0, 0, 0, 2 };

		for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
			HBoxContainer *hbox = memnew(HBoxContainer);
			vbox->add_child(hbox);
			perm_labels[i] = memnew(Label);
			perm_labels[i]->set_text(AILocalization::get(perm_str_ids[i]));
			perm_labels[i]->set_custom_minimum_size(Size2(120, 0));
			hbox->add_child(perm_labels[i]);

			perm_options[i] = memnew(OptionButton);
			perm_options[i]->add_item(TR(STR_PERM_ALLOW));
			perm_options[i]->add_item(TR(STR_PERM_ASK));
			perm_options[i]->add_item(TR(STR_PERM_DENY));
			perm_options[i]->select(perm_defaults[i]);
			perm_options[i]->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			hbox->add_child(perm_options[i]);
		}
	}

	// HTTP Request for auto-fetching models.
	models_http_request = memnew(HTTPRequest);
	models_http_request->set_use_threads(true);
	models_http_request->set_timeout(15.0);
	models_http_request->connect("request_completed", callable_mp(this, &AISettingsDialog::_on_models_request_completed));
	add_child(models_http_request);

	// Register default settings.
	EditorSettings *es = EditorSettings::get_singleton();
	if (es) {
		if (!es->has_setting("ai_assistant/provider")) {
			es->set_setting("ai_assistant/provider", "anthropic");
		}
		if (!es->has_setting("ai_assistant/api_key")) {
			es->set_setting("ai_assistant/api_key", "");
		}
		if (!es->has_setting("ai_assistant/model")) {
			es->set_setting("ai_assistant/model", "");
		}
		if (!es->has_setting("ai_assistant/api_endpoint")) {
			es->set_setting("ai_assistant/api_endpoint", "");
		}
		if (!es->has_setting("ai_assistant/max_tokens")) {
			es->set_setting("ai_assistant/max_tokens", 4096);
		}
		if (!es->has_setting("ai_assistant/temperature")) {
			es->set_setting("ai_assistant/temperature", 0.7);
		}
		if (!es->has_setting("ai_assistant/language")) {
			es->set_setting("ai_assistant/language", "en");
		}
	}

	connect("confirmed", callable_mp(this, &AISettingsDialog::_on_confirmed));
}

#endif // TOOLS_ENABLED
