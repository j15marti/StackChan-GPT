// SPDX-License-Identifier: MIT
#pragma once
#include <cJSON.h>
#include <string>

// Platform-independent request/response handling, also exercised by host tests.
namespace openai_web_search {
constexpr const char* kToolName = "self__web__search";
constexpr const char* kMcpName = "self.web.search";
cJSON* make_tool(bool flat_format);
// Hosted Responses/Live tool: { "type": "web_search", "external_web_access": true }.
cJSON* make_hosted_tool();
bool make_request(const std::string& arguments, const std::string& model,
                  const std::string& utc_date, std::string& request, std::string& error);
std::string error_result(const std::string& error);
// Returns JSON {ok, answer, sources:[{title,url}]} or {ok:false,error}.
std::string parse_response(const std::string& body);
}  // namespace openai_web_search
