#include "api_schema.hpp"
#include "api_schema_internal.hpp"

#include <string>

static void alignOfficialResponseStatuses(std::string &jsonText) {
    addSchemaResponseStatus(jsonText, "/audio_query", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/audio_query_from_preset", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/accent_phrases", "post", "400", "kana validation error");
    addSchemaResponseStatus(jsonText, "/accent_phrases", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/mora_data", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/mora_length", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/mora_pitch", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/synthesis", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/cancellable_synthesis", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/multi_synthesis", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/sing_frame_audio_query", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/sing_frame_f0", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/sing_frame_volume", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/frame_synthesis", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/connect_waves", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/validate_kana", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/initialize_speaker", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/is_initialized_speaker", "get", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/supported_devices", "get", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/morphable_targets", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/synthesis_morphing", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/add_preset", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/update_preset", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/delete_preset", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/speakers", "get", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/speaker_info", "get", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/singers", "get", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/singer_info", "get", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/user_dict_word", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/user_dict_word/{word_uuid}", "put", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/user_dict_word/{word_uuid}", "delete", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/import_user_dict", "post", "422", "Validation Error");
    addSchemaResponseStatus(jsonText, "/setting", "post", "422", "Validation Error");
    removeSchemaResponseStatus(jsonText, "/audio_query_from_preset", "post", "400");
    removeSchemaResponseStatus(jsonText, "/synthesis", "post", "400");
    removeSchemaResponseStatus(jsonText, "/synthesis", "post", "405");
    removeSchemaResponseStatus(jsonText, "/cancellable_synthesis", "post", "400");
    removeSchemaResponseStatus(jsonText, "/sing_frame_audio_query", "post", "501");
    removeSchemaResponseStatus(jsonText, "/sing_frame_f0", "post", "501");
    removeSchemaResponseStatus(jsonText, "/sing_frame_volume", "post", "501");
    removeSchemaResponseStatus(jsonText, "/frame_synthesis", "post", "501");
    removeSchemaResponseStatus(jsonText, "/morphable_targets", "post", "400");
    removeSchemaResponseStatus(jsonText, "/synthesis_morphing", "post", "400");
    removeSchemaResponseStatus(jsonText, "/synthesis_morphing", "post", "501");
    removeSchemaResponseStatus(jsonText, "/add_preset", "post", "400");
    removeSchemaResponseStatus(jsonText, "/update_preset", "post", "400");
    removeSchemaResponseStatus(jsonText, "/speaker_info", "get", "404");
    removeSchemaResponseStatus(jsonText, "/singer_info", "get", "404");
    removeSchemaResponseStatus(jsonText, "/import_user_dict", "post", "400");
    removeSchemaResponseStatus(jsonText, "/import_user_dict", "post", "405");
    removeSchemaResponseStatus(jsonText, "/setting", "post", "400");
}

