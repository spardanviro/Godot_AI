#ifdef TOOLS_ENABLED

#include "ai_settings_dialog.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

#include "providers/openai_provider.h"
#include "providers/anthropic_provider.h"
#include "providers/gemini_provider.h"
#include "providers/glm_provider.h"
#include "providers/deepseek_provider.h"

#include "editor/settings/editor_settings.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/main/http_request.h"

// Label used for the "type your own model name" entry at the bottom of the model dropdown.
static const char *CUSTOM_MODEL_ITEM = "Custom...";

void AISettingsDialog::_bind_methods() {
	ADD_SIGNAL(MethodInfo("language_changed"));
}

void AISettingsDialog::_append_custom_model_item() {
	// Remove any stale "Custom..." first (safe if absent).
	for (int i = model_option->get_item_count() - 1; i >= 0; i--) {
		if (model_option->get_item_text(i) == String(CUSTOM_MODEL_ITEM)) {
			model_option->remove_item(i);
			break;
		}
	}
	model_option->add_item(CUSTOM_MODEL_ITEM);
}

void AISettingsDialog::_select_custom_model(const String &p_name) {
	// Find "Custom..." in the list and select it.
	for (int i = 0; i < model_option->get_item_count(); i++) {
		if (model_option->get_item_text(i) == String(CUSTOM_MODEL_ITEM)) {
			model_option->select(i);
			break;
		}
	}
	custom_model_edit->set_text(p_name);
	custom_model_edit->show();
}

void AISettingsDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		_load_settings();
	}
}

String AISettingsDialog::_get_default_model_for(int p_provider_index) const {
	switch (p_provider_index) {
		case 0: return "claude-opus-4-6";   // Anthropic
		case 1: return "gpt-5.4";           // OpenAI
		case 2: return "gemini-3.1-pro-preview"; // Gemini
		case 3: return "glm-5.1";            // GLM
		case 4: return "deepseek-chat";     // DeepSeek
		default: return "claude-opus-4-6";
	}
}

PackedStringArray AISettingsDialog::_get_fallback_models_for(int p_provider_index) const {
	PackedStringArray models;
	switch (p_provider_index) {
		case 0: // Anthropic — non-deprecated models
			models.push_back("claude-opus-4-6");
			models.push_back("claude-opus-4-5");
			models.push_back("claude-opus-4-1");
			models.push_back("claude-opus-4");
			models.push_back("claude-sonnet-4-6");
			models.push_back("claude-sonnet-4-5");
			models.push_back("claude-sonnet-4");
			models.push_back("claude-haiku-4-5");
			models.push_back("claude-haiku-3-5");
			models.push_back("claude-haiku-3");
			break;
		case 1: // OpenAI / Codex models
			models.push_back("gpt-5.4");
			models.push_back("gpt-5.4-mini");
			models.push_back("gpt-5.4-pro");
			models.push_back("gpt-5.4-nano");
			models.push_back("gpt-5.3-codex");
			models.push_back("gpt-5.2-codex");
			models.push_back("gpt-5.2");
			models.push_back("gpt-5.1-codex-max");
			models.push_back("gpt-5.1-codex");
			models.push_back("gpt-5.1-codex-mini");
			models.push_back("gpt-5.1");
			models.push_back("gpt-5-codex");
			models.push_back("gpt-5-codex-mini");
			models.push_back("gpt-5");
			break;
		case 2: // Google Gemini — latest 3.x series chat models
			models.push_back("gemini-3.1-pro-preview");
			models.push_back("gemini-3-flash-preview");
			models.push_back("gemini-3.1-flash-lite-preview");
			break;
		case 3: // Zhipu AI GLM — GLM-5 series (from docs.bigmodel.cn/cn/guide/start/model-overview)
			models.push_back("glm-5.1");
			models.push_back("glm-5");
			models.push_back("glm-5-turbo");
			break;
		case 4: // DeepSeek
			models.push_back("deepseek-chat");
			models.push_back("deepseek-reasoner");
			break;
		default:
			models.push_back(_get_default_model_for(0));
			break;
	}
	return models;
}

