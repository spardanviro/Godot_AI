#pragma once

#include "../ai_audio_generator.h"
#include "../ai_image_generator.h"
#include "../ai_network_thread.h"
#include "../ai_proxy.h"

#include "core/io/json.h"
#include "core/os/thread.h"
#include "tests/test_macros.h"

namespace TestAIProxy {

// --- Reading a proxy address ---

TEST_CASE("[AIAssistant][Proxy] host:port is read with or without a scheme") {
	AIProxyConfig config;

	REQUIRE(AIProxy::parse("127.0.0.1:7990", config));
	CHECK(config.host == "127.0.0.1");
	CHECK(config.port == 7990);

	REQUIRE(AIProxy::parse(" http://proxy.corp.example:3128/ ", config));
	CHECK(config.host == "proxy.corp.example");
	CHECK(config.port == 3128);
	CHECK(config.to_text() == "proxy.corp.example:3128");
}

TEST_CASE("[AIAssistant][Proxy] the Windows per-protocol form uses the https entry") {
	AIProxyConfig config;

	REQUIRE(AIProxy::parse("http=10.0.0.1:8080;https=10.0.0.2:8443;ftp=10.0.0.3:21", config));
	CHECK(config.to_text() == "10.0.0.2:8443");

	REQUIRE(AIProxy::parse("http=10.0.0.1:8080", config));
	CHECK(config.to_text() == "10.0.0.1:8080");
}

TEST_CASE("[AIAssistant][Proxy] what cannot be used as an HTTP proxy is refused") {
	static const char *refused[] = {
		"", "   ", "socks5://127.0.0.1:1080", "socks=127.0.0.1:1080", "127.0.0.1:0", "127.0.0.1:70000",
		"127.0.0.1:abc", "user:secret@proxy.example:8080", ":8080", "ftp=10.0.0.3:21", nullptr
	};
	for (int i = 0; refused[i]; i++) {
		AIProxyConfig config;
		CHECK_MESSAGE(!AIProxy::parse(refused[i], config), String(refused[i]));
		CHECK_FALSE(config.is_set());
	}
}

// --- Choosing one ---

TEST_CASE("[AIAssistant][Proxy] off means direct whatever the system says") {
	const AIProxyConfig config = AIProxy::choose(AIProxy::MODE_OFF, "1.2.3.4:1", "editor.example:1", "env.example:2", "os.example:3");

	CHECK_FALSE(config.is_set());
}

TEST_CASE("[AIAssistant][Proxy] custom uses the typed address and nothing else") {
	CHECK(AIProxy::choose(AIProxy::MODE_CUSTOM, "1.2.3.4:8888", "editor.example:1", "env.example:2", "os.example:3").to_text() == "1.2.3.4:8888");
	// A typed address that cannot be used is not replaced by the system's;
	// the settings dialog does not let such an address be saved.
	CHECK_FALSE(AIProxy::choose(AIProxy::MODE_CUSTOM, "socks5://1.2.3.4:1080", "editor.example:1", "env.example:2", "os.example:3").is_set());
}

TEST_CASE("[AIAssistant][Proxy] system mode takes the editor's proxy, then the environment's, then the OS's") {
	CHECK(AIProxy::choose(AIProxy::MODE_SYSTEM, "", "editor.example:1", "env.example:2", "os.example:3").to_text() == "editor.example:1");
	CHECK(AIProxy::choose(AIProxy::MODE_SYSTEM, "", "", "http://env.example:2", "os.example:3").to_text() == "env.example:2");
	CHECK(AIProxy::choose(AIProxy::MODE_SYSTEM, "", "", "", "http=os.example:3").to_text() == "os.example:3");
	CHECK_FALSE(AIProxy::choose(AIProxy::MODE_SYSTEM, "ignored:9", "", "", "").is_set());
}

TEST_CASE("[AIAssistant][Proxy] this machine and the local network are reached directly") {
	static const char *local[] = {
		"localhost", "LOCALHOST", "api.localhost", "printer.local", "127.0.0.1", "10.1.2.3", "192.168.1.20", "169.254.1.1", "[::1]", "",
		"gpu-box", "llm.internal", "nas.lan", nullptr
	};
	static const char *remote[] = { "api.openai.com", "8.8.8.8", "localhost.example.com", "10.example.com", nullptr };

	for (int i = 0; local[i]; i++) {
		CHECK_MESSAGE(AIProxy::is_local_host(local[i]), String(local[i]));
	}
	for (int i = 0; remote[i]; i++) {
		CHECK_MESSAGE(!AIProxy::is_local_host(remote[i]), String(remote[i]));
	}
}

// --- Network threads ---

static void _report_marks(void *p_userdata) {
	bool *seen = static_cast<bool *>(p_userdata);
	seen[0] = AINetworkThread::is_current();
	{
		AINetworkThread::Scope scope;
		seen[1] = AINetworkThread::is_current();
	}
	seen[2] = AINetworkThread::is_current();
}

TEST_CASE("[AIAssistant][Proxy] a network thread is marked only while its scope lives, and only that thread") {
	bool seen[3] = { true, false, true };
	AINetworkThread::Scope scope_on_this_thread;
	Thread thread;

	thread.start(_report_marks, seen);
	thread.wait_to_finish();

	CHECK_FALSE(seen[0]); // The mark of this thread does not leak into another.
	CHECK(seen[1]);
	CHECK_FALSE(seen[2]);
	CHECK(AINetworkThread::is_current());
}

// --- Asset generation requests ---

static Dictionary _parse(const String &p_json) {
	return JSON::parse_string(p_json);
}

TEST_CASE("[AIAssistant][Assets] the image request for a GPT image model carries no DALL-E parameters") {
	const Dictionary body = _parse(AIImageGenerator::build_request("a red fox", "gpt-image-1"));

	CHECK(String(body["model"]) == "gpt-image-1");
	CHECK(String(body["prompt"]) == "a red fox");
	CHECK_FALSE(body.has("response_format"));
	CHECK_FALSE(body.has("quality"));
}

TEST_CASE("[AIAssistant][Assets] a DALL-E model is still asked for base64 data") {
	const Dictionary body = _parse(AIImageGenerator::build_request("a red fox", "dall-e-3"));

	CHECK(String(body["response_format"]) == "b64_json");
}

TEST_CASE("[AIAssistant][Assets] an empty model setting means the built-in default") {
	CHECK(String(_parse(AIImageGenerator::build_request("x", ""))["model"]) == AIImageGenerator::DEFAULT_MODEL);
	CHECK(String(_parse(AIAudioGenerator::build_tts_request("x", ""))["model"]) == AIAudioGenerator::DEFAULT_MODEL);
	CHECK(String(_parse(AIAudioGenerator::build_tts_request("x", "tts-1-hd"))["model"]) == "tts-1-hd");
}

} // namespace TestAIProxy
