#include "api_schema.hpp"
#include "api_schema_align.hpp"
#include "api_schema_internal.hpp"

#include <string>

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
    alignOfficialOpenApiJson(jsonText);
    return jsonText;
}
