#pragma once

#include "core/string/ustring.h"
#include "core/variant/variant.h"

// Keeps provider API keys out of editor_settings.tres, which is a plain-text
// file that gets copied between machines, attached to bug reports and synced.
//
// Lookup order for a provider's key:
//   1. Environment variable GODOT_AI_API_KEY_<PROVIDER>, then the vendor's own
//      conventional variable (OPENAI_API_KEY, ANTHROPIC_API_KEY, ...).
//   2. The credentials file in the editor config directory. On Windows each
//      key is sealed with DPAPI, so only the same Windows user can read it.
//      On other platforms it is AES-encrypted with a key derived from the
//      machine id — that stops casual reading and accidental sharing, but it
//      is obfuscation, not a keychain; prefer environment variables there.
class AICredentialStore {
public:
	static String get_api_key(const String &p_provider_id);
	// Name of the environment variable that currently supplies the key, or "".
	static String get_environment_source(const String &p_provider_id);
	// Key from the credentials file only, ignoring the environment.
	static String get_stored_api_key(const String &p_provider_id);
	// Stores (or, for an empty key, removes) the key. Returns false on failure.
	static bool set_stored_api_key(const String &p_provider_id, const String &p_key);

	// Moves any keys still sitting in EditorSettings into the store and blanks
	// the settings. Returns how many keys were moved.
	static int migrate_from_editor_settings();

	// Seals / unseals a secret for storage on this machine.
	static PackedByteArray protect(const String &p_secret);
	static String unprotect(const PackedByteArray &p_blob, bool *r_ok = nullptr);

	static String get_storage_path();
	// Tests point the store at a scratch file; "" restores the default.
	static void set_storage_path_override(const String &p_path);

private:
	static String storage_path_override;

	// r_unreadable: the file exists but could not be read back; it must not
	// be overwritten, or the keys of every other provider would be lost.
	static Dictionary _load(bool *r_unreadable = nullptr);
	static bool _save(const Dictionary &p_data);
};

// Implemented per platform; return false when OS-level sealing is unavailable.
bool ai_credential_os_protect(const PackedByteArray &p_plain, PackedByteArray &r_sealed);
bool ai_credential_os_unprotect(const PackedByteArray &p_sealed, PackedByteArray &r_plain);