static void alignOfficialResponseSchemas(std::string &jsonText) {
    setSchemaResponseStatus(jsonText, "/audio_query", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/AudioQuery"}}}})json");
    setSchemaResponseStatus(jsonText, "/audio_query", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/audio_query_from_preset", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/AudioQuery"}}}})json");
    setSchemaResponseStatus(jsonText, "/audio_query_from_preset", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/accent_phrases", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/AccentPhrase"},"title":"Response Accent Phrases Accent Phrases Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/accent_phrases", "post", "400", R"json({"description":"読み仮名のパースに失敗","content":{"application/json":{"schema":{"$ref":"#/components/schemas/ParseKanaBadRequest"}}}})json");
    setSchemaResponseStatus(jsonText, "/accent_phrases", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/mora_data", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/AccentPhrase"},"title":"Response Mora Data Mora Data Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/mora_data", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/mora_length", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/AccentPhrase"},"title":"Response Mora Length Mora Length Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/mora_length", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/mora_pitch", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/AccentPhrase"},"title":"Response Mora Pitch Mora Pitch Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/mora_pitch", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/synthesis", "post", "200", R"json({"description":"Successful Response","content":{"audio/wav":{"schema":{"type":"string","format":"binary"}}}})json");
    setSchemaResponseStatus(jsonText, "/synthesis", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/cancellable_synthesis", "post", "200", R"json({"description":"Successful Response","content":{"audio/wav":{"schema":{"type":"string","format":"binary"}}}})json");
    setSchemaResponseStatus(jsonText, "/cancellable_synthesis", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/multi_synthesis", "post", "200", R"json({"description":"Successful Response","content":{"application/zip":{"schema":{"type":"string","format":"binary"}}}})json");
    setSchemaResponseStatus(jsonText, "/multi_synthesis", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/sing_frame_audio_query", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/FrameAudioQuery"}}}})json");
    setSchemaResponseStatus(jsonText, "/sing_frame_audio_query", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/sing_frame_f0", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"type":"number"},"title":"Response Sing Frame F0 Sing Frame F0 Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/sing_frame_f0", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/sing_frame_volume", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"type":"number"},"title":"Response Sing Frame Volume Sing Frame Volume Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/sing_frame_volume", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/frame_synthesis", "post", "200", R"json({"description":"Successful Response","content":{"audio/wav":{"schema":{"type":"string","format":"binary"}}}})json");
    setSchemaResponseStatus(jsonText, "/frame_synthesis", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/connect_waves", "post", "200", R"json({"description":"Successful Response","content":{"audio/wav":{"schema":{"type":"string","format":"binary"}}}})json");
    setSchemaResponseStatus(jsonText, "/connect_waves", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/validate_kana", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"boolean","title":"Response Validate Kana Validate Kana Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/validate_kana", "post", "400", R"json({"description":"テキストが不正です","content":{"application/json":{"schema":{"$ref":"#/components/schemas/ParseKanaBadRequest"}}}})json");
    setSchemaResponseStatus(jsonText, "/validate_kana", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/initialize_speaker", "post", "204", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/initialize_speaker", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/is_initialized_speaker", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"boolean","title":"Response Is Initialized Speaker Is Initialized Speaker Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/is_initialized_speaker", "get", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/supported_devices", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/SupportedDevicesInfo"}}}})json");
    setSchemaResponseStatus(jsonText, "/supported_devices", "get", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/morphable_targets", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"type":"object","additionalProperties":{"$ref":"#/components/schemas/MorphableTargetInfo"}},"title":"Response Morphable Targets Morphable Targets Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/morphable_targets", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/synthesis_morphing", "post", "200", R"json({"description":"Successful Response","content":{"audio/wav":{"schema":{"type":"string","format":"binary"}}}})json");
    setSchemaResponseStatus(jsonText, "/synthesis_morphing", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/presets", "get", "200", R"json({"description":"プリセットのリスト","content":{"application/json":{"schema":{"items":{"$ref":"#/components/schemas/Preset"},"type":"array","title":"Response Get Presets Presets Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/add_preset", "post", "200", R"json({"description":"追加したプリセットのプリセットID","content":{"application/json":{"schema":{"type":"integer","title":"Response Add Preset Add Preset Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/add_preset", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/update_preset", "post", "200", R"json({"description":"更新したプリセットのプリセットID","content":{"application/json":{"schema":{"type":"integer","title":"Response Update Preset Update Preset Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/update_preset", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/delete_preset", "post", "204", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/delete_preset", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/speakers", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/Speaker"},"title":"Response Speakers Speakers Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/speakers", "get", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/speaker_info", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/SpeakerInfo"}}}})json");
    setSchemaResponseStatus(jsonText, "/speaker_info", "get", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/singers", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"array","items":{"$ref":"#/components/schemas/Speaker"},"title":"Response Singers Singers Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/singers", "get", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/singer_info", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/SpeakerInfo"}}}})json");
    setSchemaResponseStatus(jsonText, "/singer_info", "get", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/user_dict", "get", "200", R"json({"description":"単語のUUIDとその詳細","content":{"application/json":{"schema":{"additionalProperties":{"$ref":"#/components/schemas/UserDictWord"},"type":"object","title":"Response Get User Dict Words User Dict Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/user_dict_word", "post", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"string","title":"Response Add User Dict Word User Dict Word Post"}}}})json");
    setSchemaResponseStatus(jsonText, "/user_dict_word", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/user_dict_word/{word_uuid}", "put", "204", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/user_dict_word/{word_uuid}", "put", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/user_dict_word/{word_uuid}", "delete", "204", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/user_dict_word/{word_uuid}", "delete", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/import_user_dict", "post", "204", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/import_user_dict", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/version", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"type":"string","title":"Response Version Version Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/core_versions", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"items":{"type":"string"},"type":"array","title":"Response Core Versions Core Versions Get"}}}})json");
    setSchemaResponseStatus(jsonText, "/engine_manifest", "get", "200", R"json({"description":"Successful Response","content":{"application/json":{"schema":{"$ref":"#/components/schemas/EngineManifest"}}}})json");
    setSchemaResponseStatus(jsonText, "/setting", "get", "200", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/setting", "post", "204", R"json({"description":"Successful Response"})json");
    setSchemaResponseStatus(jsonText, "/setting", "post", "422", R"json({"description":"Validation Error","content":{"application/json":{"schema":{"$ref":"#/components/schemas/HTTPValidationError"}}}})json");
    setSchemaResponseStatus(jsonText, "/", "get", "200", R"json({"description":"Successful Response","content":{"text/html":{"schema":{"type":"string"}}}})json");
}

static void alignOfficialRequestBodies(std::string &jsonText) {
    std::string accentPhrasesBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "items": {
                  "$ref": "#/components/schemas/AccentPhrase"
                },
                "title": "Accent Phrases",
                "type": "array"
              }
            }
          },
          "required": true
        })json";
    std::string audioQueryBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/AudioQuery"
              }
            }
          },
          "required": true
        })json";
    std::string frameAudioQueryBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/FrameAudioQuery"
              }
            }
          },
          "required": true
        })json";
    std::string multiSynthesisBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "items": {
                  "$ref": "#/components/schemas/AudioQuery"
                },
                "title": "Queries",
                "type": "array"
              }
            }
          },
          "required": true
        })json";
    std::string stringWaveArrayBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "items": {
                  "type": "string"
                },
                "title": "Waves",
                "type": "array"
              }
            }
          },
          "required": true
        })json";
    std::string morphableTargetsBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "items": {
                  "type": "integer"
                },
                "title": "Base Style Ids",
                "type": "array"
              }
            }
          },
          "required": true
        })json";
    std::string scoreBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/Score"
              }
            }
          },
          "required": true
        })json";
    std::string singFrameF0Body = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/Body_sing_frame_f0_sing_frame_f0_post"
              }
            }
          },
          "required": true
        })json";
    std::string singFrameVolumeBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/Body_sing_frame_volume_sing_frame_volume_post"
              }
            }
          },
          "required": true
        })json";
    std::string presetAddBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/Preset",
                "description": "新しいプリセット。プリセットIDが既存のものと重複している場合は、新規のプリセットIDが採番されます。"
              }
            }
          },
          "required": true
        })json";
    std::string presetUpdateBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "$ref": "#/components/schemas/Preset",
                "description": "更新するプリセット。プリセットIDが更新対象と一致している必要があります。"
              }
            }
          },
          "required": true
        })json";
    std::string userDictImportBody = R"json(        "requestBody": {
          "content": {
            "application/json": {
              "schema": {
                "additionalProperties": {
                  "$ref": "#/components/schemas/UserDictWord"
                },
                "description": "インポートするユーザー辞書のデータ",
                "title": "Import Dict Data",
                "type": "object"
              }
            }
          },
          "required": true
        })json";
    std::string settingBody = R"json(        "requestBody": {
          "content": {
            "application/x-www-form-urlencoded": {
              "schema": {
                "$ref": "#/components/schemas/Body_setting_post_setting_post"
              }
            }
          },
          "required": true
        })json";
    setSchemaRequestBody(jsonText, "/mora_data", "post", accentPhrasesBody);
    setSchemaRequestBody(jsonText, "/mora_length", "post", accentPhrasesBody);
    setSchemaRequestBody(jsonText, "/mora_pitch", "post", accentPhrasesBody);
    setSchemaRequestBody(jsonText, "/synthesis", "post", audioQueryBody);
    setSchemaRequestBody(jsonText, "/cancellable_synthesis", "post", audioQueryBody);
    setSchemaRequestBody(jsonText, "/multi_synthesis", "post", multiSynthesisBody);
    setSchemaRequestBody(jsonText, "/sing_frame_audio_query", "post", scoreBody);
    setSchemaRequestBody(jsonText, "/sing_frame_f0", "post", singFrameF0Body);
    setSchemaRequestBody(jsonText, "/sing_frame_volume", "post", singFrameVolumeBody);
    setSchemaRequestBody(jsonText, "/frame_synthesis", "post", frameAudioQueryBody);
    setSchemaRequestBody(jsonText, "/connect_waves", "post", stringWaveArrayBody);
    setSchemaRequestBody(jsonText, "/morphable_targets", "post", morphableTargetsBody);
    setSchemaRequestBody(jsonText, "/synthesis_morphing", "post", audioQueryBody);
    setSchemaRequestBody(jsonText, "/add_preset", "post", presetAddBody);
    setSchemaRequestBody(jsonText, "/update_preset", "post", presetUpdateBody);
    setSchemaRequestBody(jsonText, "/import_user_dict", "post", userDictImportBody);
    setSchemaRequestBody(jsonText, "/setting", "post", settingBody);
}

