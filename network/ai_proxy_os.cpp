#include "ai_proxy.h"

#ifdef WINDOWS_ENABLED

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const wchar_t *INTERNET_SETTINGS_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

// The "use a proxy server" setting of the current user (Settings > Network >
// Proxy). Automatic configuration scripts are not evaluated.
String AIProxy::get_os_proxy() {
	DWORD enabled = 0;
	DWORD size = sizeof(enabled);
	if (RegGetValueW(HKEY_CURRENT_USER, INTERNET_SETTINGS_KEY, L"ProxyEnable", RRF_RT_REG_DWORD, nullptr, &enabled, &size) != ERROR_SUCCESS || enabled == 0) {
		return String();
	}
	wchar_t server[512] = {};
	size = sizeof(server) - sizeof(wchar_t);
	if (RegGetValueW(HKEY_CURRENT_USER, INTERNET_SETTINGS_KEY, L"ProxyServer", RRF_RT_REG_SZ, nullptr, server, &size) != ERROR_SUCCESS) {
		return String();
	}
	return String::utf16((const char16_t *)server);
}

#else

// Elsewhere the system proxy reaches programs through environment variables,
// which AIProxy reads on its own.
String AIProxy::get_os_proxy() {
	return String();
}

#endif
