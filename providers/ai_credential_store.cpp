#include "ai_credential_store.h"
#include "../common/ai_settings.h"

#include "ai_provider.h"

#include "core/crypto/crypto.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"

#ifdef TOOLS_ENABLED
#include "editor/file_system/editor_paths.h"
#endif

String AICredentialStore::storage_path_override;

// Blob layout: one format byte, then the payload.
static const uint8_t FORMAT_OS_SEALED = 'D';
static const uint8_t FORMAT_OBFUSCATED = 'O';
static const int AES_BLOCK = 16;

static void _derive_machine_key(uint8_t r_key[32]) {
	const CharString seed = (OS::get_singleton()->get_unique_id() + "|godot-ai-credentials-v1").utf8();
	CryptoCore::sha256((const uint8_t *)seed.get_data(), seed.length(), r_key);
}

static PackedByteArray _obfuscate(const PackedByteArray &p_plain) {
	uint8_t key[32];
	_derive_machine_key(key);

	PackedByteArray iv;
	Ref<Crypto> crypto = Ref<Crypto>(Crypto::create());
	if (crypto.is_valid()) {
		iv = crypto->generate_random_bytes(AES_BLOCK);
	}
	if (iv.size() != AES_BLOCK) {
		return PackedByteArray(); // No randomness source: refuse rather than reuse an IV.
	}

	// PKCS#7 padding.
	const int pad = AES_BLOCK - (p_plain.size() % AES_BLOCK);
	PackedByteArray padded = p_plain;
	for (int i = 0; i < pad; i++) {
		padded.push_back((uint8_t)pad);
	}

	PackedByteArray cipher;
	cipher.resize(padded.size());
	CryptoCore::AESContext aes;
	aes.set_encode_key(key, 256);
	uint8_t iv_work[AES_BLOCK];
	memcpy(iv_work, iv.ptr(), AES_BLOCK);
	if (aes.encrypt_cbc(padded.size(), iv_work, padded.ptr(), cipher.ptrw()) != OK) {
		return PackedByteArray();
	}

	PackedByteArray out = iv;
	out.append_array(cipher);
	return out;
}

static bool _deobfuscate(const PackedByteArray &p_blob, PackedByteArray &r_plain) {
	if (p_blob.size() < AES_BLOCK * 2 || (p_blob.size() % AES_BLOCK) != 0) {
		return false;
	}
	uint8_t key[32];
	_derive_machine_key(key);

	uint8_t iv_work[AES_BLOCK];
	memcpy(iv_work, p_blob.ptr(), AES_BLOCK);
	const int cipher_len = p_blob.size() - AES_BLOCK;
	PackedByteArray padded;
	padded.resize(cipher_len);
	CryptoCore::AESContext aes;
	aes.set_decode_key(key, 256);
	if (aes.decrypt_cbc(cipher_len, iv_work, p_blob.ptr() + AES_BLOCK, padded.ptrw()) != OK) {
		return false;
	}
	const int pad = padded[cipher_len - 1];
	if (pad < 1 || pad > AES_BLOCK || pad > cipher_len) {
		return false;
	}
	for (int i = cipher_len - pad; i < cipher_len; i++) {
		if (padded[i] != pad) {
			return false;
		}
	}
	r_plain = padded.slice(0, cipher_len - pad);
	return true;
}

PackedByteArray AICredentialStore::protect(const String &p_secret) {
	const CharString utf8 = p_secret.utf8();
	PackedByteArray plain;
	plain.resize(utf8.length());
	if (utf8.length() > 0) {
		memcpy(plain.ptrw(), utf8.get_data(), utf8.length());
	}

	PackedByteArray out;
	PackedByteArray sealed;
	if (ai_credential_os_protect(plain, sealed)) {
		out.push_back(FORMAT_OS_SEALED);
		out.append_array(sealed);
		return out;
	}
#ifdef WINDOWS_ENABLED
	// Never fall back to the weaker format where the OS facility should exist:
	// better to fail visibly than to store a key any local process can recover.
	return PackedByteArray();
#else
	sealed = _obfuscate(plain);
	if (sealed.is_empty()) {
		return PackedByteArray();
	}
	out.push_back(FORMAT_OBFUSCATED);
	out.append_array(sealed);
	return out;
#endif
}

String AICredentialStore::unprotect(const PackedByteArray &p_blob, bool *r_ok) {
	if (r_ok) {
		*r_ok = false;
	}
	if (p_blob.size() < 2) {
		return String();
	}
	const PackedByteArray payload = p_blob.slice(1);
	PackedByteArray plain;
	bool ok = false;
	if (p_blob[0] == FORMAT_OS_SEALED) {
		ok = ai_credential_os_unprotect(payload, plain);
	} else if (p_blob[0] == FORMAT_OBFUSCATED) {
		ok = _deobfuscate(payload, plain);
	}
	if (!ok) {
		return String();
	}
	if (r_ok) {
		*r_ok = true;
	}
	return String::utf8((const char *)plain.ptr(), plain.size());
}

String AICredentialStore::get_storage_path() {
	if (!storage_path_override.is_empty()) {
		return storage_path_override;
	}
#ifdef TOOLS_ENABLED
	if (EditorPaths::get_singleton()) {
		return EditorPaths::get_singleton()->get_config_dir().path_join("ai_assistant").path_join("credentials.json");
	}
#endif
	return String();
}

void AICredentialStore::set_storage_path_override(const String &p_path) {
	storage_path_override = p_path;
}

