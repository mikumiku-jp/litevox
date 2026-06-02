#include "api_schema.hpp"
#include "api_schema_align.hpp"
#include "api_schema_spec.hpp"

#include <string>

std::string createOpenApiJson() {
    std::string jsonText = createOpenApiJsonModelsSection();
    jsonText += createOpenApiJsonQuerySection();
    jsonText += createOpenApiJsonSynthesisSection();
    jsonText += createOpenApiJsonAdminSection();
    alignOfficialOpenApiJson(jsonText);
    return jsonText;
}
