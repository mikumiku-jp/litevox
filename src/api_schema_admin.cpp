#include "api_schema_spec.hpp"

#include <string>

std::string createOpenApiJsonAdminSection() {
    return R"json(
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
)json";
}
