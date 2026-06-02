#include "api_schema_spec.hpp"

#include <string>

std::string createOpenApiJsonModelsSection() {
    return R"json(
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
)json";
}
