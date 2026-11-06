// SPDX-License-Identifier: MIT
#include "openai_web_search.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void check(bool condition, const char* label)
{
    if (!condition) { std::cerr << "FAILED: " << label << '\n'; std::exit(1); }
}
static bool succeeded(const std::string& result)
{
    cJSON* root = cJSON_Parse(result.c_str());
    check(root != nullptr, "result must be valid JSON");
    bool ok = cJSON_IsTrue(cJSON_GetObjectItem(root, "ok"));
    cJSON_Delete(root);
    return ok;
}
int main()
{
    using namespace openai_web_search;
    std::string request, error;
    for (const char* args : {"{", "{}", "null", R"({"query":2})", R"({"query":"   "})"})
        check(!make_request(args, "gpt-4.1-mini", "", request, error), "reject invalid query");
    check(!make_request("{\"query\":\"" + std::string(1025, 'a') + "\"}", "gpt-4.1-mini", "", request, error), "query size bound");
    check(make_request(R"({"query":"Today's Los Angeles news"})", "gpt-4.1-mini", "2026-11-06", request, error), "build request");
    cJSON* root = cJSON_Parse(request.c_str());
    check(cJSON_IsFalse(cJSON_GetObjectItem(root, "store")), "do not store search");
    check(std::string(cJSON_GetObjectItem(root,"tool_choice")->valuestring) == "required", "search is required");
    cJSON* tool = cJSON_GetArrayItem(cJSON_GetObjectItem(root,"tools"),0);
    check(std::string(cJSON_GetObjectItem(tool,"type")->valuestring) == "web_search", "hosted search tool");
    check(cJSON_IsTrue(cJSON_GetObjectItem(tool,"external_web_access")), "live access");
    cJSON_Delete(root);
    for (bool flat : {false, true}) {
        root = make_tool(flat);
        cJSON* fn = flat ? root : cJSON_GetObjectItem(root, "function");
        check(std::string(cJSON_GetObjectItem(fn, "name")->valuestring) == kToolName, "both voice formats");
        check(cJSON_GetObjectItem(fn, "parameters") != nullptr, "tool schema");
        cJSON_Delete(root);
    }
    // Search items need not come first, and there may be several text blocks.
    const std::string good = R"json({"status":"completed","output":[{"type":"reasoning"},{"type":"message","role":"assistant","content":[{"type":"output_text","text":"News from NASA.","annotations":[{"type":"url_citation","title":"NASA","start_index":5,"end_index":9,"url":"https://www.nasa.gov/news/"},{"type":"url_citation","title":"duplicate","url":"https://www.nasa.gov/news/"},{"type":"url_citation","url":"javascript:alert(1)"}]},{"type":"output_text","text":"Second paragraph."}]},{"type":"web_search_call","status":"completed"}]})json";
    auto parsed = parse_response(good);
    check(succeeded(parsed), "parse sourced result");
    root = cJSON_Parse(parsed.c_str());
    check(cJSON_GetArraySize(cJSON_GetObjectItem(root,"sources")) == 1, "deduplicate sources and reject unsafe links");
    check(std::string(cJSON_GetObjectItem(root,"answer")->valuestring).find("Second paragraph.") != std::string::npos, "all text blocks retained");
    check(cJSON_GetArraySize(cJSON_GetObjectItem(root,"citations")) == 1, "preserve inline citation");
    cJSON_Delete(root);
    for (const char* body : {"{", "null", "{}", R"({"status":"incomplete"})", R"({"status":"failed","error":{"message":"quota exceeded"}})", R"({"status":"completed","output":[]})"})
        check(!succeeded(parse_response(body)), "failed/incomplete response is explicit error");
    auto no_search = good;
    no_search.replace(no_search.find("web_search_call"), 15, "some_other_call");
    check(!succeeded(parse_response(no_search)), "reject answer without actual search");
    auto no_source = good;
    size_t at = 0;
    while ((at = no_source.find("url_citation", at)) != std::string::npos) no_source.replace(at, 12, "other_source");
    check(!succeeded(parse_response(no_source)), "reject unsourced answer");
    check(!succeeded(error_result("quote \" newline\n")), "escape error strings");
    std::cout << "Web search request, parsing, failure and source checks passed.\n";
}
