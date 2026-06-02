#include "api_schema_spec.hpp"

#include <string>

std::string createOpenApiJsonSynthesisSection() {
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
)json";
}
