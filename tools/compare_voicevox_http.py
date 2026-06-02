import base64
import json
import math
import sys
import urllib.error
import urllib.parse
import urllib.request
import wave
import zipfile
from io import BytesIO


OFFICIAL_BASE = sys.argv[1]
LITEVOX_BASE = sys.argv[2]
TEXT = "ずんだもんなのだ"
KANA = "ズンダモンナノダ'"
DEFAULT_SONG_SCORE = {
    "notes": [
        {"key": None, "frame_length": 15, "lyric": ""},
        {"key": 60, "frame_length": 45, "lyric": "ド"},
        {"key": 62, "frame_length": 45, "lyric": "レ"},
        {"key": 64, "frame_length": 45, "lyric": "ミ"},
        {"key": None, "frame_length": 15, "lyric": ""},
    ]
}


def request(base_url, method, path, body=None, headers=None):
    request_headers = headers or {}
    request_body = body
    if isinstance(request_body, str):
        request_body = request_body.encode()
    url = base_url + path
    http_request = urllib.request.Request(url, data=request_body, method=method, headers=request_headers)
    try:
        with urllib.request.urlopen(http_request, timeout=120) as response:
            return {
                "status": response.status,
                "content_type": response.headers.get("Content-Type"),
                "headers": dict(response.headers.items()),
                "body": response.read(),
            }
    except urllib.error.HTTPError as error:
        return {
            "status": error.code,
            "content_type": error.headers.get("Content-Type"),
            "headers": dict(error.headers.items()),
            "body": error.read(),
        }


def quote(text):
    return urllib.parse.quote(text)


def post(path, body=b"", headers=None):
    return "POST", path, body, headers or {}


def get(path):
    return "GET", path, None, {}


def put(path, body=b"", headers=None):
    return "PUT", path, body, headers or {}


def delete(path):
    return "DELETE", path, None, {}


def load_json(body):
    return json.loads(body.decode())


def compare_bytes(label, official_response, litevox_response):
    return {
        "label": label,
        "status_equal": official_response["status"] == litevox_response["status"],
        "content_type_equal": official_response["content_type"] == litevox_response["content_type"],
        "body_equal": official_response["body"] == litevox_response["body"],
        "official_status": official_response["status"],
        "litevox_status": litevox_response["status"],
        "official_content_type": official_response["content_type"],
        "litevox_content_type": litevox_response["content_type"],
        "official_size": len(official_response["body"]),
        "litevox_size": len(litevox_response["body"]),
        "official_preview": official_response["body"][:160].decode("utf-8", "replace"),
        "litevox_preview": litevox_response["body"][:160].decode("utf-8", "replace"),
    }


def get_wav_info(body):
    with wave.open(BytesIO(body), "rb") as wav_file:
        return {
            "channels": wav_file.getnchannels(),
            "sample_width": wav_file.getsampwidth(),
            "frame_rate": wav_file.getframerate(),
            "frames": wav_file.getnframes(),
        }


def compare_wav_shape(label, official_response, litevox_response):
    result = compare_bytes(label, official_response, litevox_response)
    result["raw_body_equal"] = result["body_equal"]
    result["body_equal"] = True
    result["official_wav"] = get_wav_info(official_response["body"]) if official_response["status"] == 200 else None
    result["litevox_wav"] = get_wav_info(litevox_response["body"]) if litevox_response["status"] == 200 else None
    result["wav_shape_equal"] = result["official_wav"] == result["litevox_wav"]
    return result


def compare_zip_wav_shape(label, official_response, litevox_response):
    result = compare_bytes(label, official_response, litevox_response)
    result["raw_body_equal"] = result["body_equal"]
    result["body_equal"] = True
    if official_response["status"] != 200 or litevox_response["status"] != 200:
        result["zip_wav_shape_equal"] = official_response["body"] == litevox_response["body"]
        return result
    with zipfile.ZipFile(BytesIO(official_response["body"])) as official_zip:
        official_names = official_zip.namelist()
        official_wavs = {name: get_wav_info(official_zip.read(name)) for name in official_names}
    with zipfile.ZipFile(BytesIO(litevox_response["body"])) as litevox_zip:
        litevox_names = litevox_zip.namelist()
        litevox_wavs = {name: get_wav_info(litevox_zip.read(name)) for name in litevox_names}
    result["official_zip_names"] = official_names
    result["litevox_zip_names"] = litevox_names
    result["zip_wav_shape_equal"] = official_wavs == litevox_wavs
    return result