void AISettingsDialog::_load_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	String provider = es->get_setting("ai_assistant/provider");
	int idx = 0;
	if (provider == "anthropic") {
		idx = 0;
	} else if (provider == "openai") {
		idx = 1;
	} else if (provider == "gemini") {
		idx = 2;
	} else if (provider == "glm") {
		idx = 3;
	} else if (provider == "deepseek") {
		idx = 4;
	}
	provider_option->select(idx);
	current_provider_idx = idx;

	// Load per-provider API keys. Fall back to legacy single key for the active provider.
	{
		const char *prov_names[] = { "anthropic", "openai", "gemini", "glm", "deepseek" };
		String legacy_key = es->has_setting("ai_assistant/api_key") ? String(es->get_setting("ai_assistant/api_key")) : String();
		for (int i = 0; i < 5; i++) {
			String setting_key = "ai_assistant/api_key_" + String(prov_names[i]);
			if (es->has_setting(setting_key) && !String(es->get_setting(setting_key)).is_empty()) {
				provider_api_keys[i] = es->get_setting(setting_key);
			} else if (i == idx) {
				// Migrate legacy key to the currently active provider.
				provider_api_keys[i] = legacy_key;
			}
		}
	}
	api_key_edit->set_text(provider_api_keys[idx]);

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

	if (es->has_setting("ai_assistant/restore_last_chat")) {
		restore_last_chat_check->set_pressed(es->get_setting("ai_assistant/restore_last_chat"));
	} else {
		restore_last_chat_check->set_pressed(true);
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

	// _on_provider_changed seeds the dropdown with the provider default and starts a fetch.
	// Restore the saved model so it shows immediately and is preserved when fetch completes.
	_on_provider_changed(idx);
	String saved_model = es->get_setting("ai_assistant/model");
	if (!saved_model.is_empty()) {
		bool found = false;
		for (int i = 0; i < model_option->get_item_count(); i++) {
			String item = model_option->get_item_text(i);
			if (item != String(CUSTOM_MODEL_ITEM) && item == saved_model) {
				model_option->select(i);
				custom_model_edit->hide();
				found = true;
				break;
			}
		}
		if (!found) {
			// Model not in the preset list — activate the custom entry.
			_select_custom_model(saved_model);
		}
	}
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
	if (label_restore_last_chat) label_restore_last_chat->set_text(TR(STR_SETTINGS_RESTORE_LAST_CHAT));
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
	if (restore_last_chat_check) restore_last_chat_check->set_text(TR(STR_SETTINGS_RESTORE_LAST_CHAT_DESC));

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
	// Switch to the API key stored for this provider.
	current_provider_idx = p_index;
	api_key_edit->set_text(provider_api_keys[p_index]);

	switch (p_index) {
		case 0:
			endpoint_edit->set_placeholder("https://api.anthropic.com/v1/messages");
			break;
		case 1:
			endpoint_edit->set_placeholder("https://api.openai.com/v1/chat/completions");
			break;
		case 2:
			endpoint_edit->set_placeholder("https://generativelanguage.googleapis.com/v1beta/models/");
			break;
		case 3:
			endpoint_edit->set_placeholder("https://open.bigmodel.cn/api/coding/paas/v4/chat/completions");
			break;
		case 4:
			endpoint_edit->set_placeholder("https://api.deepseek.com/chat/completions");
			break;
	}

	// Seed dropdown with hardcoded current models immediately (fetch may update it).
	model_option->clear();
	PackedStringArray fallback = _get_fallback_models_for(p_index);
	for (int i = 0; i < fallback.size(); i++) {
		model_option->add_item(fallback[i]);
	}
	_append_custom_model_item();
	model_option->select(0);
	custom_model_edit->hide();
	custom_model_edit->set_text("");
	_on_model_changed(0); // Update max-tokens for the default model.

	_auto_fetch_models();
}

void AISettingsDialog::_on_model_changed(int /*p_index*/) {
	if (!model_option || !max_tokens_spin) {
		return;
	}
	String model_name = model_option->get_item_text(model_option->get_selected()).strip_edges();
	if (model_name.is_empty()) {
		return;
	}

	// Show/hide the custom model entry field.
	bool is_custom = (model_name == String(CUSTOM_MODEL_ITEM));
	if (custom_model_edit) {
		if (is_custom) {
			custom_model_edit->show();
			custom_model_edit->grab_focus();
		} else {
			custom_model_edit->hide();
		}
	}

	// Don't auto-update max-tokens when custom entry is selected — the user
	// hasn't typed a name yet, so we can't query the provider for it.
	if (is_custom) {
		return;
	}

	// Create a temporary provider just to query its recommended max tokens.
	Ref<AIProvider> tmp = _create_provider_for_index(current_provider_idx, "", "");
	if (!tmp.is_valid()) {
		return;
	}
	tmp->set_model(model_name);

	int recommended = tmp->get_recommended_max_tokens();

	// Extend the spin-box ceiling if needed (e.g. Gemini 65536, codex 32768).
	int spin_max = MAX(recommended, (int)max_tokens_spin->get_max());
	max_tokens_spin->set_max(spin_max);
	max_tokens_spin->set_value(recommended);
}

