#pragma once
#include "duktape.h"
#include <vector>
#include <condition_variable>
#include <mutex>
#include <napi.h>
#include <string>
#include <unordered_map>
#include <functional>
#include "json.hpp"

using json = nlohmann::json;

struct FunctionContext
{
    napi_env env;
    napi_ref function;
};


struct NapiFunctionExecutionData
{
    std::condition_variable cv;
    std::mutex mtx;
    bool ready = false;
    std::string response;
    bool errored = false;
};

duk_ret_t napi_function_wrapper(duk_context *ctx);
json duk_to_json(duk_context *ctx, duk_idx_t idx);
void json_to_duk(duk_context *ctx, const std::string &json_str);