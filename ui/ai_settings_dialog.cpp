#ifdef TOOLS_ENABLED

#include "ai_settings_dialog.h"

#include "../providers/ai_credential_store.h"
#include "../common/ai_log.h"
#include "../network/ai_model_list_fetcher.h"
#include "ai_model_selector.h"
#include "../providers/ai_provider_catalog.h"
#include "../providers/ai_provider_setup.h"
#include "../network/ai_proxy.h"

#include "core/io/dir_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/file_system/editor_paths.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_container.h"

// The value "max tokens" has before the user (or we) ever changed it.
static const int UNSET_MAX_TOKENS = 4096;
static const int DEFAULT_KEEP_RECENT = 6;
static const float DEFAULT_THRESHOLD = 0.70f;
static const char *LANGUAGE_IDS[] = { "en", "zh" };
static const int LANGUAGE_COUNT = 2;
// In the order of the items of proxy_mode_option.
static const char *PROXY_MODES[] = { AIProxy::MODE_SYSTEM, AIProxy::MODE_OFF, AIProxy::MODE_CUSTOM };
static const int PROXY_MODE_COUNT = 3;
static const int PROXY_MODE_CUSTOM_INDEX = 2;
// Wide enough for a row with a caption, a field and two buttons; short
// enough for a small laptop screen. Longer tabs scroll.
static const Size2 DIALOG_SIZE(560, 440);
static const float MAX_SCREEN_RATIO = 0.85f;

static Variant _setting(const char *p_name, const Variant &p_default) {
	return AIProviderSetup::get_editor_setting(String("ai_assistant/") + p_name, p_default);
}

void AISettingsDialog::_bind_methods() {
	ClassDB::bind_method(D_METHOD("refresh"), &AISettingsDialog::refresh);
	ClassDB::bind_method(D_METHOD("open"), &AISettingsDialog::open);
	ADD_SIGNAL(MethodInfo("language_changed"));
}

void AISettingsDialog::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		// Shows the saved values. The model list is only asked for when the
		// dialog is opened (refresh()): starting the editor sends nothing.
		_load_settings();
	}
}

void AISettingsDialog::register_default_settings() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}
	auto ensure = [es](const char *p_name, const Variant &p_value) {
		const String name = String("ai_assistant/") + p_name;
		if (!es->has_setting(name)) {
			es->set_setting(name, p_value);
		}
	};
	ensure("provider", AIProviderCatalog::get_id(0));
	ensure("model", "");
	ensure("api_endpoint", "");
	ensure("max_tokens", UNSET_MAX_TOKENS);
	ensure("temperature", AIProviderSetup::DEFAULT_TEMPERATURE);
	ensure("language", LANGUAGE_IDS[0]);
	// Folder of Markdown files that override the built-in prompts and
	// knowledge (see AIKnowledge). Empty: use <editor config>/ai_assistant/knowledge.
	ensure("knowledge_dir", "");
	ensure("compression_keep_recent", DEFAULT_KEEP_RECENT);
	ensure("compression_threshold_pct", DEFAULT_THRESHOLD);
	ensure("compression_use_ai_summary", false);
	ensure("proxy_mode", AIProxy::MODE_SYSTEM);
	ensure("proxy_address", "");
	// Empty: the generators' built-in defaults.
	ensure("image_model", "");
	ensure("speech_model", "");
	// "custom_temperature" is deliberately not registered: while it is absent,
	// AIProviderSetup derives it from whether the temperature was ever changed.
}

void AISettingsDialog::refresh() {
	_load_settings();
	_fetch_models();
}

void AISettingsDialog::open() {
	if (is_visible()) {
		grab_focus(); // Already open: reloading would discard what is being edited.
		return;
	}
	refresh();
	popup_centered_clamped(DIALOG_SIZE * EDSCALE, MAX_SCREEN_RATIO);
}

// =============================================================================
// Loading and saving
// =============================================================================