void AISettingsDialog::_on_custom_model_changed(const String & /*p_text*/) {
	// Nothing special — value is read in _on_confirmed.
}

void AISettingsDialog::_on_api_key_changed(const String &p_text) {
	// Keep the per-provider cache in sync as the user types.
	if (current_provider_idx >= 0 && current_provider_idx < 5) {
		provider_api_keys[current_provider_idx] = p_text;
	}
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
			provider = Ref<GeminiProvider>(memnew(GeminiProvider));
			break;
		case 3:
			provider = Ref<GLMProvider>(memnew(GLMProvider));
			break;
		case 4:
			provider = Ref<DeepSeekProvider>(memnew(DeepSeekProvider));
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
	if (current_api_key.is_empty()) {
		fetch_status_label->set_text("Enter API key to fetch models");
		fetch_status_label->add_theme_color_override("font_color", Color(0.6, 0.6, 0.6));
		return;
	}

	// Cancel any in-flight request before starting a new one.
	// Reset is_fetching first so the new request is never blocked.
	if (is_fetching) {
		models_http_request->cancel_request();
		is_fetching = false;
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

	Error err = models_http_request->request(url, headers, HTTPClient::METHOD_GET);
	if (err != OK) {
		fetch_status_label->set_text(TR(STR_FETCH_FAILED));
		fetch_status_label->add_theme_color_override("font_color", Color(1.0, 0.4, 0.4));
	} else {
		is_fetching = true;
	}
}

void AISettingsDialog::_on_models_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	is_fetching = false;

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		String err_msg;
		if (p_result != HTTPRequest::RESULT_SUCCESS) {
			err_msg = "Fetch error (network)";
		} else if (p_response_code == 401) {
			err_msg = "Fetch failed (401 — wrong API key?)";
		} else if (p_response_code == 403) {
			err_msg = "Fetch failed (403 — no model list access)";
		} else {
			err_msg = "Fetch failed (HTTP " + itos(p_response_code) + ")";
		}
		fetch_status_label->set_text(err_msg);
		fetch_status_label->add_theme_color_override("font_color", Color(1.0, 0.5, 0.3));
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

	// Remember what was selected before repopulating (may be a saved model or "Custom...").
	String prev_selected;
	String prev_custom_text;
	if (model_option->get_item_count() > 0) {
		prev_selected = model_option->get_item_text(model_option->get_selected());
		if (prev_selected == String(CUSTOM_MODEL_ITEM) && custom_model_edit) {
			prev_custom_text = custom_model_edit->get_text().strip_edges();
		}
	}

	// Repopulate dropdown with fetched models, then re-append "Custom...".
	model_option->clear();
	for (int i = 0; i < models.size(); i++) {
		model_option->add_item(models[i]);
	}
	_append_custom_model_item();

	// Try to restore previously selected model.
	bool found = false;
	if (!prev_selected.is_empty() && prev_selected != String(CUSTOM_MODEL_ITEM)) {
		for (int i = 0; i < model_option->get_item_count(); i++) {
			if (model_option->get_item_text(i) == prev_selected) {
				model_option->select(i);
				found = true;
				break;
			}
		}
	}
	// Fall back to best model if previous selection not in list.
	if (!found && !best.is_empty()) {
		for (int i = 0; i < model_option->get_item_count(); i++) {
			if (model_option->get_item_text(i) == best) {
				model_option->select(i);
				found = true;
				break;
			}
		}
	}
	if (!found && model_option->get_item_count() > 0) {
		model_option->select(0);
	}

	// Restore custom text if the user had typed something.
	if (!prev_custom_text.is_empty() && custom_model_edit) {
		custom_model_edit->set_text(prev_custom_text);
	}

	fetch_status_label->set_text("Latest: " + best + " (" + itos(models.size()) + " models)");
	fetch_status_label->add_theme_color_override("font_color", Color(0.5, 1.0, 0.5));

	// Sync max-tokens to the newly selected model.
	_on_model_changed(model_option->get_selected());
}

