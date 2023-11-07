#include <node_api.h>
#include "duktape.h"
#include <vector>
#include <cstdint>
#include <utility>
#include <cstdlib>
#include <unordered_map>
#include <functional>
#include <iostream>
#include "conversion_utils.h"
#include <assert.h>


std::unordered_map<duk_context *, std::unordered_map<int, FunctionContext>> contextFunctionMap;
std::unordered_map<duk_context *, int> contextFunctionCounters;

void cleanup_context(napi_env env, void *finalize_data, void *finalize_hint)
{
    duk_context *ctx = static_cast<duk_context *>(finalize_data);
    duk_destroy_heap(ctx);
}

napi_value create_context(napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1)
    {
        napi_throw_type_error(env, nullptr, "Expected a configuration object");
        return nullptr;
    }

    uint32_t gas_limit, mem_cost_per_byte;
    napi_value prop_value;

    napi_get_named_property(env, args[0], "gasLimit", &prop_value);
    napi_get_value_uint32(env, prop_value, &gas_limit);

    napi_get_named_property(env, args[0], "memCostPerByte", &prop_value);
    napi_get_value_uint32(env, prop_value, &mem_cost_per_byte);

    auto *gasData = new GasData;
    gasData->gas_limit = gas_limit;
    gasData->gas_used = 0;
    gasData->mem_cost_per_byte = mem_cost_per_byte;
    auto fatal_handler = [](void *udata, const char *msg)
    {
        napi_env env = static_cast<napi_env>(udata);
        napi_throw_error(env, nullptr, msg ? msg : "Duktape fatal error");
    };
    duk_context *ctx = duk_create_heap(duk_gas_respecting_alloc_function, duk_gas_respecting_realloc_function, duk_gas_respecting_free_function, gasData, (duk_fatal_function)fatal_handler);
    if (!ctx)
    {
        delete gasData;
        napi_throw_error(env, nullptr, "Failed to create Duktape context");
        return nullptr;
    }

    duk_push_bare_object(ctx);
    duk_set_global_object(ctx);
    napi_value externalCtx;
    napi_create_external(env, ctx, cleanup_context, nullptr, &externalCtx);

    return externalCtx;
}

napi_value set_global(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 3)
    {
        napi_throw_type_error(env, nullptr, "Expected a context, a property name, and a value");
        return nullptr;
    }

   
    duk_context *ctx;
    napi_get_value_external(env, args[0], (void **)&ctx);

    size_t strSize;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strSize);
    std::vector<char> propName(strSize + 1);
    napi_get_value_string_utf8(env, args[1], propName.data(), strSize + 1, nullptr);

    napi_to_duk(env, ctx, args[2]);
    duk_put_global_string(ctx, propName.data());

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}
napi_value get_global(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2)
    {
        napi_throw_type_error(env, nullptr, "Expected a context and a property name");
        return nullptr;
    }


    duk_context *ctx;
    napi_get_value_external(env, args[0], (void **)&ctx);


    size_t strSize;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strSize);
    std::vector<char> propName(strSize + 1);
    napi_get_value_string_utf8(env, args[1], propName.data(), strSize + 1, nullptr);

    duk_get_global_string(ctx, propName.data());


    napi_value result= duk_to_napi(env, ctx, -1);

    duk_pop(ctx);

    return result;
}
napi_value eval_string(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2)
    {
        napi_throw_type_error(env, nullptr, "Expected a context and a string to evaluate");
        return nullptr;
    }

    void *ctx;
    napi_get_value_external(env, args[0], &ctx);

    size_t str_size;
    char *script;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &str_size);
    script = new char[str_size + 1];
    napi_get_value_string_utf8(env, args[1], script, str_size + 1, nullptr);

    if (duk_peval_string(static_cast<duk_context *>(ctx), script) != 0)
    {
        delete[] script;
        if (duk_is_error(static_cast<duk_context *>(ctx), -1))
        {
            duk_get_prop_string(static_cast<duk_context *>(ctx), -1, "stack");
        }
        const char *error = duk_safe_to_string(static_cast<duk_context *>(ctx), -1);
        napi_throw_error(env, nullptr, error);
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }
    delete[] script; 

    napi_value result = duk_to_napi(env, static_cast<duk_context *>(ctx), -1);
    duk_pop(static_cast<duk_context *>(ctx));

    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    napi_value createContext, evalString, setGlobal, getGlobal;
    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, create_context, nullptr, &createContext);
    napi_set_named_property(env, exports, "createContext", createContext);

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, set_global, nullptr, &setGlobal);
    napi_set_named_property(env, exports, "setGlobal", setGlobal);

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, get_global, nullptr, &getGlobal);
    napi_set_named_property(env, exports, "getGlobal", getGlobal);

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, eval_string, nullptr, &evalString);
    napi_set_named_property(env, exports, "evalString", evalString);

    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
