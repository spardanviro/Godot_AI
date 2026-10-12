#ifdef TOOLS_ENABLED

#include "ai_asset_service.h"
#include "../common/ai_settings.h"
#include "../common/ai_notice.h"
#include "ai_asset_request.h"

#include "ai_audio_generator.h"
#include "../providers/ai_credential_store.h"
#include "ai_image_generator.h"
#include "../common/ai_log.h"
#include "../providers/ai_provider_setup.h"
#include "../network/ai_proxy.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/http_request.h"

static const char *IMAGE_ENDPOINT = "https://api.openai.com/v1/images/generations";
static const char *AUDIO_ENDPOINT = "https://api.openai.com/v1/audio/speech";
static const char *DEFAULT_IMAGE_PATH = "res://ai_generated_image.png";
static const char *DEFAULT_AUDIO_PATH = "res://ai_generated_audio.mp3";
static const double REQUEST_TIMEOUT_SEC = 120.0;

void AIAssetService::_bind_methods() {
	ADD_SIGNAL(MethodInfo("notice", PropertyInfo(Variant::INT, "level"), PropertyInfo(Variant::STRING, "text")));
}

void AIAssetService::_notify(int p_level, const String &p_text) {
	emit_signal(SNAME("notice"), p_level, p_text);
}

void AIAssetService::enqueue(const AIAssetRequest &p_request) {
	const String kind_en = p_request.is_audio ? "Audio" : "Image";
	const String kind_zh = p_request.is_audio ? U"音频" : U"图像";

	if (!AIAssetRequest::is_safe_path(p_request.save_path)) {
		_notify(AI_NOTICE_ERROR,
				AILocalization::get_language() == "zh"
						? String(U"已跳过") + kind_zh + U"生成：保存路径必须位于 res:// 内 (" + p_request.save_path + ")."
						: kind_en + " generation skipped: save path must be inside res:// (" + p_request.save_path + ").");
		return;
	}
	// Generation always talks to api.openai.com, so only the OpenAI key may be
	// sent — never the key of whichever provider is active.
	if (AICredentialStore::get_api_key("openai").is_empty()) {
		_notify(AI_NOTICE_ERROR,
				AILocalization::get_language() == "zh"
						? kind_zh + U"生成使用 OpenAI 接口：请先在设置中填写 OpenAI API 密钥。"
						: kind_en + " generation uses the OpenAI API: set an OpenAI API key in Settings first.");
		return;
	}

	queue.push_back(p_request);
	if (!request_active) {
		_start_next();
	}
}

void AIAssetService::_start_next() {
	while (!queue.is_empty() && !request_active) {
		current = queue[0];
		queue.remove_at(0);

		_notify(AI_NOTICE_INFO,
				(current.is_audio ? AI_L("Generating audio: ", "正在生成音频：") : AI_L("Generating image: ", "正在生成图像：")) + current.prompt + "...");

		Vector<String> headers;
		headers.push_back("Content-Type: application/json");
		headers.push_back("Authorization: Bearer " + AICredentialStore::get_api_key("openai"));
		const String model = String(AISettings::get(current.is_audio ? "ai_assistant/speech_model" : "ai_assistant/image_model", "")).strip_edges();
		const String body = current.is_audio ? AIAudioGenerator::build_tts_request(current.prompt, model) : AIImageGenerator::build_request(current.prompt, model);
		const String endpoint = current.is_audio ? AUDIO_ENDPOINT : IMAGE_ENDPOINT;
		AIProxy::apply(http_request, endpoint);
		const Error err = http_request->request(endpoint, headers, HTTPClient::METHOD_POST, body);
		if (err == OK) {
			request_active = true;
		} else {
			_notify(AI_NOTICE_ERROR, AI_L("Failed to send the asset generation request.", "资源生成请求发送失败。"));
		}
	}
}

void AIAssetService::_on_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	request_active = false;

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != 200) {
		const String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
		_notify(AI_NOTICE_ERROR, AI_L("Asset generation failed (HTTP ", "资源生成失败 (HTTP ") + itos(p_response_code) + "): " + body_text.left(300));
	} else if (current.is_audio) {
		String path = current.save_path.is_empty() ? String(DEFAULT_AUDIO_PATH) : current.save_path;
		// The speech API is asked for mp3 whatever extension the model picked.
		if (path.ends_with(".wav")) {
			path = path.get_basename() + ".mp3";
		}
		if (AIAudioGenerator::save_audio_bytes(p_body, path)) {
			_notify(AI_NOTICE_SUCCESS, AI_L("Audio saved: ", "音频已保存：") + path);
		} else {
			_notify(AI_NOTICE_ERROR, AI_L("Failed to save audio.", "音频保存失败。"));
		}
	} else {
		const String b64 = AIImageGenerator::parse_response(String::utf8((const char *)p_body.ptr(), p_body.size()));
		const String path = current.save_path.is_empty() ? String(DEFAULT_IMAGE_PATH) : current.save_path;
		if (b64.is_empty()) {
			_notify(AI_NOTICE_ERROR, AI_L("Failed to parse image response.", "图像响应解析失败。"));
		} else if (AIImageGenerator::save_base64_as_texture(b64, path)) {
			_notify(AI_NOTICE_SUCCESS, AI_L("Image saved: ", "图像已保存：") + path);
		} else {
			_notify(AI_NOTICE_ERROR, AI_L("Failed to save image.", "图像保存失败。"));
		}
	}

	_start_next();
}

AIAssetService::AIAssetService() {
	http_request = memnew(HTTPRequest);
	http_request->set_use_threads(true);
	http_request->set_timeout(REQUEST_TIMEOUT_SEC);
	http_request->connect("request_completed", callable_mp(this, &AIAssetService::_on_request_completed));
	add_child(http_request);
}

#endif // TOOLS_ENABLED