def compare_json_semantic(label, official_response, litevox_response):
    result = compare_bytes(label, official_response, litevox_response)
    if official_response["body"] and litevox_response["body"]:
        try:
            result["json_equal"] = load_json(official_response["body"]) == load_json(litevox_response["body"])
        except Exception:
            result["json_equal"] = False
    else:
        result["json_equal"] = official_response["body"] == litevox_response["body"]
    return result


def are_finite_numbers(values, allow_negative):
    if not isinstance(values, list):
        return False
    for number in values:
        if not isinstance(number, (int, float)) or not math.isfinite(number):
            return False
        if not allow_negative and number < 0:
            return False
    return True


def get_frame_count(frame_query):
    if not isinstance(frame_query.get("phonemes"), list):
        return -1
    frame_count = 0
    for phoneme in frame_query["phonemes"]:
        if not isinstance(phoneme, dict) or not isinstance(phoneme.get("frame_length"), int):
            return -1
        frame_count += phoneme["frame_length"]
    return frame_count


def compare_frame_audio_query_shape(label, official_response, litevox_response):
    result = compare_bytes(label, official_response, litevox_response)
    result["raw_body_equal"] = result["body_equal"]
    result["body_equal"] = True
    try:
        official_query = load_json(official_response["body"])
        litevox_query = load_json(litevox_response["body"])
        official_frame_count = get_frame_count(official_query)
        litevox_frame_count = get_frame_count(litevox_query)
        result["phonemes_equal"] = official_query.get("phonemes") == litevox_query.get("phonemes")
        result["f0_shape_equal"] = len(official_query.get("f0", [])) == len(litevox_query.get("f0", [])) == official_frame_count == litevox_frame_count
        result["volume_shape_equal"] = len(official_query.get("volume", [])) == len(litevox_query.get("volume", [])) == official_frame_count == litevox_frame_count
        result["f0_values_valid"] = are_finite_numbers(official_query.get("f0"), False) and are_finite_numbers(litevox_query.get("f0"), False)
        result["volume_values_valid"] = are_finite_numbers(official_query.get("volume"), True) and are_finite_numbers(litevox_query.get("volume"), True)
    except Exception:
        result["phonemes_equal"] = False
        result["f0_shape_equal"] = False
        result["volume_shape_equal"] = False
        result["f0_values_valid"] = False
        result["volume_values_valid"] = False
    return result


def compare_float_array_shape(label, official_response, litevox_response):
    result = compare_bytes(label, official_response, litevox_response)
    result["raw_body_equal"] = result["body_equal"]
    result["body_equal"] = True
    try:
        official_values = load_json(official_response["body"])
        litevox_values = load_json(litevox_response["body"])
        result["array_length_equal"] = isinstance(official_values, list) and isinstance(litevox_values, list) and len(official_values) == len(litevox_values)
        result["array_values_valid"] = are_finite_numbers(official_values, True) and are_finite_numbers(litevox_values, True)
    except Exception:
        result["array_length_equal"] = False
        result["array_values_valid"] = False
    return result


def call_both(step):
    method, path, body, headers = step
    return request(OFFICIAL_BASE, method, path, body, headers), request(LITEVOX_BASE, method, path, body, headers)


def add_preset(preset_id):
    preset = {
        "id": preset_id,
        "name": "LiteVox Verify",
        "speaker_uuid": "dummy",
        "style_id": 3,
        "speedScale": 1,
        "pitchScale": 0,
        "intonationScale": 1,
        "volumeScale": 1,
        "prePhonemeLength": 0.1,
        "postPhonemeLength": 0.1,
        "pauseLength": 0.4,
        "pauseLengthScale": 1,
    }
    body = json.dumps(preset, ensure_ascii=False, separators=(",", ":")).encode()
    return post("/add_preset", body, {"Content-Type": "application/json"})


def delete_preset(preset_id):
    return post(f"/delete_preset?id={preset_id}")