static void alignOfficialParameters(std::string &jsonText) {
    setSchemaParameters(jsonText, "/audio_query", "post", R"json(        "parameters": [{"name":"text","in":"query","required":true,"schema":{"type":"string","title":"Text"}},{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"enable_katakana_english","in":"query","required":false,"schema":{"type":"boolean","default":true,"title":"Enable Katakana English"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/audio_query_from_preset", "post", R"json(        "parameters": [{"name":"text","in":"query","required":true,"schema":{"type":"string","title":"Text"}},{"name":"preset_id","in":"query","required":true,"schema":{"type":"integer","title":"Preset Id"}},{"name":"enable_katakana_english","in":"query","required":false,"schema":{"type":"boolean","default":true,"title":"Enable Katakana English"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/accent_phrases", "post", R"json(        "parameters": [{"name":"text","in":"query","required":true,"schema":{"type":"string","title":"Text"}},{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"is_kana","in":"query","required":false,"schema":{"type":"boolean","default":false,"title":"Is Kana"}},{"name":"enable_katakana_english","in":"query","required":false,"schema":{"type":"boolean","default":true,"title":"Enable Katakana English"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/mora_data", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/mora_length", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/mora_pitch", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/synthesis", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"enable_interrogative_upspeak","in":"query","required":false,"schema":{"type":"boolean","description":"疑問系のテキストが与えられたら語尾を自動調整する","default":true,"title":"Enable Interrogative Upspeak"},"description":"疑問系のテキストが与えられたら語尾を自動調整する"},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/cancellable_synthesis", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"enable_interrogative_upspeak","in":"query","required":false,"schema":{"type":"boolean","default":true,"title":"Enable Interrogative Upspeak"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/multi_synthesis", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"enable_interrogative_upspeak","in":"query","required":false,"schema":{"type":"boolean","description":"疑問系のテキストが与えられたら語尾を自動調整する","default":true,"title":"Enable Interrogative Upspeak"},"description":"疑問系のテキストが与えられたら語尾を自動調整する"},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/sing_frame_audio_query", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/sing_frame_f0", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/sing_frame_volume", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/frame_synthesis", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/validate_kana", "post", R"json(        "parameters": [{"name":"text","in":"query","required":true,"schema":{"type":"string","description":"判定する対象の文字列","title":"Text"},"description":"判定する対象の文字列"}])json");
    setSchemaParameters(jsonText, "/initialize_speaker", "post", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"skip_reinit","in":"query","required":false,"schema":{"type":"boolean","description":"既に初期化済みのスタイルの再初期化をスキップするかどうか","default":false,"title":"Skip Reinit"},"description":"既に初期化済みのスタイルの再初期化をスキップするかどうか"},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/is_initialized_speaker", "get", R"json(        "parameters": [{"name":"speaker","in":"query","required":true,"schema":{"type":"integer","title":"Speaker"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/supported_devices", "get", R"json(        "parameters": [{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/morphable_targets", "post", R"json(        "parameters": [{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/synthesis_morphing", "post", R"json(        "parameters": [{"name":"base_speaker","in":"query","required":true,"schema":{"type":"integer","title":"Base Speaker"}},{"name":"target_speaker","in":"query","required":true,"schema":{"type":"integer","title":"Target Speaker"}},{"name":"morph_rate","in":"query","required":true,"schema":{"type":"number","maximum":1.0,"minimum":0.0,"title":"Morph Rate"}},{"name":"enable_interrogative_upspeak","in":"query","required":false,"schema":{"type":"boolean","description":"疑問系のテキストが与えられたら語尾を自動調整する","default":true,"title":"Enable Interrogative Upspeak"},"description":"疑問系のテキストが与えられたら語尾を自動調整する"},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/delete_preset", "post", R"json(        "parameters": [{"name":"id","in":"query","required":true,"schema":{"type":"integer","description":"削除するプリセットのプリセットID","title":"Id"},"description":"削除するプリセットのプリセットID"}])json");
    setSchemaParameters(jsonText, "/speakers", "get", R"json(        "parameters": [{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/speaker_info", "get", R"json(        "parameters": [{"name":"speaker_uuid","in":"query","required":true,"schema":{"type":"string","title":"Speaker Uuid"}},{"name":"resource_format","in":"query","required":false,"schema":{"enum":["base64","url"],"type":"string","default":"base64","title":"Resource Format"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/singers", "get", R"json(        "parameters": [{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/singer_info", "get", R"json(        "parameters": [{"name":"speaker_uuid","in":"query","required":true,"schema":{"type":"string","title":"Speaker Uuid"}},{"name":"resource_format","in":"query","required":false,"schema":{"enum":["base64","url"],"type":"string","default":"base64","title":"Resource Format"}},{"name":"core_version","in":"query","required":false,"schema":{"type":"string","title":"Core Version"}}])json");
    setSchemaParameters(jsonText, "/user_dict_word", "post", R"json(        "parameters": [{"name":"surface","in":"query","required":true,"schema":{"type":"string","description":"言葉の表層形","title":"Surface"},"description":"言葉の表層形"},{"name":"pronunciation","in":"query","required":true,"schema":{"type":"string","description":"言葉の発音（カタカナ）","title":"Pronunciation"},"description":"言葉の発音（カタカナ）"},{"name":"accent_type","in":"query","required":true,"schema":{"type":"integer","description":"アクセント型（音が下がる場所を指す）","title":"Accent Type"},"description":"アクセント型（音が下がる場所を指す）"},{"name":"word_type","in":"query","required":false,"schema":{"$ref":"#/components/schemas/WordTypes","description":"PROPER_NOUN（固有名詞）、COMMON_NOUN（普通名詞）、VERB（動詞）、ADJECTIVE（形容詞）、SUFFIX（語尾）のいずれか"},"description":"PROPER_NOUN（固有名詞）、COMMON_NOUN（普通名詞）、VERB（動詞）、ADJECTIVE（形容詞）、SUFFIX（語尾）のいずれか"},{"name":"priority","in":"query","required":false,"schema":{"type":"integer","description":"単語の優先度（0から10までの整数）。数字が大きいほど優先度が高くなる。1から9までの値を指定することを推奨","maximum":10,"minimum":0,"title":"Priority"},"description":"単語の優先度（0から10までの整数）。数字が大きいほど優先度が高くなる。1から9までの値を指定することを推奨"}])json");
    setSchemaParameters(jsonText, "/user_dict_word/{word_uuid}", "put", R"json(        "parameters": [{"name":"word_uuid","in":"path","required":true,"schema":{"type":"string","description":"更新する言葉のUUID","title":"Word Uuid"},"description":"更新する言葉のUUID"},{"name":"surface","in":"query","required":true,"schema":{"type":"string","description":"言葉の表層形","title":"Surface"},"description":"言葉の表層形"},{"name":"pronunciation","in":"query","required":true,"schema":{"type":"string","description":"言葉の発音（カタカナ）","title":"Pronunciation"},"description":"言葉の発音（カタカナ）"},{"name":"accent_type","in":"query","required":true,"schema":{"type":"integer","description":"アクセント型（音が下がる場所を指す）","title":"Accent Type"},"description":"アクセント型（音が下がる場所を指す）"},{"name":"word_type","in":"query","required":false,"schema":{"$ref":"#/components/schemas/WordTypes","description":"PROPER_NOUN（固有名詞）、COMMON_NOUN（普通名詞）、VERB（動詞）、ADJECTIVE（形容詞）、SUFFIX（語尾）のいずれか"},"description":"PROPER_NOUN（固有名詞）、COMMON_NOUN（普通名詞）、VERB（動詞）、ADJECTIVE（形容詞）、SUFFIX（語尾）のいずれか"},{"name":"priority","in":"query","required":false,"schema":{"type":"integer","description":"単語の優先度（0から10までの整数）。数字が大きいほど優先度が高くなる。1から9までの値を指定することを推奨。","maximum":10,"minimum":0,"title":"Priority"},"description":"単語の優先度（0から10までの整数）。数字が大きいほど優先度が高くなる。1から9までの値を指定することを推奨。"}])json");
    setSchemaParameters(jsonText, "/user_dict_word/{word_uuid}", "delete", R"json(        "parameters": [{"name":"word_uuid","in":"path","required":true,"schema":{"type":"string","description":"削除する言葉のUUID","title":"Word Uuid"},"description":"削除する言葉のUUID"}])json");
    setSchemaParameters(jsonText, "/import_user_dict", "post", R"json(        "parameters": [{"name":"override","in":"query","required":true,"schema":{"type":"boolean","description":"重複したエントリがあった場合、上書きするかどうか","title":"Override"},"description":"重複したエントリがあった場合、上書きするかどうか"}])json");
}

static void appendSchemaComponents(std::string &jsonText) {
    std::string componentsText = R"json(,"components":{"schemas":{"AccentPhrase":{"description":"アクセント句ごとの情報。","properties":{"accent":{"description":"アクセント箇所","title":"Accent","type":"integer"},"is_interrogative":{"default":false,"description":"疑問系かどうか","title":"Is Interrogative","type":"boolean"},"moras":{"description":"モーラのリスト","items":{"$ref":"#/components/schemas/Mora"},"title":"Moras","type":"array"},"pause_mora":{"$ref":"#/components/schemas/Mora","description":"アクセント句の末尾につく無音モーラ。null の場合は無音モーラを付けない。","title":"Pause Mora"}},"required":["moras","accent"],"title":"AccentPhrase","type":"object"},"AudioQuery":{"description":"音声合成用のクエリ。","properties":{"accent_phrases":{"description":"アクセント句のリスト","items":{"$ref":"#/components/schemas/AccentPhrase"},"title":"Accent Phrases","type":"array"},"intonationScale":{"description":"全体の抑揚","title":"Intonationscale","type":"number"},"kana":{"description":"[読み取り専用]AquesTalk 風記法によるテキスト。音声合成用のクエリとしては無視される","title":"Kana","type":"string"},"outputSamplingRate":{"description":"音声データの出力サンプリングレート","title":"Outputsamplingrate","type":"integer"},"outputStereo":{"description":"音声データをステレオ出力するか否か","title":"Outputstereo","type":"boolean"},"pauseLength":{"anyOf":[{"type":"number"},{"type":"null"}],"description":"句読点などの無音時間。nullのときは無視される。デフォルト値はnull","title":"Pauselength"},"pauseLengthScale":{"default":1,"description":"句読点などの無音時間（倍率）。デフォルト値は1","title":"Pauselengthscale","type":"number"},"pitchScale":{"description":"全体の音高","title":"Pitchscale","type":"number"},"postPhonemeLength":{"description":"音声の後の無音時間","title":"Postphonemelength","type":"number"},"prePhonemeLength":{"description":"音声の前の無音時間","title":"Prephonemelength","type":"number"},"speedScale":{"description":"全体の話速","title":"Speedscale","type":"number"},"volumeScale":{"description":"全体の音量","title":"Volumescale","type":"number"}},"required":["accent_phrases","speedScale","pitchScale","intonationScale","volumeScale","prePhonemeLength","postPhonemeLength","outputSamplingRate","outputStereo"],"title":"AudioQuery","type":"object"},"Body_setting_post_setting_post":{"properties":{"allow_origin":{"title":"Allow Origin","type":"string"},"cors_policy_mode":{"$ref":"#/components/schemas/CorsPolicyMode"}},"required":["cors_policy_mode"],"title":"Body_setting_post_setting_post","type":"object"},"Body_sing_frame_f0_sing_frame_f0_post":{"properties":{"frame_audio_query":{"$ref":"#/components/schemas/FrameAudioQuery"},"score":{"$ref":"#/components/schemas/Score"}},"required":["score","frame_audio_query"],"title":"Body_sing_frame_f0_sing_frame_f0_post","type":"object"},"Body_sing_frame_volume_sing_frame_volume_post":{"properties":{"frame_audio_query":{"$ref":"#/components/schemas/FrameAudioQuery"},"score":{"$ref":"#/components/schemas/Score"}},"required":["score","frame_audio_query"],"title":"Body_sing_frame_volume_sing_frame_volume_post","type":"object"},"CorsPolicyMode":{"description":"CORSの許可モード。","enum":["all","localapps"],"title":"CorsPolicyMode","type":"string"},"EngineManifest":{"description":"エンジン自体に関する情報。","properties":{"brand_name":{"description":"ブランド名","title":"Brand Name","type":"string"},"default_sampling_rate":{"description":"デフォルトのサンプリング周波数","title":"Default Sampling Rate","type":"integer"},"dependency_licenses":{"description":"依存関係のライセンス情報","items":{"$ref":"#/components/schemas/LicenseInfo"},"title":"Dependency Licenses","type":"array"},"frame_rate":{"description":"エンジンのフレームレート","title":"Frame Rate","type":"number"},"icon":{"description":"エンジンのアイコンをBASE64エンコードしたもの","title":"Icon","type":"string"},"manifest_version":{"description":"マニフェストのバージョン","title":"Manifest Version","type":"string"},"name":{"description":"エンジン名","title":"Name","type":"string"},"supported_features":{"$ref":"#/components/schemas/SupportedFeatures","description":"エンジンが持つ機能"},"supported_vvlib_manifest_version":{"description":"エンジンが対応するvvlibのバージョン","title":"Supported Vvlib Manifest Version","type":"string"},"terms_of_service":{"description":"エンジンの利用規約","title":"Terms Of Service","type":"string"},"update_infos":{"description":"エンジンのアップデート情報","items":{"$ref":"#/components/schemas/UpdateInfo"},"title":"Update Infos","type":"array"},"url":{"description":"エンジンのURL","title":"Url","type":"string"},"uuid":{"description":"エンジンのUUID","title":"Uuid","type":"string"}},"required":["manifest_version","name","brand_name","uuid","url","icon","default_sampling_rate","frame_rate","terms_of_service","update_infos","dependency_licenses","supported_features"],"title":"EngineManifest","type":"object"},"FrameAudioQuery":{"description":"フレームごとの音声合成用のクエリ。","properties":{"f0":{"description":"フレームごとの基本周波数","items":{"type":"number"},"title":"F0","type":"array"},"outputSamplingRate":{"description":"音声データの出力サンプリングレート","title":"Outputsamplingrate","type":"integer"},"outputStereo":{"description":"音声データをステレオ出力するか否か","title":"Outputstereo","type":"boolean"},"phonemes":{"description":"音素のリスト","items":{"$ref":"#/components/schemas/FramePhoneme"},"title":"Phonemes","type":"array"},"volume":{"description":"フレームごとの音量","items":{"type":"number"},"title":"Volume","type":"array"},"volumeScale":{"description":"全体の音量","title":"Volumescale","type":"number"}},"required":["f0","volume","phonemes","volumeScale","outputSamplingRate","outputStereo"],"title":"FrameAudioQuery","type":"object"},"FramePhoneme":{"description":"音素の情報。","properties":{"frame_length":{"description":"音素のフレーム長","title":"Frame Length","type":"integer"},"note_id":{"anyOf":[{"type":"string"},{"type":"null"}],"description":"音符のID","title":"Note Id"},"phoneme":{"description":"音素","title":"Phoneme","type":"string"}},"required":["phoneme","frame_length"],"title":"FramePhoneme","type":"object"},"HTTPValidationError":{"properties":{"detail":{"items":{"$ref":"#/components/schemas/ValidationError"},"title":"Detail","type":"array"}},"title":"HTTPValidationError","type":"object"},"LicenseInfo":{"description":"依存ライブラリのライセンス情報。","properties":{"license":{"description":"依存ライブラリのライセンス名","title":"License","type":"string"},"name":{"description":"依存ライブラリ名","title":"Name","type":"string"},"text":{"description":"依存ライブラリのライセンス本文","title":"Text","type":"string"},"version":{"description":"依存ライブラリのバージョン","title":"Version","type":"string"}},"required":["name","text"],"title":"LicenseInfo","type":"object"},"Mora":{"description":"モーラ（子音＋母音）ごとの情報。","properties":{"consonant":{"description":"子音の音素","title":"Consonant","type":"string"},"consonant_length":{"description":"子音の長さ","title":"Consonant Length","type":"number"},"pitch":{"description":"音高","title":"Pitch","type":"number"},"text":{"description":"文字","title":"Text","type":"string"},"vowel":{"description":"母音の音素","title":"Vowel","type":"string"},"vowel_length":{"description":"母音の長さ","title":"Vowel Length","type":"number"}},"required":["text","vowel","vowel_length","pitch"],"title":"Mora","type":"object"},"MorphableTargetInfo":{"description":"モーフィング相手としての情報。","properties":{"is_morphable":{"description":"指定したキャラクターに対してモーフィングの可否","title":"Is Morphable","type":"boolean"}},"required":["is_morphable"],"title":"MorphableTargetInfo","type":"object"},"Note":{"description":"音符ごとの情報。","properties":{"frame_length":{"description":"音符のフレーム長","title":"Frame Length","type":"integer"},"id":{"anyOf":[{"type":"string"},{"type":"null"}],"description":"ID","title":"Id"},"key":{"description":"音階","title":"Key","type":"integer"},"lyric":{"description":"音符の歌詞","title":"Lyric","type":"string"}},"required":["frame_length","lyric"],"title":"Note","type":"object"},"ParseKanaBadRequest":{"description":"読み仮名のパースに失敗した。","properties":{"error_args":{"additionalProperties":{"type":"string"},"description":"エラーを起こした箇所","title":"Error Args","type":"object"},"error_name":{"description":"エラー名\n\n|name|description|\n|---|---|\n| UNKNOWN_TEXT | 判別できない読み仮名があります: {text} |\n| ACCENT_TOP | 句頭にアクセントは置けません: {text} |\n| ACCENT_TWICE | 1つのアクセント句に二つ以上のアクセントは置けません: {text} |\n| ACCENT_NOTFOUND | アクセントを指定していないアクセント句があります: {text} |\n| EMPTY_PHRASE | {position}番目のアクセント句が空白です |\n| INTERROGATION_MARK_NOT_AT_END | アクセント句末以外に「？」は置けません: {text} |\n| INFINITE_LOOP | 処理時に無限ループになってしまいました...バグ報告をお願いします。 |","title":"Error Name","type":"string"},"text":{"description":"エラーメッセージ","title":"Text","type":"string"}},"required":["text","error_name","error_args"],"title":"ParseKanaBadRequest","type":"object"},"Preset":{"description":"プリセット情報。","properties":{"id":{"description":"プリセットID","title":"Id","type":"integer"},"intonationScale":{"description":"全体の抑揚","title":"Intonationscale","type":"number"},"name":{"description":"プリセット名","title":"Name","type":"string"},"pauseLength":{"description":"句読点などの無音時間","title":"Pauselength","type":"number"},"pauseLengthScale":{"default":1,"description":"句読点などの無音時間（倍率）","title":"Pauselengthscale","type":"number"},"pitchScale":{"description":"全体の音高","title":"Pitchscale","type":"number"},"postPhonemeLength":{"description":"音声の後の無音時間","title":"Postphonemelength","type":"number"},"prePhonemeLength":{"description":"音声の前の無音時間","title":"Prephonemelength","type":"number"},"speaker_uuid":{"description":"キャラクターのUUID","title":"Speaker Uuid","type":"string"},"speedScale":{"description":"全体の話速","title":"Speedscale","type":"number"},"style_id":{"description":"スタイルID","title":"Style Id","type":"integer"},"volumeScale":{"description":"全体の音量","title":"Volumescale","type":"number"}},"required":["id","name","speaker_uuid","style_id","speedScale","pitchScale","intonationScale","volumeScale","prePhonemeLength","postPhonemeLength"],"title":"Preset","type":"object"},"Score":{"description":"楽譜情報。","properties":{"notes":{"description":"音符のリスト","items":{"$ref":"#/components/schemas/Note"},"title":"Notes","type":"array"}},"required":["notes"],"title":"Score","type":"object"},"Speaker":{"description":"キャラクター情報","properties":{"name":{"description":"名前","title":"Name","type":"string"},"speaker_uuid":{"description":"キャラクターのUUID","title":"Speaker Uuid","type":"string"},"styles":{"description":"スタイルの一覧","items":{"$ref":"#/components/schemas/SpeakerStyle"},"title":"Styles","type":"array"},"supported_features":{"$ref":"#/components/schemas/SpeakerSupportedFeatures","description":"キャラクターの対応機能"},"version":{"description":"キャラクターのバージョン","title":"Version","type":"string"}},"required":["name","speaker_uuid","styles","version"],"title":"Speaker","type":"object"},"SpeakerInfo":{"description":"キャラクターの追加情報","properties":{"policy":{"description":"policy.md","title":"Policy","type":"string"},"portrait":{"description":"立ち絵画像をbase64エンコードしたもの、あるいはURL","title":"Portrait","type":"string"},"style_infos":{"description":"スタイルの追加情報","items":{"$ref":"#/components/schemas/StyleInfo"},"title":"Style Infos","type":"array"}},"required":["policy","portrait","style_infos"],"title":"SpeakerInfo","type":"object"},"SpeakerStyle":{"description":"キャラクターのスタイル情報","properties":{"id":{"description":"スタイルID","title":"Id","type":"integer"},"name":{"description":"スタイル名","title":"Name","type":"string"},"type":{"default":"talk","description":"スタイルの種類。talk:音声合成クエリの作成と音声合成が可能。singing_teacher:歌唱音声合成用のクエリの作成が可能。frame_decode:歌唱音声合成が可能。sing:歌唱音声合成用のクエリの作成と歌唱音声合成が可能。","enum":["talk","singing_teacher","frame_decode","sing"],"title":"Type","type":"string"}},"required":["name","id"],"title":"SpeakerStyle","type":"object"},"SpeakerSupportedFeatures":{"description":"キャラクターの対応機能の情報","properties":{"permitted_synthesis_morphing":{"default":"ALL","description":"モーフィング機能への対応。'ALL' は「全て許可」、'SELF_ONLY' は「同じキャラクター内でのみ許可」、'NOTHING' は「全て禁止」","enum":["ALL","SELF_ONLY","NOTHING"],"title":"Permitted Synthesis Morphing","type":"string"}},"title":"SpeakerSupportedFeatures","type":"object"},"StyleInfo":{"description":"スタイルの追加情報","properties":{"icon":{"description":"このスタイルのアイコンをbase64エンコードしたもの、あるいはURL","title":"Icon","type":"string"},"id":{"description":"スタイルID","title":"Id","type":"integer"},"portrait":{"description":"このスタイルの立ち絵画像をbase64エンコードしたもの、あるいはURL","title":"Portrait","type":"string"},"voice_samples":{"description":"サンプル音声をbase64エンコードしたもの、あるいはURL","items":{"type":"string"},"title":"Voice Samples","type":"array"}},"required":["id","icon","voice_samples"],"title":"StyleInfo","type":"object"},"SupportedDevicesInfo":{"description":"対応しているデバイスの情報。","properties":{"cpu":{"description":"CPUに対応しているか","title":"Cpu","type":"boolean"},"cuda":{"description":"CUDA(Nvidia GPU)に対応しているか","title":"Cuda","type":"boolean"},"dml":{"description":"DirectML(Nvidia GPU/Radeon GPU等)に対応しているか","title":"Dml","type":"boolean"}},"required":["cpu","cuda","dml"],"title":"SupportedDevicesInfo","type":"object"},"SupportedFeatures":{"description":"エンジンが持つ機能の一覧。","properties":{"adjust_intonation_scale":{"description":"全体の抑揚の調整","title":"Adjust Intonation Scale","type":"boolean"},"adjust_mora_pitch":{"description":"モーラごとの音高の調整","title":"Adjust Mora Pitch","type":"boolean"},"adjust_pause_length":{"description":"句読点などの無音時間の調整","title":"Adjust Pause Length","type":"boolean"},"adjust_phoneme_length":{"description":"音素ごとの長さの調整","title":"Adjust Phoneme Length","type":"boolean"},"adjust_pitch_scale":{"description":"全体の音高の調整","title":"Adjust Pitch Scale","type":"boolean"},"adjust_speed_scale":{"description":"全体の話速の調整","title":"Adjust Speed Scale","type":"boolean"},"adjust_volume_scale":{"description":"全体の音量の調整","title":"Adjust Volume Scale","type":"boolean"},"apply_katakana_english":{"description":"未知の英単語をカタカナ読みに変換","title":"Apply Katakana English","type":"boolean"},"interrogative_upspeak":{"description":"疑問文の自動調整","title":"Interrogative Upspeak","type":"boolean"},"manage_library":{"description":"音声ライブラリのインストール・アンインストール","title":"Manage Library","type":"boolean"},"return_resource_url":{"description":"キャラクター情報のリソースをURLで返送","title":"Return Resource Url","type":"boolean"},"sing":{"description":"歌唱音声合成","title":"Sing","type":"boolean"},"synthesis_morphing":{"description":"2種類のスタイルでモーフィングした音声を合成","title":"Synthesis Morphing","type":"boolean"}},"required":["adjust_mora_pitch","adjust_phoneme_length","adjust_speed_scale","adjust_pitch_scale","adjust_intonation_scale","adjust_volume_scale","interrogative_upspeak","synthesis_morphing"],"title":"SupportedFeatures","type":"object"},"UpdateInfo":{"description":"エンジンのアップデート情報。","properties":{"contributors":{"description":"貢献者名","items":{"type":"string"},"title":"Contributors","type":"array"},"descriptions":{"description":"アップデートの詳細についての説明","items":{"type":"string"},"title":"Descriptions","type":"array"},"version":{"description":"エンジンのバージョン名","title":"Version","type":"string"}},"required":["version","descriptions"],"title":"UpdateInfo","type":"object"},"UserDictWord":{"description":"辞書のコンパイルに使われる情報。","properties":{"accent_associative_rule":{"description":"アクセント結合規則","title":"Accent Associative Rule","type":"string"},"accent_type":{"description":"アクセント型","title":"Accent Type","type":"integer"},"context_id":{"default":1348,"description":"文脈ID","title":"Context Id","type":"integer"},"inflectional_form":{"description":"活用形","title":"Inflectional Form","type":"string"},"inflectional_type":{"description":"活用型","title":"Inflectional Type","type":"string"},"mora_count":{"description":"モーラ数","title":"Mora Count","type":"integer"},"part_of_speech":{"description":"品詞","title":"Part Of Speech","type":"string"},"part_of_speech_detail_1":{"description":"品詞細分類1","title":"Part Of Speech Detail 1","type":"string"},"part_of_speech_detail_2":{"description":"品詞細分類2","title":"Part Of Speech Detail 2","type":"string"},"part_of_speech_detail_3":{"description":"品詞細分類3","title":"Part Of Speech Detail 3","type":"string"},"priority":{"description":"優先度","maximum":10.0,"minimum":0.0,"title":"Priority","type":"integer"},"pronunciation":{"description":"発音","title":"Pronunciation","type":"string"},"stem":{"description":"原形","title":"Stem","type":"string"},"surface":{"description":"表層形","title":"Surface","type":"string"},"yomi":{"description":"読み","title":"Yomi","type":"string"}},"required":["surface","priority","part_of_speech","part_of_speech_detail_1","part_of_speech_detail_2","part_of_speech_detail_3","inflectional_type","inflectional_form","stem","yomi","pronunciation","accent_type","accent_associative_rule"],"title":"UserDictWord","type":"object"},"ValidationError":{"properties":{"loc":{"items":{"anyOf":[{"type":"string"},{"type":"integer"}]},"title":"Location","type":"array"},"msg":{"title":"Message","type":"string"},"type":{"title":"Error Type","type":"string"}},"required":["loc","msg","type"],"title":"ValidationError","type":"object"},"WordTypes":{"description":"品詞","enum":["PROPER_NOUN","COMMON_NOUN","VERB","ADJECTIVE","SUFFIX"],"title":"WordTypes","type":"string"}}})json";
    size_t rootClosePosition = jsonText.rfind("\n}");
    if (rootClosePosition != std::string::npos) {
        jsonText.insert(rootClosePosition, componentsText);
    }
}

std::string createOpenApiJson() {
    std::string jsonText = R"json({
  "openapi": "3.1.0",
  "info": {
    "title": "LiteVox API",
    "version": "0.1.0"
  },
  "paths": {
    "/": {
      "get": {
        "responses": {
          "200": {
            "description": "portal page"
          }
        }
      }
    },
    "/health": {
      "get": {
        "responses": {
          "200": {
            "description": "runtime health"
          }
        }
      }
    },
    "/version": {
      "get": {
        "responses": {
          "200": {
            "description": "engine version"
          }
        }
      }
    },
    "/core_versions": {
      "get": {
        "responses": {
          "200": {
            "description": "core versions"
          }
        }
      }
    },
    "/runtime_info": {
      "get": {
        "responses": {
          "200": {
            "description": "runtime state"
          }
        }
      }
    },
    "/request_queue": {
      "get": {
        "responses": {
          "200": {
            "description": "request queue metrics"
          }
        }
      }
    },
    "/model_assets": {
      "get": {
        "responses": {
          "200": {
            "description": "VVM model asset table"
          }
        }
      }
    },
    "/model_cache": {
      "get": {
        "responses": {
          "200": {
            "description": "model session cache state"
          }
        }
      },
      "post": {
        "responses": {
          "200": {
            "description": "model session cache loaded"
          }
        }
      }
    },
    "/speakers": {
      "get": {
        "parameters": [
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "speaker metas"
          }
        }
      }
    },
    "/speaker_info": {
      "get": {
        "parameters": [
          {
            "name": "speaker_uuid",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "resource_format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "base64",
                "url"
              ],
              "default": "base64"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "speaker assets and style info"
          },
          "404": {
            "description": "speaker uuid was not found"
          }
        }
      }
    },
    "/singer_info": {
      "get": {
        "parameters": [
          {
            "name": "speaker_uuid",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "resource_format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "base64",
                "url"
              ],
              "default": "base64"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "singer assets and style info"
          },
          "404": {
            "description": "singer uuid was not found"
          }
        }
      }
    },
    "/user_dict_word": {
      "get": {
        "responses": {
          "200": {
            "description": "user dictionary words"
          }
        }
      },
      "post": {
        "parameters": [
          {
            "name": "surface",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "pronunciation",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "accent_type",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "word_type",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "priority",
            "in": "query",
            "required": false,
            "schema": {
              "type": "integer"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "created word uuid"
          }
        }
      }
    },
    "/user_dict_word/{word_uuid}": {
      "put": {
        "parameters": [
          {
            "name": "word_uuid",
            "in": "path",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "surface",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "pronunciation",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "accent_type",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "word_type",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "priority",
            "in": "query",
            "required": false,
            "schema": {
              "type": "integer"
            }
          }
        ],
        "responses": {
          "204": {
            "description": "word updated"
          }
        }
      },
      "delete": {
        "parameters": [
          {
            "name": "word_uuid",
            "in": "path",
            "required": true,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "204": {
            "description": "word removed"
          }
        }
      }
    },
    "/user_dict": {
      "get": {
        "responses": {
          "200": {
            "description": "user dictionary words"
          }
        }
      }
    },
    "/import_user_dict": {
      "post": {
        "parameters": [
          {
            "name": "override",
            "in": "query",
            "required": true,
            "schema": {
              "type": "boolean"
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "additionalProperties": true
              }
            }
          }
        },
        "responses": {
          "204": {
            "description": "user dictionary imported"
          },
          "400": {
            "description": "invalid user dictionary json"
          },
          "405": {
            "description": "method not allowed"
          }
        }
      }
    },
    "/singers": {
      "get": {
        "parameters": [
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "singer metas"
          }
        }
      }
    },
    "/models": {
      "get": {
        "responses": {
          "200": {
            "description": "style table"
          }
        }
      }
    },
    "/styles": {
      "get": {
        "responses": {
          "200": {
            "description": "style table"
          }
        }
      }
    },
    "/engine_manifest": {
      "get": {
        "responses": {
          "200": {
            "description": "engine manifest"
          }
        }
      }
    },
    "/supported_devices": {
      "get": {
        "parameters": [
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "supported devices"
          }
        }
      }
    },
    "/downloadable_libraries": {
      "get": {
        "responses": {
          "200": {
            "description": "downloadable voice libraries"
          }
        }
      }
    },
    "/installed_libraries": {
      "get": {
        "responses": {
          "200": {
            "description": "installed voice libraries"
          }
        }
      }
    },
    "/install_library/{library_uuid}": {
      "post": {
        "parameters": [
          {
            "name": "library_uuid",
            "in": "path",
            "required": true,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "204": {
            "description": "library installed"
          },
          "422": {
            "description": "library archive is invalid or mismatched"
          }
        }
      }
    },
    "/uninstall_library/{library_uuid}": {
      "post": {
        "parameters": [
          {
            "name": "library_uuid",
            "in": "path",
            "required": true,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "204": {
            "description": "library uninstalled"
          },
          "403": {
            "description": "bundled library cannot be uninstalled"
          },
          "404": {
            "description": "installed library was not found"
          }
        }
      }
    },
    "/audio_query": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "is_kana",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean"
            }
          },
          {
            "name": "enable_katakana_english",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "audio query"
          }
        }
      }
    },
    "/audio_query_from_accent_phrases": {
      "post": {
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "array",
                "items": {
                  "type": "object"
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "audio query from accent phrases"
          },
          "400": {
            "description": "invalid accent phrases"
          }
        }
      }
    },
    "/audio_query_from_preset": {
      "post": {
        "parameters": [
          {
            "name": "preset_id",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "enable_katakana_english",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "audio query from preset"
          },
          "400": {
            "description": "invalid preset request"
          }
        }
      }
    },
    "/validate_kana": {
      "post": {
        "parameters": [
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "kana is valid"
          },
          "400": {
            "description": "kana is invalid"
          }
        }
      }
    },
    "/connect_waves": {
      "post": {
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "array",
                "items": {
                  "type": "string"
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "connected wav"
          }
        }
      }
    },
    "/multi_synthesis": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "enable_interrogative_upspeak",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "array",
                "items": {
                  "type": "object"
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "zip of synthesized wav files"
          }
        }
      }
    },
    "/sing_frame_audio_query": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "sing frame audio query"
          },
          "501": {
            "description": "sing query is not supported by this backend"
          }
        }
      }
    },
    "/accent_phrases": {
      "post": {
        "parameters": [
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "is_kana",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": false
            }
          },
          {
            "name": "enable_katakana_english",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "accent phrases"
          }
        }
      }
    },
    "/mora_data": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "accent phrases with mora data"
          }
        }
      }
    },
    "/mora_length": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "accent phrases with phoneme length"
          }
        }
      }
    },
    "/mora_pitch": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "accent phrases with mora pitch"
          }
        }
      }
    },
    "/sing_frame_f0": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "sing frame f0"
          },
          "501": {
            "description": "sing f0 prediction is not supported by this backend"
          }
        }
      }
    },
    "/sing_frame_volume": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "sing frame volume"
          },
          "501": {
            "description": "sing volume prediction is not supported by this backend"
          }
        }
      }
    },
    "/synthesis": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "enable_interrogative_upspeak",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "wav",
                "pcm"
              ]
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object"
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "wav or pcm audio"
          },
          "400": {
            "description": "invalid audio query"
          },
          "405": {
            "description": "method not allowed"
          }
        }
      }
    },
    "/frame_synthesis": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object"
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "frame wav or pcm audio"
          },
          "501": {
            "description": "frame synthesis is not supported by this backend"
          }
        }
      }
    },
    "/morphable_targets": {
      "post": {
        "parameters": [
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "array",
                "items": {
                  "type": "integer"
                }
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "morphable target map"
          },
          "400": {
            "description": "invalid style id array"
          }
        }
      }
    },
    "/synthesis_morphing": {
      "post": {
        "parameters": [
          {
            "name": "base_speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "target_speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "morph_rate",
            "in": "query",
            "required": true,
            "schema": {
              "type": "number",
              "minimum": 0,
              "maximum": 1
            }
          },
          {
            "name": "enable_interrogative_upspeak",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object"
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "morphed wav or pcm audio"
          },
          "400": {
            "description": "invalid morphing request"
          },
          "501": {
            "description": "morphing synthesis is not supported by this backend"
          }
        }
      }
    },
    "/cancellable_synthesis": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "enable_interrogative_upspeak",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": true
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object"
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "wav audio with LiteVox cancellation capability headers"
          },
          "400": {
            "description": "invalid request"
          }
        }
      }
    },
    "/synthesis_stream": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "wav",
                "pcm"
              ]
            }
          },
          {
            "name": "chunk_samples",
            "in": "query",
            "required": false,
            "schema": {
              "type": "integer",
              "minimum": 1
            }
          }
        ],
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object"
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "chunked wav or pcm audio"
          },
          "400": {
            "description": "invalid audio query"
          }
        }
      }
    },
    "/tts": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "is_kana",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean"
            }
          },
          {
            "name": "format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "wav",
                "pcm"
              ]
            }
          }
        ],
        "responses": {
          "200": {
            "description": "wav or pcm audio"
          },
          "400": {
            "description": "invalid tts request"
          }
        }
      },
      "get": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "is_kana",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean"
            }
          },
          {
            "name": "format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "wav",
                "pcm"
              ]
            }
          }
        ],
        "responses": {
          "200": {
            "description": "wav or pcm audio"
          },
          "400": {
            "description": "invalid tts request"
          }
        },
        "description": "/tts GET compatible operation"
      }
    },
    "/tts_stream": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "is_kana",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean"
            }
          },
          {
            "name": "format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "wav",
                "pcm"
              ]
            }
          },
          {
            "name": "chunk_samples",
            "in": "query",
            "required": false,
            "schema": {
              "type": "integer",
              "minimum": 1
            }
          }
        ],
        "responses": {
          "200": {
            "description": "chunked wav or pcm audio"
          },
          "400": {
            "description": "invalid streaming request"
          }
        }
      },
      "get": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "text",
            "in": "query",
            "required": true,
            "schema": {
              "type": "string"
            }
          },
          {
            "name": "is_kana",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean"
            }
          },
          {
            "name": "format",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string",
              "enum": [
                "wav",
                "pcm"
              ]
            }
          },
          {
            "name": "chunk_samples",
            "in": "query",
            "required": false,
            "schema": {
              "type": "integer",
              "minimum": 1
            }
          }
        ],
        "responses": {
          "200": {
            "description": "chunked wav or pcm audio"
          },
          "400": {
            "description": "invalid streaming request"
          }
        },
        "description": "/tts_stream GET compatible operation"
      }
    },
    "/presets": {
      "get": {
        "responses": {
          "200": {
            "description": "presets"
          }
        }
      }
    },
    "/add_preset": {
      "post": {
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "required": [
                  "id",
                  "name",
                  "style_id"
                ],
                "properties": {
                  "id": {
                    "type": "integer"
                  },
                  "name": {
                    "type": "string"
                  },
                  "style_id": {
                    "type": "integer"
                  }
                },
                "additionalProperties": true
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "added preset id"
          },
          "400": {
            "description": "invalid preset"
          }
        }
      }
    },
    "/update_preset": {
      "post": {
        "requestBody": {
          "required": true,
          "content": {
            "application/json": {
              "schema": {
                "type": "object",
                "required": [
                  "id",
                  "name",
                  "style_id"
                ],
                "properties": {
                  "id": {
                    "type": "integer"
                  },
                  "name": {
                    "type": "string"
                  },
                  "style_id": {
                    "type": "integer"
                  }
                },
                "additionalProperties": true
              }
            }
          }
        },
        "responses": {
          "200": {
            "description": "updated preset id"
          },
          "400": {
            "description": "invalid preset"
          }
        }
      }
    },
    "/delete_preset": {
      "post": {
        "parameters": [
          {
            "name": "id",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          }
        ],
        "responses": {
          "204": {
            "description": "preset deleted"
          }
        }
      }
    },
    "/setting": {
      "get": {
        "responses": {
          "200": {
            "description": "setting page"
          }
        }
      },
      "post": {
        "requestBody": {
          "required": true,
          "content": {
            "application/x-www-form-urlencoded": {
              "schema": {
                "type": "object",
                "required": [
                  "cors_policy_mode"
                ],
                "properties": {
                  "cors_policy_mode": {
                    "type": "string",
                    "enum": [
                      "all",
                      "localapps"
                    ]
                  },
                  "allow_origin": {
                    "type": "string"
                  }
                }
              }
            }
          }
        },
        "responses": {
          "204": {
            "description": "setting updated"
          },
          "400": {
            "description": "invalid setting"
          }
        }
      }
    },
    "/is_initialized_speaker": {
      "get": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "200": {
            "description": "speaker loaded state"
          }
        }
      }
    },
    "/loaded_models": {
      "get": {
        "responses": {
          "200": {
            "description": "loaded model table"
          }
        }
      }
    },
    "/initialize_speaker": {
      "post": {
        "parameters": [
          {
            "name": "speaker",
            "in": "query",
            "required": true,
            "schema": {
              "type": "integer"
            }
          },
          {
            "name": "skip_reinit",
            "in": "query",
            "required": false,
            "schema": {
              "type": "boolean",
              "default": false
            }
          },
          {
            "name": "core_version",
            "in": "query",
            "required": false,
            "schema": {
              "type": "string"
            }
          }
        ],
        "responses": {
          "204": {
            "description": "speaker loaded"
          }
        }
      }
    },
    "/unload_speaker": {
      "post": {
        "responses": {
          "204": {
            "description": "speaker model unloaded"
          }
        }
      }
    },
    "/open_jtalk/analyze": {
      "post": {
        "responses": {
          "200": {
            "description": "accent phrases from OpenJTalk"
          }
        }
      }
    },
    "/openapi.json": {
      "get": {
        "responses": {
          "200": {
            "description": "OpenAPI schema"
          }
        }
      }
    }
  }
})json";
    alignOfficialResponseStatuses(jsonText);
    alignOfficialResponseSchemas(jsonText);
    alignOfficialRequestBodies(jsonText);
    alignOfficialParameters(jsonText);
    appendSchemaComponents(jsonText);
    return jsonText;
}