void AISettingsDialog::_load_settings() {
	if (!EditorSettings::get_singleton()) {
		return;
	}

	// API keys live in the credential store and are never loaded into the
	// dialog: a stored key put into the LineEdit could be read by any code
	// that can walk the editor's node tree, generated scripts included. The
	// field only ever holds what is typed now; empty means "keep the stored key".
	_forget_unsaved_keys();

	const int provider_index = AIProviderCatalog::find(_setting("provider", ""));
	provider_option->select(provider_index);
	endpoints.clear();
	endpoints[AIProviderCatalog::get_id(provider_index)] = _setting("api_endpoint", "");
	_show_provider(provider_index);

	custom_temperature_check->set_pressed(AIProviderSetup::uses_custom_temperature());
	temperature_spin->set_value(_setting("temperature", AIProviderSetup::DEFAULT_TEMPERATURE));
	_on_custom_temperature_toggled(custom_temperature_check->is_pressed());

	language_at_open = _setting("language", LANGUAGE_IDS[0]);
	language_option->select(language_at_open == LANGUAGE_IDS[1] ? 1 : 0);
	send_on_enter_check->set_pressed(_setting("send_on_enter", true));
	autorun_check->set_pressed(_setting("autorun", true));
	restore_last_chat_check->set_pressed(_setting("restore_last_chat", true));
	runtime_fix_check->set_pressed(_setting("runtime_fix", true));
	compression_keep_recent_spin->set_value(_setting("compression_keep_recent", DEFAULT_KEEP_RECENT));
	// Round, don't truncate: 0.58f * 100 is 57.99..., which would drift down on every save.
	compression_threshold_spin->set_value(Math::round(float(_setting("compression_threshold_pct", DEFAULT_THRESHOLD)) * 100.0f));
	compression_ai_summary_check->set_pressed(_setting("compression_use_ai_summary", false));

	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		const AIPermissionManager::Category category = (AIPermissionManager::Category)i;
		const int level = AIProviderSetup::get_editor_setting(AIPermissionManager::get_setting_key(category), (int)AIPermissionManager::get_default_level(category));
		if (level >= AIPermissionManager::LEVEL_ALLOW && level <= AIPermissionManager::LEVEL_DENY) {
			permission_options[i]->select(level);
		}
	}

	const String proxy_mode = _setting("proxy_mode", AIProxy::MODE_SYSTEM);
	proxy_mode_option->select(0);
	for (int i = 0; i < PROXY_MODE_COUNT; i++) {
		if (proxy_mode == PROXY_MODES[i]) {
			proxy_mode_option->select(i);
		}
	}
	proxy_address_edit->set_text(_setting("proxy_address", ""));
	_on_proxy_changed();
	image_model_edit->set_text(_setting("image_model", ""));
	speech_model_edit->set_text(_setting("speech_model", ""));
	knowledge_dir_edit->set_text(_setting("knowledge_dir", ""));
	if (EditorPaths::get_singleton()) {
		knowledge_dir_edit->set_placeholder(EditorPaths::get_singleton()->get_config_dir().path_join("ai_assistant").path_join("knowledge"));
	}

	// Last, so that what the user saved wins over what _show_provider()
	// suggested for the provider's default model.
	_set_max_tokens(_setting("max_tokens", UNSET_MAX_TOKENS));
	model_selector->select_model(_setting("model", ""));
	max_tokens_edited = false;
}

