#include "conversion_utils.h"
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
#include <cassert>
#include <unordered_map>

extern std::unordered_map<duk_context *, std::unordered_map<int, FunctionContext>> contextFunctionMap;
extern std::unordered_map<duk_context *, int> contextFunctionCounters;

napi_value duk_to_napi(napi_env env, duk_context *ctx, duk_idx_t idx)
{
    napi_value result;

    switch (duk_get_type(ctx, idx))
    {
    case DUK_TYPE_STRING:
        napi_create_string_utf8(env, duk_get_string(ctx, idx), NAPI_AUTO_LENGTH, &result);
        break;

    case DUK_TYPE_NUMBER:
        napi_create_double(env, duk_get_number(ctx, idx), &result);
        break;

    case DUK_TYPE_BOOLEAN:
        napi_get_boolean(env, duk_get_boolean(ctx, idx), &result);
        break;

    case DUK_TYPE_NULL:
        napi_get_null(env, &result);
        break;

    case DUK_TYPE_UNDEFINED:
        napi_get_undefined(env, &result);
        break;

    case DUK_TYPE_OBJECT:
        if (duk_is_array(ctx, idx))
        {
            duk_size_t length = duk_get_length(ctx, idx);
            napi_create_array_with_length(env, length, &result);
            for (duk_size_t i = 0; i < length; i++)
            {
                duk_get_prop_index(ctx, idx, i);
                napi_value elem = duk_to_napi(env, ctx, -1);
                napi_set_element(env, result, i, elem);
                duk_pop(ctx);
            }
        }else 
        if (duk_is_function(ctx, idx))
        {
            duk_dup(ctx, idx);
            auto data = new std::pair<duk_context *, void *>{ctx, duk_get_heapptr(ctx, -1)};
            napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, duk_function_wrapper, data, &result);
        }
        else
        {
            napi_create_object(env, &result);
            duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
            while (duk_next(ctx, -1, true))
            {
                napi_set_named_property(env, result, duk_require_string(ctx, -2), duk_to_napi(env, ctx, -1));
                duk_pop_2(ctx);
            }
            duk_pop(ctx);
        }
        break;

    default:
        napi_create_string_utf8(env, duk_safe_to_string(ctx, idx), NAPI_AUTO_LENGTH, &result);
        break;
    }

    return result;
}

void napi_to_duk(napi_env env, duk_context *ctx, napi_value value)
{
    napi_valuetype type;
    napi_typeof(env, value, &type);

    switch (type)
    {
    case napi_undefined:
        duk_push_undefined(ctx);
        break;

    case napi_null:
        duk_push_null(ctx);
        break;

    case napi_boolean:
    {
        bool boolValue;
        napi_get_value_bool(env, value, &boolValue);
        duk_push_boolean(ctx, boolValue);
        break;
    }

    case napi_number:
    {
        double numValue;
        napi_get_value_double(env, value, &numValue);
        duk_push_number(ctx, numValue);
        break;
    }

    case napi_string:
    {
        size_t strSize;
        napi_get_value_string_utf8(env, value, nullptr, 0, &strSize);
        std::unique_ptr<char[]> str(new char[strSize + 1]);
        napi_get_value_string_utf8(env, value, str.get(), strSize + 1, nullptr);
        duk_push_string(ctx, str.get());
        break;
    }

    case napi_function:
    {
        int &functionCounter = contextFunctionCounters[ctx];
        FunctionContext funcContext = {env, nullptr};
        napi_create_reference(env, value, 1, &funcContext.function);
        contextFunctionMap[ctx][functionCounter] = funcContext;
        duk_push_c_function(ctx, napi_function_wrapper, DUK_VARARGS);
        duk_set_magic(ctx, -1, functionCounter++);
        break;
    }

    case napi_object:
    {
        bool isArray;
        napi_is_array(env, value, &isArray);
        if (isArray)
        {
            uint32_t length;
            napi_get_array_length(env, value, &length);
            duk_push_array(ctx);
            for (uint32_t i = 0; i < length; ++i)
            {
                napi_value elem;
                napi_get_element(env, value, i, &elem);
                napi_to_duk(env, ctx, elem);
                duk_put_prop_index(ctx, -2, i);
            }
        }
        else
        {
            napi_value propNames;
            napi_get_property_names(env, value, &propNames);
            uint32_t length;
            napi_get_array_length(env, propNames, &length);
            duk_push_object(ctx);
            for (uint32_t i = 0; i < length; ++i)
            {
                napi_value propNameNapi, propValue;
                napi_get_element(env, propNames, i, &propNameNapi);
                size_t strSize;
                napi_get_value_string_utf8(env, propNameNapi, nullptr, 0, &strSize);
                std::unique_ptr<char[]> str(new char[strSize + 1]);
                napi_get_value_string_utf8(env, propNameNapi, str.get(), strSize + 1, nullptr);
                napi_get_named_property(env, value, str.get(), &propValue);
                napi_to_duk(env, ctx, propValue);
                duk_put_prop_string(ctx, -2, str.get());
            }
        }
        break;
    }

    default:
        duk_push_undefined(ctx);
        break;
    }
}

duk_ret_t napi_function_wrapper(duk_context *ctx)
{
    int funcId = duk_get_current_magic(ctx);
    FunctionContext funcContext = contextFunctionMap[ctx][funcId];

    int argCount = duk_get_top(ctx);
    std::vector<napi_value> napiArgs(argCount);

    for (int i = 0; i < argCount; ++i)
    {
        napiArgs[i] = duk_to_napi(funcContext.env, ctx, i);
    }

    napi_value function, result, global;
    napi_get_reference_value(funcContext.env, funcContext.function, &function);

    napi_get_global(funcContext.env, &global);
    napi_status status = napi_call_function(funcContext.env, global, function, argCount, napiArgs.data(), &result);

    if (status != napi_ok)
    {
        return duk_error(ctx, DUK_ERR_ERROR, "Error calling function");
    }

    napi_to_duk(funcContext.env, ctx, result);

    return 1;
}

napi_value duk_function_wrapper(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    void *data;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, &data);

    std::vector<napi_value> args(argc);
    napi_get_cb_info(env, info, &argc, args.data(), nullptr, nullptr);

    auto [ctx, func_ref] = *static_cast<std::pair<duk_context *, void *> *>(data);

    duk_push_heapptr(ctx, func_ref);

    for (const auto &arg : args)
    {
        napi_to_duk(env, ctx, arg);
    }

    if (duk_pcall(ctx, argc) != 0)
    {
        napi_throw_error(env, nullptr, duk_safe_to_string(ctx, -1));
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    napi_value result = duk_to_napi(env, ctx, -1);
    duk_pop(ctx);

    return result;
}
