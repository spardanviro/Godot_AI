#include "ai_settings.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#endif

namespace {

bool overridden = false;
Dictionary overrides;

String full_name(const String &p_name) {
	return p_name.begins_with(AISettings::PREFIX) ? p_name : String(AISettings::PREFIX) + p_name;
}

} // namespace

void AISettings::set_overrides_for_tests(const Dictionary &p_values) {
	overrides.clear();
	for (const Variant *key = p_values.next(nullptr); key; key = p_values.next(key)) {
		overrides[full_name(*key)] = p_values[*key];
	}
	overridden = true;
}

void AISettings::clear_overrides_for_tests() {
	overrides.clear();
	overridden = false;
}

Dictionary AISettings::get_overrides_for_tests() {
	return overrides.duplicate();
}

bool AISettings::is_available() {
	if (overridden) {
		return true;
	}
#ifdef TOOLS_ENABLED
	return EditorSettings::get_singleton() != nullptr;
#else
	return false;
#endif
}

bool AISettings::has(const String &p_name) {
	if (overridden) {
		return overrides.has(full_name(p_name));
	}
#ifdef TOOLS_ENABLED
	EditorSettings *es = EditorSettings::get_singleton();
	return es && es->has_setting(full_name(p_name));
#else
	return false;
#endif
}

Variant AISettings::get(const String &p_name, const Variant &p_default) {
	if (overridden) {
		return overrides.get(full_name(p_name), p_default);
	}
	return get_editor(full_name(p_name), p_default);
}

void AISettings::set(const String &p_name, const Variant &p_value) {
	if (overridden) {
		overrides[full_name(p_name)] = p_value;
		return;
	}
#ifdef TOOLS_ENABLED
	if (EditorSettings *es = EditorSettings::get_singleton()) {
		es->set_setting(full_name(p_name), p_value);
	}
#endif
}

void AISettings::save() {
#ifdef TOOLS_ENABLED
	if (!overridden && EditorSettings::get_singleton()) {
		EditorSettings::save();
	}
#endif
}

Variant AISettings::get_editor(const String &p_full_name, const Variant &p_default) {
	if (overridden) {
		return overrides.get(p_full_name, p_default);
	}
#ifdef TOOLS_ENABLED
	EditorSettings *es = EditorSettings::get_singleton();
	if (es && es->has_setting(p_full_name)) {
		return es->get_setting(p_full_name);
	}
#endif
	return p_default;
}