Dictionary AICredentialStore::_load(bool *r_unreadable) {
	if (r_unreadable) {
		*r_unreadable = false;
	}
	const String path = get_storage_path();
	if (path.is_empty() || !FileAccess::exists(path)) {
		return Dictionary();
	}
	if (r_unreadable) {
		*r_unreadable = true;
	}
	Ref<FileAccess> f = FileAccess::open(path, FileAccess::READ);
	if (f.is_null()) {
		return Dictionary();
	}
	Ref<JSON> json;
	json.instantiate();
	if (json->parse(f->get_as_text()) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	if (r_unreadable) {
		*r_unreadable = false;
	}
	const Dictionary root = json->get_data();
	const Variant keys = root.get("keys", Variant());
	return keys.get_type() == Variant::DICTIONARY ? Dictionary(keys) : Dictionary();
}

bool AICredentialStore::_save(const Dictionary &p_data) {
	const String path = get_storage_path();
	if (path.is_empty()) {
		return false;
	}
	DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	Dictionary root;
	root["version"] = 1;
	root["keys"] = p_data;
	// Written beside the real file and renamed over it, so a crash or a full
	// disk cannot leave a truncated store behind.
	const String temp_path = path + ".tmp";
	Ref<FileAccess> f = FileAccess::open(temp_path, FileAccess::WRITE);
	if (f.is_null()) {
		return false;
	}
#ifdef UNIX_ENABLED
	// Restrict the file before anything secret is in it.
	FileAccess::set_unix_permissions(temp_path, FileAccess::UNIX_READ_OWNER | FileAccess::UNIX_WRITE_OWNER);
#endif
	f->store_string(JSON::stringify(root, "\t"));
	const bool written = f->get_error() == OK;
	f.unref();
	if (!written || DirAccess::rename_absolute(temp_path, path) != OK) {
		DirAccess::remove_absolute(temp_path);
		return false;
	}
	return true;
}

String AICredentialStore::get_environment_source(const String &p_provider_id) {
	static const struct {
		const char *provider;
		const char *variable;
	} vendor_variables[] = {
		{ "anthropic", "ANTHROPIC_API_KEY" },
		{ "openai", "OPENAI_API_KEY" },
		{ "gemini", "GEMINI_API_KEY" },
		{ "deepseek", "DEEPSEEK_API_KEY" },
		{ "glm", "ZHIPUAI_API_KEY" },
		{ nullptr, nullptr }
	};

	const String own = "GODOT_AI_API_KEY_" + p_provider_id.to_upper();
	if (!OS::get_singleton()->get_environment(own).is_empty()) {
		return own;
	}
	for (int i = 0; vendor_variables[i].provider; i++) {
		if (p_provider_id == vendor_variables[i].provider &&
				!OS::get_singleton()->get_environment(vendor_variables[i].variable).is_empty()) {
			return vendor_variables[i].variable;
		}
	}
	return String();
}

String AICredentialStore::get_stored_api_key(const String &p_provider_id) {
	const Dictionary keys = _load();
	if (!keys.has(p_provider_id)) {
		return String();
	}
	const CharString b64 = String(keys[p_provider_id]).ascii();
	PackedByteArray blob;
	blob.resize(b64.length() / 4 * 3 + 4);
	size_t blob_len = 0;
	if (b64.length() == 0 || CryptoCore::b64_decode(blob.ptrw(), blob.size(), &blob_len, (const uint8_t *)b64.get_data(), b64.length()) != OK) {
		return String();
	}
	blob.resize(blob_len);
	return unprotect(blob);
}

String AICredentialStore::get_api_key(const String &p_provider_id) {
	const String variable = get_environment_source(p_provider_id);
	if (!variable.is_empty()) {
		return OS::get_singleton()->get_environment(variable).strip_edges();
	}
	return get_stored_api_key(p_provider_id);
}

bool AICredentialStore::set_stored_api_key(const String &p_provider_id, const String &p_key) {
	bool unreadable = false;
	Dictionary keys = _load(&unreadable);
	ERR_FAIL_COND_V_MSG(unreadable, false, "[Godot AI] The credential store exists but cannot be read; not overwriting it: " + get_storage_path());
	if (p_key.is_empty()) {
		if (!keys.has(p_provider_id)) {
			return true;
		}
		keys.erase(p_provider_id);
		return _save(keys);
	}
	const PackedByteArray blob = protect(p_key);
	if (blob.is_empty()) {
		return false;
	}
	keys[p_provider_id] = CryptoCore::b64_encode_str(blob.ptr(), blob.size());
	return _save(keys);
}

int AICredentialStore::migrate_from_editor_settings() {
	int moved = 0;
#ifdef TOOLS_ENABLED
	if (!AISettings::is_available() || get_storage_path().is_empty()) {
		return 0;
	}

	// Moves one plain-text setting into the store. The setting is only blanked
	// once the key has been written and read back intact.
	auto move_setting = [&](const String &p_setting, const String &p_provider_id) {
		const String plain = String(AISettings::get(p_setting, "")).strip_edges();
		if (plain.is_empty()) {
			return;
		}
		if (get_stored_api_key(p_provider_id).is_empty()) {
			if (!set_stored_api_key(p_provider_id, plain) || get_stored_api_key(p_provider_id) != plain) {
				ERR_PRINT("[Godot AI] Could not move the API key for '" + p_provider_id + "' into the credential store; it stays in the editor settings.");
				return;
			}
		}
		AISettings::set(p_setting, "");
		moved++;
	};

	const Vector<String> providers = AIProvider::get_provider_ids();
	for (int i = 0; i < providers.size(); i++) {
		move_setting("api_key_" + providers[i], providers[i]);
	}
	// The pre-multi-provider single key belonged to whichever provider was active.
	if (AISettings::has("provider")) {
		move_setting("api_key", String(AISettings::get("provider", "")));
	}
	if (moved > 0) {
		AISettings::save();
	}
#endif
	return moved;
}