void AISettingsDialog::_on_confirmed() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	String providers[] = { "anthropic", "openai", "gemini", "glm", "deepseek" };
	int idx = provider_option->get_selected();
	if (idx >= 0 && idx < 5) {
		es->set_setting("ai_assistant/provider", providers[idx]);
	}

	// Save per-provider API keys.
	{
		const char *prov_names[] = { "anthropic", "openai", "gemini", "glm", "deepseek" };
		for (int i = 0; i < 5; i++) {
			es->set_setting("ai_assistant/api_key_" + String(prov_names[i]), provider_api_keys[i]);
		}
		// Also keep legacy single-key for backward compat (saves the active provider's key).
		if (idx >= 0 && idx < 5) {
			es->set_setting("ai_assistant/api_key", provider_api_keys[idx]);
		}
	}
	String selected_model;
	if (model_option->get_item_count() > 0) {
		String item_text = model_option->get_item_text(model_option->get_selected()).strip_edges();
		if (item_text == String(CUSTOM_MODEL_ITEM)) {
			// Use whatever the user typed in the custom field.
			selected_model = custom_model_edit ? custom_model_edit->get_text().strip_edges() : String();
		} else {
			selected_model = item_text;
		}
	}
	es->set_setting("ai_assistant/model", selected_model);
	es->set_setting("ai_assistant/api_endpoint", endpoint_edit->get_text());
	es->set_setting("ai_assistant/max_tokens", (int)max_tokens_spin->get_value());
	es->set_setting("ai_assistant/temperature", (float)temperature_spin->get_value());
	es->set_setting("ai_assistant/send_on_enter", send_on_enter_check->is_pressed());
	es->set_setting("ai_assistant/autorun", autorun_check->is_pressed());
	es->set_setting("ai_assistant/restore_last_chat", restore_last_chat_check->is_pressed());

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
		provider_option->add_item("Google (Gemini)");
		provider_option->add_item(String::utf8("智谱 (GLM)"));
		provider_option->add_item("DeepSeek");
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
		api_key_edit->connect("text_changed", callable_mp(this, &AISettingsDialog::_on_api_key_changed));
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

		model_option = memnew(OptionButton);
		model_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		model_option->add_item(_get_default_model_for(0)); // Anthropic default until fetch runs.
		model_option->add_item(CUSTOM_MODEL_ITEM);
		model_option->select(0);
		model_option->connect("item_selected", callable_mp(this, &AISettingsDialog::_on_model_changed));
		hbox->add_child(model_option);
	}

	// Custom model name input — only visible when "Custom..." is selected.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);

		// Spacer to align with the dropdown above.
		Label *spacer = memnew(Label);
		spacer->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(spacer);

		custom_model_edit = memnew(LineEdit);
		custom_model_edit->set_placeholder("Enter model name, e.g. gpt-4o-mini");
		custom_model_edit->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		custom_model_edit->connect("text_changed", callable_mp(this, &AISettingsDialog::_on_custom_model_changed));
		custom_model_edit->hide(); // Hidden until "Custom..." is selected.
		hbox->add_child(custom_model_edit);
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
		max_tokens_spin->set_max(65536);
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

	// Restore last chat.
	{
		HBoxContainer *hbox = memnew(HBoxContainer);
		vbox->add_child(hbox);
		label_restore_last_chat = memnew(Label);
		label_restore_last_chat->set_text(TR(STR_SETTINGS_RESTORE_LAST_CHAT));
		label_restore_last_chat->set_custom_minimum_size(Size2(120, 0));
		hbox->add_child(label_restore_last_chat);

		restore_last_chat_check = memnew(CheckBox);
		restore_last_chat_check->set_text(TR(STR_SETTINGS_RESTORE_LAST_CHAT_DESC));
		restore_last_chat_check->set_pressed(true);
		hbox->add_child(restore_last_chat_check);
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
		// Per-provider API keys.
		if (!es->has_setting("ai_assistant/api_key_anthropic")) {
			es->set_setting("ai_assistant/api_key_anthropic", "");
		}
		if (!es->has_setting("ai_assistant/api_key_openai")) {
			es->set_setting("ai_assistant/api_key_openai", "");
		}
		if (!es->has_setting("ai_assistant/api_key_gemini")) {
			es->set_setting("ai_assistant/api_key_gemini", "");
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
