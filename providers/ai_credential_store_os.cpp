#include "ai_credential_store.h"

#ifdef WINDOWS_ENABLED

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wincrypt.h>

// DPAPI ties the sealed blob to the current Windows user account.
bool ai_credential_os_protect(const PackedByteArray &p_plain, PackedByteArray &r_sealed) {
	DATA_BLOB in;
	in.pbData = const_cast<BYTE *>(p_plain.ptr());
	in.cbData = (DWORD)p_plain.size();
	DATA_BLOB out = {};
	if (!CryptProtectData(&in, L"Godot AI API key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		return false;
	}
	r_sealed.resize(out.cbData);
	memcpy(r_sealed.ptrw(), out.pbData, out.cbData);
	LocalFree(out.pbData);
	return true;
}

bool ai_credential_os_unprotect(const PackedByteArray &p_sealed, PackedByteArray &r_plain) {
	DATA_BLOB in;
	in.pbData = const_cast<BYTE *>(p_sealed.ptr());
	in.cbData = (DWORD)p_sealed.size();
	DATA_BLOB out = {};
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		return false;
	}
	r_plain.resize(out.cbData);
	if (out.cbData > 0) {
		memcpy(r_plain.ptrw(), out.pbData, out.cbData);
	}
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return true;
}

#else

bool ai_credential_os_protect(const PackedByteArray &p_plain, PackedByteArray &r_sealed) {
	return false;
}

bool ai_credential_os_unprotect(const PackedByteArray &p_sealed, PackedByteArray &r_plain) {
	return false;
}

#endif