void AISettingsDialog::_on_confirmed() {
	EditorSettings *es = EditorSettings::get_singleton();
	if (!es) {
		return;
	}

	// API keys go to the credential store, never to the editor settings file.
	// A provider without a typed key keeps the key it has, unless its removal
	// was asked for.
	for (const String &provider_id : keys_to_clear) {
		if (!AICredentialStore::set_stored_api_key(provider_id, String())) {
			AI_ERR("Could not remove the API key for " + provider_id + ".");
		}
	}
	for (const KeyValue<String, String> &typed : typed_api_keys) {
		const String key = typed.value.strip_edges();
		if (!key.is_empty() && !AICredentialStore::set_stored_api_key(typed.key, key)) {
			AI_ERR("Could not store the API key for " + typed.key + ".");
		}
	}
	_forget_unsaved_keys();
	model_list_fetcher->cancel();

	es->set_setting("ai_assistant/provider", AIProviderCatalog::get_id(provider_option->get_selected()));
	es->set_setting("ai_assistant/model", model_selector->get_model());
	es->set_setting("ai_assistant/api_endpoint", endpoint_edit->get_text().strip_edges());
	es->set_setting("ai_assistant/max_tokens", (int)max_tokens_spin->get_value());
	es->set_setting("ai_assistant/custom_temperature", custom_temperature_check->is_pressed());
	es->set_setting("ai_assistant/temperature", (float)temperature_spin->get_value());

	const int language_index = language_option->get_selected();
	if (language_index >= 0 && language_index < LANGUAGE_COUNT) {
		es->set_setting("ai_assistant/language", LANGUAGE_IDS[language_index]);
		language_at_open = LANGUAGE_IDS[language_index];
	}
	es->set_setting("ai_assistant/send_on_enter", send_on_enter_check->is_pressed());
	es->set_setting("ai_assistant/autorun", autorun_check->is_pressed());
	es->set_setting("ai_assistant/restore_last_chat", restore_last_chat_check->is_pressed());
	es->set_setting("ai_assistant/runtime_fix", runtime_fix_check->is_pressed());
	es->set_setting("ai_assistant/compression_keep_recent", (int)compression_keep_recent_spin->get_value());
	es->set_setting("ai_assistant/compression_threshold_pct", (float)compression_threshold_spin->get_value() / 100.0f);
	es->set_setting("ai_assistant/compression_use_ai_summary", compression_ai_summary_check->is_pressed());

	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		es->set_setting(AIPermissionManager::get_setting_key((AIPermissionManager::Category)i), permission_options[i]->get_selected());
	}

	es->set_setting("ai_assistant/proxy_mode", _get_proxy_mode());
	es->set_setting("ai_assistant/proxy_address", proxy_address_edit->get_text().strip_edges());
	es->set_setting("ai_assistant/image_model", image_model_edit->get_text().strip_edges());
	es->set_setting("ai_assistant/speech_model", speech_model_edit->get_text().strip_edges());
	es->set_setting("ai_assistant/knowledge_dir", knowledge_dir_edit->get_text().strip_edges());
	es->save();

	// The panel re-reads everything it renders from the settings.
	emit_signal(SNAME("language_changed"));
}

void AISettingsDialog::_forget_unsaved_keys() {
	typed_api_keys.clear();
	keys_to_clear.clear();
	api_key_edit->set_text(String());
	show_key_button->set_pressed(false);
	_update_api_key_state();
}

void AISettingsDialog::_on_canceled() {
	model_list_fetcher->cancel();
	_forget_unsaved_keys();
	// The language was switched right away so the dialog could be read in it;
	// everything else was never written.
	EditorSettings *es = EditorSettings::get_singleton();
	if (es && String(_setting("language", LANGUAGE_IDS[0])) != language_at_open) {
		es->set_setting("ai_assistant/language", language_at_open);
		_update_ui_texts();
		emit_signal(SNAME("language_changed"));
	}
}

void AISettingsDialog::_on_language_changed(int p_index) {
	// Applied right away, so that the texts below already come out translated.
	// Cancel puts the old language back.
	EditorSettings *es = EditorSettings::get_singleton();
	if (es && p_index >= 0 && p_index < LANGUAGE_COUNT) {
		es->set_setting("ai_assistant/language", LANGUAGE_IDS[p_index]);
	}
	_update_ui_texts();
	emit_signal(SNAME("language_changed"));
}

// =============================================================================
// General, permissions, advanced
// =============================================================================

void AISettingsDialog::_on_custom_temperature_toggled(bool p_enabled) {
	temperature_spin->set_editable(p_enabled);
}

void AISettingsDialog::_set_all_permissions(int p_level) {
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		permission_options[i]->select(p_level);
	}
}

void AISettingsDialog::_reset_permissions() {
	for (int i = 0; i < AIPermissionManager::PERM_MAX; i++) {
		permission_options[i]->select(AIPermissionManager::get_default_level((AIPermissionManager::Category)i));
	}
}

