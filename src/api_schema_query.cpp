#include "api_schema_spec.hpp"

#include <string>

std::string createOpenApiJsonQuerySection() {
    return R"json(
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
)json";
}
