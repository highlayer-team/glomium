#pragma once
#include <node_api.h>
#include "duktape.h"
#include <vector>
// #include <future>
#include <unordered_map>
#include <functional>

struct FunctionContext
{
    napi_env env;
    napi_ref function;
};
struct CallbackData
{
    duk_context *ctx;
    duk_thread_state *st;
};
struct AsyncWorkData
{
    napi_env env;
    duk_idx_t idx;
    napi_deferred deferred;
    bool isWaitingForResolve;
    void *heapPtr;
};

napi_value duk_to_napi(napi_env env, duk_context *ctx, duk_idx_t idx);
void napi_to_duk(napi_env env, duk_context *ctx, napi_value value);
duk_ret_t napi_function_wrapper(duk_context *ctx);
napi_value duk_function_wrapper(napi_env env, napi_callback_info info);