String AISettingsDialog::_get_proxy_mode() const {
	const int index = proxy_mode_option->get_selected();
	return index >= 0 && index < PROXY_MODE_COUNT ? PROXY_MODES[index] : AIProxy::MODE_SYSTEM;
}

void AISettingsDialog::_on_proxy_mode_selected(int /*p_index*/) {
	_on_proxy_changed();
}

void AISettingsDialog::_on_proxy_address_changed(const String & /*p_text*/) {
	_on_proxy_changed();
}

// Says what the choice on screen amounts to, before it is saved.
void AISettingsDialog::_on_proxy_changed() {
	const bool is_custom = proxy_mode_option->get_selected() == PROXY_MODE_CUSTOM_INDEX;
	proxy_address_edit->set_editable(is_custom);

	const String address = proxy_address_edit->get_text().strip_edges();
	const AIProxyConfig proxy = AIProxy::preview(_get_proxy_mode(), address);
	String status;
	if (proxy.is_set()) {
		status = AI_L("In effect: HTTP proxy ", "当前生效：HTTP 代理 ") + proxy.to_text() +
				AI_L(". Local addresses are still reached directly. Through a proxy, page fetches cannot check which address a name resolves to.",
						"。本机和局域网地址仍然直连。经过代理时，网页抓取无法校验域名解析到的地址。");
	} else if (!is_custom && _get_proxy_mode() == AIProxy::MODE_SYSTEM && !AIProxy::get_os_proxy().is_empty()) {
		status = AI_L("The system proxy (", "系统代理（") + AIProxy::get_os_proxy() + AI_L(") is not an HTTP proxy and cannot be used. In effect: direct connection.", "）不是 HTTP 代理，无法使用。当前生效：直连。");
	} else if (is_custom && !address.is_empty()) {
		status = AI_L("Not a usable address. Write host:port; only HTTP proxies are supported (no SOCKS).", "地址不可用。请填写 host:port；仅支持 HTTP 代理（不支持 SOCKS）。");
	} else if (is_custom) {
		status = AI_L("Enter the proxy as host:port.", "请以 host:port 的形式填写代理地址。");
	} else {
		status = AI_L("In effect: direct connection.", "当前生效：直连。");
	}
	proxy_status_label->set_text(status);
	// "Custom" without a usable address would quietly mean "direct"; a user
	// who needs the proxy must not be able to save that by accident.
	get_ok_button()->set_disabled(is_custom && !proxy.is_set());
}

String AISettingsDialog::_get_knowledge_dir() const {
	const String typed = knowledge_dir_edit->get_text().strip_edges();
	return typed.is_empty() ? knowledge_dir_edit->get_placeholder() : typed;
}

void AISettingsDialog::_on_open_knowledge_dir_pressed() {
	const String dir = _get_knowledge_dir();
	if (dir.is_empty()) {
		return;
	}
	// The folder does not exist until the user wants to put something in it.
	if (DirAccess::make_dir_recursive_absolute(dir) != OK) {
		AI_ERR("Could not create the knowledge folder: " + dir);
		return;
	}
	OS::get_singleton()->shell_show_in_file_manager(dir, true);
}

AISettingsDialog::AISettingsDialog() {
	set_min_size(DIALOG_SIZE * EDSCALE);

	tabs = memnew(TabContainer);
	tabs->set_name("Tabs");
	add_child(tabs);
	_build_model_tab(_add_tab("Model"));
	_build_general_tab(_add_tab("General"));
	_build_permissions_tab(_add_tab("Permissions"));
	_build_advanced_tab(_add_tab("Advanced"));

	model_list_fetcher = memnew(AIModelListFetcher);
	model_list_fetcher->connect("fetched", callable_mp(this, &AISettingsDialog::_on_models_fetched));
	model_list_fetcher->connect("failed", callable_mp(this, &AISettingsDialog::_on_models_fetch_failed));
	add_child(model_list_fetcher);

	_update_ui_texts();
	register_default_settings();
	connect("confirmed", callable_mp(this, &AISettingsDialog::_on_confirmed));
	connect("canceled", callable_mp(this, &AISettingsDialog::_on_canceled));
}

#endif // TOOLS_ENABLED
