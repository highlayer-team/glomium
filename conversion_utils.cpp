#include "conversion_utils.h"
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include "json.hpp"

using json = nlohmann::json;

extern std::unordered_map<duk_context *, std::unordered_map<int, FunctionContext>> contextFunctionMap;
extern std::unordered_map<duk_context *, int> contextFunctionCounters;

void emit_event_callback(duk_context *ctx, const std::string &message);

json duk_to_json(duk_context *ctx, duk_idx_t idx)
{
    switch (duk_get_type(ctx, idx))
    {
    case DUK_TYPE_STRING:
        return json(duk_get_string(ctx, idx));

    case DUK_TYPE_NUMBER:
        return json(duk_get_number(ctx, idx));

    case DUK_TYPE_BOOLEAN:
        return json(duk_get_boolean(ctx, idx) != 0);

    case DUK_TYPE_NULL:
    case DUK_TYPE_UNDEFINED:
        return json(nullptr);

    case DUK_TYPE_OBJECT:
        if (duk_is_array(ctx, idx))
        {
            json array = json::array();
            duk_size_t length = duk_get_length(ctx, idx);
            for (duk_size_t i = 0; i < length; i++)
            {
                duk_get_prop_index(ctx, idx, i);
                array.push_back(duk_to_json(ctx, -1));
                duk_pop(ctx);
            }
            return array;
        }
        else if (duk_is_function(ctx, idx))
        {
            duk_dup(ctx, idx);
            void *heapptr = duk_get_heapptr(ctx, -1);
          

            json funcObject = {
                {"__engineInternalProperties", {{"type", "function"}, {"heapptr", reinterpret_cast<uintptr_t>(heapptr)}}}};
            duk_pop(ctx); // Pop the duplicated function
            return funcObject;
        }
        else
        {
            // Handle regular objects
            json jsonObj;
            duk_enum(ctx, idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
            while (duk_next(ctx, -1, true))
            {
                std::string key = duk_get_string(ctx, -2);
                if (key != "__engineInternalProperties")
                { // Skip __engineInternalProperties
                    jsonObj[key] = duk_to_json(ctx, -1);
                }
                duk_pop_2(ctx); // Pop key and value
            }
            duk_pop(ctx); // Pop the enumerator
            return jsonObj;
        }

    default:
        return json(duk_safe_to_string(ctx, idx));
    }
}

void json_to_duk(duk_context *ctx, const std::string &json_str)
{
    json json_obj = json::parse(json_str, nullptr, false);

    if (json_obj.is_discarded())
    {
        // Handle invalid JSON.
        duk_push_error_object(ctx, DUK_ERR_TYPE_ERROR, "Invalid JSON string");
        return;
    }

    std::function<void(const json &)> push_json;
    push_json = [&ctx, &push_json](const json &obj)
    {
        if (obj.is_null())
        {
            duk_push_null(ctx);
        }
        else if (obj.is_boolean())
        {
            duk_push_boolean(ctx, obj.get<bool>());
        }
        else if (obj.is_number())
        {
            duk_push_number(ctx, obj.get<double>());
        }
        else if (obj.is_string())
        {
            duk_push_string(ctx, obj.get<std::string>().c_str());
        }
        else if (obj.is_array())
        {
            duk_push_array(ctx);
            for (size_t i = 0; i < obj.size(); ++i)
            {
                push_json(obj[i]);
                duk_put_prop_index(ctx, -2, i);
            }
        }
        else if (obj.is_object())
        {
            if (obj.contains("__engineInternalProperties") && obj["__engineInternalProperties"].is_object())
            {
                auto internalProps = obj["__engineInternalProperties"];
                if (internalProps.contains("type") && internalProps["type"] == "function" && internalProps.contains("id"))
                {
                    int functionId = internalProps["id"];

                    duk_push_c_function(ctx, napi_function_wrapper, DUK_VARARGS);
                    duk_set_magic(ctx, -1, functionId);
                }
            }
            else
            {
                duk_push_object(ctx);
                for (auto it = obj.begin(); it != obj.end(); ++it)
                {
                    push_json(it.value());
                    duk_put_prop_string(ctx, -2, it.key().c_str());
                }
            }
        }
        else
        {
            // Handle other types if necessary.
            duk_push_undefined(ctx);
        }
    };

    push_json(json_obj);
}


duk_ret_t napi_function_wrapper(duk_context *ctx)
{
    int funcId = duk_get_current_magic(ctx);

    int argCount = duk_get_top(ctx);
    json argsJson = json::array();

    for (int i = 0; i < argCount; ++i)
    {

        argsJson.push_back(duk_to_json(ctx, i));
    }
    auto executionData = std::make_unique<NapiFunctionExecutionData>();

    // Cast pointer to int
    uint64_t ptrAsInt = reinterpret_cast<uint64_t>(executionData.get());

    json callInfo = {
        {"event", "functionCall"},
        {"id", funcId},
        {"args", argsJson},
        {"executionDataPtr", ptrAsInt}
    };

    emit_event_callback(ctx, callInfo.dump());

    {
        std::unique_lock<std::mutex> lock(executionData->mtx);
        executionData->cv.wait(lock, [&executionData]
                               { return executionData->ready; });
    }

    if(executionData->errored){
        (void) duk_error(ctx, DUK_ERR_ERROR, "%s", executionData->response.c_str());
    }

    json_to_duk(ctx, executionData->response);

    executionData->ready = false;

    return 1; 
}