def make_audio_query():
    official_response, litevox_response = call_both(post(f"/audio_query?speaker=3&text={quote(TEXT)}"))
    return official_response, litevox_response


def make_frame_query():
    body = json.dumps(DEFAULT_SONG_SCORE, ensure_ascii=False, separators=(",", ":")).encode()
    return body, call_both(post("/sing_frame_audio_query?speaker=6000", body, {"Content-Type": "application/json"}))


def run():
    preset_id = 480000
    for cleanup_id in range(preset_id, preset_id + 4):
        call_both(delete_preset(cleanup_id))
    official_audio_query, litevox_audio_query = make_audio_query()
    audio_query_body = official_audio_query["body"]
    official_accent, litevox_accent = call_both(post(f"/accent_phrases?speaker=3&text={quote(TEXT)}"))
    accent_body = official_accent["body"]
    score_body, frame_query_pair = make_frame_query()
    frame_query_body = frame_query_pair[0]["body"]
    wav_body = base64.b64encode(request(OFFICIAL_BASE, "POST", "/synthesis?speaker=3", audio_query_body, {"Content-Type": "application/json"})["body"]).decode()
    cases = [
        ("root", get("/"), compare_bytes),
        ("version", get("/version"), compare_bytes),
        ("core_versions", get("/core_versions"), compare_bytes),
        ("supported_devices", get("/supported_devices"), compare_bytes),
        ("speakers", get("/speakers"), compare_bytes),
        ("singers", get("/singers"), compare_bytes),
        ("speaker_info_base64", get("/speaker_info?speaker_uuid=7ffcb7ce-00ec-4bdc-82cd-45a8889e43ff"), compare_bytes),
        ("singer_info_base64", get("/singer_info?speaker_uuid=7ffcb7ce-00ec-4bdc-82cd-45a8889e43ff"), compare_json_semantic),
        ("presets_initial", get("/presets"), compare_bytes),
        ("engine_manifest", get("/engine_manifest"), compare_bytes),
        ("setting", get("/setting"), compare_bytes),
        ("openapi", get("/openapi.json"), compare_bytes),
        ("docs", get("/docs"), compare_bytes),
        ("redoc", get("/redoc"), compare_bytes),
        ("docs_oauth2_redirect", get("/docs/oauth2-redirect"), compare_bytes),
        ("validate_kana_ok", post(f"/validate_kana?text={quote(KANA)}"), compare_bytes),
        ("audio_query", post(f"/audio_query?speaker=3&text={quote(TEXT)}"), compare_bytes),
        ("audio_query_kana", post(f"/audio_query?speaker=3&text={quote(KANA)}&is_kana=true"), compare_bytes),
        ("accent_phrases", post(f"/accent_phrases?speaker=3&text={quote(TEXT)}"), compare_bytes),
        ("accent_phrases_kana", post(f"/accent_phrases?speaker=3&text={quote(KANA)}&is_kana=true"), compare_bytes),
        ("mora_data", post("/mora_data?speaker=3", accent_body, {"Content-Type": "application/json"}), compare_bytes),
        ("mora_length", post("/mora_length?speaker=3", accent_body, {"Content-Type": "application/json"}), compare_bytes),
        ("mora_pitch", post("/mora_pitch?speaker=3", accent_body, {"Content-Type": "application/json"}), compare_bytes),
        ("synthesis", post("/synthesis?speaker=3", audio_query_body, {"Content-Type": "application/json"}), compare_wav_shape),
        ("cancellable_synthesis", post("/cancellable_synthesis?speaker=3", audio_query_body, {"Content-Type": "application/json"}), compare_wav_shape),
        ("multi_synthesis", post("/multi_synthesis?speaker=3", b"[" + audio_query_body + b"]", {"Content-Type": "application/json"}), compare_zip_wav_shape),
        ("connect_waves", post("/connect_waves", json.dumps([wav_body], separators=(",", ":")).encode(), {"Content-Type": "application/json"}), compare_wav_shape),
        ("morphable_targets", post("/morphable_targets", b"[3,8,89,2]", {"Content-Type": "application/json"}), compare_bytes),
        ("synthesis_morphing", post("/synthesis_morphing?base_speaker=3&target_speaker=1&morph_rate=0.5", audio_query_body, {"Content-Type": "application/json"}), compare_wav_shape),
        ("sing_frame_audio_query", post("/sing_frame_audio_query?speaker=6000", score_body, {"Content-Type": "application/json"}), compare_frame_audio_query_shape),
        ("sing_frame_f0", post("/sing_frame_f0?speaker=6000", json.dumps({"score": load_json(score_body), "frame_audio_query": load_json(frame_query_body)}, ensure_ascii=False, separators=(",", ":")).encode(), {"Content-Type": "application/json"}), compare_float_array_shape),
        ("sing_frame_volume", post("/sing_frame_volume?speaker=6000", json.dumps({"score": load_json(score_body), "frame_audio_query": load_json(frame_query_body)}, ensure_ascii=False, separators=(",", ":")).encode(), {"Content-Type": "application/json"}), compare_float_array_shape),
        ("frame_synthesis", post("/frame_synthesis?speaker=3000", frame_query_body, {"Content-Type": "application/json"}), compare_wav_shape),
        ("user_dict_initial", get("/user_dict"), compare_bytes),
        ("is_initialized_before", get("/is_initialized_speaker?speaker=3"), compare_bytes),
        ("initialize_speaker", post("/initialize_speaker?speaker=3"), compare_bytes),
        ("is_initialized_after", get("/is_initialized_speaker?speaker=3"), compare_bytes),
        ("audio_query_from_preset_missing", post(f"/audio_query_from_preset?preset_id={preset_id}&text={quote(TEXT)}"), compare_bytes),
        ("delete_preset_missing", delete_preset(preset_id), compare_bytes),
        ("update_preset_missing", post("/update_preset", json.dumps({"id": preset_id, "name": "LiteVox Verify", "speaker_uuid": "dummy", "style_id": 3, "speedScale": 1, "pitchScale": 0, "intonationScale": 1, "volumeScale": 1, "prePhonemeLength": 0.1, "postPhonemeLength": 0.1}, separators=(",", ":")).encode(), {"Content-Type": "application/json"}), compare_bytes),
        ("add_preset", add_preset(preset_id), compare_bytes),
        ("add_preset_duplicate", add_preset(preset_id), compare_bytes),
        ("audio_query_from_preset", post(f"/audio_query_from_preset?preset_id={preset_id}&text={quote(TEXT)}"), compare_bytes),
        ("update_preset", post("/update_preset", json.dumps({"id": preset_id, "name": "LiteVox Verify", "speaker_uuid": "dummy", "style_id": 3, "speedScale": 1, "pitchScale": 0, "intonationScale": 1, "volumeScale": 1, "prePhonemeLength": 0.1, "postPhonemeLength": 0.1}, separators=(",", ":")).encode(), {"Content-Type": "application/json"}), compare_bytes),
        ("delete_preset", delete_preset(preset_id), compare_bytes),
    ]
    results = []
    for label, step, comparator in cases:
        official_response, litevox_response = call_both(step)
        result = comparator(label, official_response, litevox_response)
        results.append(result)
        flags = [key for key in ("status_equal", "content_type_equal", "body_equal", "json_equal", "wav_shape_equal", "zip_wav_shape_equal", "phonemes_equal", "f0_shape_equal", "volume_shape_equal", "f0_values_valid", "volume_values_valid", "array_length_equal", "array_values_valid") if key in result and not result[key]]
        print(json.dumps({"label": label, "failed": flags, "official_status": result["official_status"], "litevox_status": result["litevox_status"], "official_size": result["official_size"], "litevox_size": result["litevox_size"]}, ensure_ascii=False, separators=(",", ":")))
    failed_results = [result for result in results if any(key in result and not result[key] for key in ("status_equal", "content_type_equal", "body_equal", "json_equal", "wav_shape_equal", "zip_wav_shape_equal", "phonemes_equal", "f0_shape_equal", "volume_shape_equal", "f0_values_valid", "volume_values_valid", "array_length_equal", "array_values_valid"))]
    print(json.dumps({"total": len(results), "failed": len(failed_results), "failed_labels": [result["label"] for result in failed_results]}, ensure_ascii=False, separators=(",", ":")))
    if failed_results:
        print(json.dumps(failed_results, ensure_ascii=False, indent=2))
        sys.exit(1)


if __name__ == "__main__":
    run()
