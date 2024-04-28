#include "napi.h"
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
#include "json.hpp"
#include <chrono>
#include <mutex>
#include <thread>
#include <queue>
#include <setjmp.h>

using json = nlohmann::json;
std::mutex EventCallbacksMapMutex;
std::unordered_map<duk_context *, Napi::ThreadSafeFunction> eventCallbacks;
std::unordered_map<duk_context *, std::unordered_map<int, FunctionContext>> contextFunctionMap;
std::unordered_map<duk_context *, int> contextFunctionCounters;

struct ThreadData
{
    std::thread thread;
    std::queue<std::string> messageQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    bool stopThread = false;
};

std::unordered_map<duk_context *, std::shared_ptr<ThreadData>> contextThreadMap;

std::mutex contextThreadMapMutex;

jmp_buf pre_fatal_state;

void emit_event_callback(duk_context *ctx, const std::string &message);

void cleanup_thread_for_context(duk_context *ctx)
{
    std::shared_ptr<ThreadData> *threadDataPtr = nullptr;

    {
        std::lock_guard<std::mutex> lock(contextThreadMapMutex);
        auto it = contextThreadMap.find(ctx);
        if (it == contextThreadMap.end())
        {
            return;
        }
        threadDataPtr = &(it->second);
    }

    if (!threadDataPtr || !(*threadDataPtr))
    {
        return;
    }

    ThreadData *threadData = threadDataPtr->get();

    {
        std::lock_guard<std::mutex> lock(threadData->queueMutex);
        threadData->stopThread = true;
    }
    threadData->cv.notify_one();

    if (threadData->thread.joinable())
    {
        threadData->thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(contextThreadMapMutex);
        contextThreadMap.erase(ctx);
    }
}

void cleanup_context(napi_env env, void *finalize_data, void *finalize_hint)
{
    duk_context *ctx = static_cast<duk_context *>(finalize_data);
    {
        std::lock_guard<std::mutex> lock(EventCallbacksMapMutex);
        auto it = eventCallbacks.find(ctx);
        if (it != eventCallbacks.end())
        {
            it->second.Release();
            eventCallbacks.erase(it);
            cleanup_thread_for_context(ctx);
            duk_destroy_heap(ctx);
        }
        else
        {
            // std::cout << "Node's GC trying to clean flushed context, already cleaned.";
        }
    }
    auto itcf = contextFunctionMap.find(ctx);
    if (itcf != contextFunctionMap.end())
    {
        contextFunctionMap.erase(itcf);
    }
}

void call_event_callback(Napi::Env env, Napi::Function jsCallback, std::string *data)
{
    jsCallback.Call({Napi::String::New(env, *data)});
    delete data;
}

void fatal_handler(void *udata, const char *msg)
{

    HeapConfig *heapData = (HeapConfig *)udata;
    duk_context *ctx = (duk_context *)heapData->ctx;

    longjmp(pre_fatal_state, 1);
};

duk_context *create_bare_context(GasData *gasData)
{

    auto *initGasData = new GasData;
    initGasData->gas_limit = 999999; // just a big enough value for Duktape to warm up
    initGasData->gas_used = 0;
    initGasData->mem_cost_per_byte = 0;

    auto *heapConfig = new HeapConfig;
    heapConfig->gasConfig = initGasData;

    duk_context *ctx = duk_create_heap(duk_gas_respecting_alloc_function, duk_gas_respecting_realloc_function, duk_gas_respecting_free_function, heapConfig, (duk_fatal_function)fatal_handler);
    heapConfig->ctx = (void *)ctx;
    heapConfig->gasConfig = gasData;
    duk_push_bare_object(ctx);
    duk_set_global_object(ctx);
    return ctx;
}

void create_and_associate_thread(duk_context *ctx)
{
    auto threadData = std::make_shared<ThreadData>();
    std::thread workerThread([ctx, threadData]() mutable {

        
        std::unique_lock<std::mutex> lock(threadData->queueMutex);
        while (!threadData->stopThread) {
            threadData->cv.wait(lock, [threadData](){ return !threadData->messageQueue.empty() || threadData->stopThread; });

            while (!threadData->messageQueue.empty()) {
                std::string message = threadData->messageQueue.front();
                threadData->messageQueue.pop();
                auto msg = json::parse(message, nullptr, false);
                
                auto eventName = msg["event"].get<std::string>();
                if (setjmp(pre_fatal_state)==0){// Handling fatal errors, primarily used for out of gas, other fatal errors shouldn't occur in normal circumstances 
                    if (eventName == "setGlobal")
                    {
                        auto globalValue = msg["globalValue"].dump();

                        json_to_duk(ctx, globalValue);
                        duk_put_global_string(ctx, msg["globalName"].get<std::string>().c_str());
                        emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", true}}.dump());
                    }
                    else if (eventName == "eval")
                    {
                        std::string code = msg["code"].get<std::string>();
                        if (duk_peval_string(ctx, code.c_str()) != 0)
                        {
                            if (duk_is_error(ctx, -1))
                            {
                                duk_get_prop_string(ctx, -1, "stack");
                            }
                            const char *error = duk_safe_to_string(ctx, -1);
                            emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", false}, {"error", std::string(error)}}.dump());
                        }
                        else
                        {
                            json result = duk_to_json(ctx, -1);
                            emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", result}}.dump());
                        }
                        duk_pop(ctx);
                    } else if(eventName=="callFunctionByPointer"){
                        duk_push_heapptr(ctx, reinterpret_cast<void *>(msg["pointer"].get<uintptr_t>()));
                         for (const auto arg : msg["args"])
                             {
                               json_to_duk(ctx,arg.dump());
                            }
                            if (duk_pcall(ctx,msg["args"].size()) != 0)
                                 {
                             emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"error", duk_safe_to_string(ctx, -1)}}.dump());
                            }else{

                            emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", duk_to_json(ctx,-1)}}.dump());
                            duk_pop(ctx);
                        
                            }
                    }
                    else if(eventName == "flushContext")
                    {
                        uint32_t newGasLimit = msg["newGas"]["gasLimit"];
                        uint32_t newMemCostPerByte = msg["newGas"]["memCostPerByte"];

                        auto *newGasData = new GasData();
                        newGasData->gas_limit =9999999;
                        newGasData->mem_cost_per_byte = 0;
                        newGasData->gas_used = 0;

                        auto *newHeapConfig = new HeapConfig();
                        newHeapConfig->gasConfig = newGasData;
                        newHeapConfig->fatal_function = fatal_handler;

                        duk_context *newCtx = duk_create_heap(duk_gas_respecting_alloc_function, duk_gas_respecting_realloc_function, duk_gas_respecting_free_function, newHeapConfig, (duk_fatal_function)fatal_handler);
                        newHeapConfig->ctx = (void *)newCtx;

                        duk_push_bare_object(newCtx);
                        duk_set_global_object(newCtx);

                        newGasData->gas_limit = newGasLimit;
                        newGasData->mem_cost_per_byte = newMemCostPerByte;
                        newGasData->gas_used = 0;
                        emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", reinterpret_cast<uintptr_t>(newCtx)}}.dump());

                        duk_destroy_heap(ctx);

                        ctx = newCtx;
                    }else if(eventName=="getGas"){
                        GasData *gasData = duk_get_gas_info(ctx);
                        emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", {{"gasLimit", gasData->gas_limit}, {"gasUsed", gasData->gas_used}, {"memCostPerByte", gasData->mem_cost_per_byte}}}}.dump());
                    }else if(eventName=="setGas"){
                        GasData *gasData = duk_get_gas_info(ctx);
                        gasData->mem_cost_per_byte = msg["gasData"]["memoryByteCost"];
                        gasData->gas_limit = msg["gasData"]["limit"];
                        gasData->gas_used = msg["gasData"]["used"];
                        emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", {{"gasLimit", gasData->gas_limit}, {"gasUsed", gasData->gas_used}, {"memCostPerByte", gasData->mem_cost_per_byte}}}}.dump());
                    }else if(eventName=="getGlobal"){
                        std::string propName = msg["globalName"];
                        duk_get_global_string(ctx, propName.c_str());

                        json result = duk_to_json( ctx, -1);

                        duk_pop(ctx);

                        emit_event_callback(ctx, json{{"event", "callFinished"}, {"callId", msg["callId"]}, {"result", result}}.dump());
                    }else if(eventName=="setGlobal"){
                         std::string propName = msg["globalName"];
                         json_to_duk(ctx, msg["globalValue"].dump());
                         duk_put_global_string(ctx, propName.c_str());
                    }
                }else{//Fatal error happened during execution
                    // HeapConfig *heapData = (HeapConfig *)ctx->heap->heap_udata;
                    GasData *gasData = duk_get_gas_info(ctx);

                    emit_event_callback(ctx, json{
                        {"event", "fatalError"},
                        {"callId", msg["callId"]},
                        {"gasInfo",{
                            {"gasLimit",gasData->gas_limit},
                            {"gasUsed", gasData->gas_used},
                            {"memCostPerByte",gasData->mem_cost_per_byte}
                            }
                        }
                    }.dump());
                    duk_destroy_heap(ctx);
                    
                    
                }
                   
            }
        }
         });

    workerThread.detach();

    {
        std::lock_guard<std::mutex> lock(contextThreadMapMutex);
        contextThreadMap[ctx] = threadData;
    }
}

void emit_to_thread(duk_context *ctx, const std::string &data)
{
    std::shared_ptr<ThreadData> *threadDataPtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(contextThreadMapMutex);
        auto it = contextThreadMap.find(ctx);
        if (it == contextThreadMap.end())
        {
            std::cerr << "No thread found for the given context" << std::endl;
            return;
        }
        threadDataPtr = &(it->second);
    }

    if (threadDataPtr && *threadDataPtr)
    {

        std::lock_guard<std::mutex> lock((*threadDataPtr)->queueMutex);
        (*threadDataPtr)->messageQueue.push(data);
        (*threadDataPtr)->cv.notify_one();
    }
}

napi_value create_context(napi_env env, napi_callback_info info)
{
    Napi::Env napiEnv(env);
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2)
    {
        napi_throw_type_error(env, nullptr, "Expected a configuration object and a callback");
        return nullptr;
    }

    uint32_t gas_limit, mem_cost_per_byte;
    napi_value prop_value;

    napi_get_named_property(env, args[0], "gasLimit", &prop_value);
    napi_get_value_uint32(env, prop_value, &gas_limit);

    napi_get_named_property(env, args[0], "memCostPerByte", &prop_value);
    napi_get_value_uint32(env, prop_value, &mem_cost_per_byte);

    auto *gasData = new GasData;
    gasData->gas_limit = 999999; // just a big enough value for Duktape to warm up
    gasData->gas_used = 0;
    gasData->mem_cost_per_byte = 0;

    auto *heapConfig = new HeapConfig;
    heapConfig->gasConfig = gasData;

    heapConfig->fatal_function = fatal_handler;

    Napi::Function jsEventCallback = Napi::Value(env, args[1]).As<Napi::Function>();

    Napi::ThreadSafeFunction eventCallback = Napi::ThreadSafeFunction::New(
        napiEnv,
        jsEventCallback,
        "EventCallback",
        0,
        1,
        [](Napi::Env) {});

    duk_context *ctx = duk_create_heap(duk_gas_respecting_alloc_function, duk_gas_respecting_realloc_function, duk_gas_respecting_free_function, heapConfig, (duk_fatal_function)fatal_handler);
    if (!ctx)
    {
        delete gasData;
        delete heapConfig;
        napi_throw_error(env, nullptr, "Failed to create Duktape context");
        return nullptr;
    }
    heapConfig->ctx = (void *)ctx;
    duk_push_bare_object(ctx);
    duk_set_global_object(ctx);
    gasData->gas_limit = gas_limit;
    gasData->mem_cost_per_byte = mem_cost_per_byte;
    gasData->gas_used = 0; // we don't really want to count warmup as a used gas as it's not dependent on usercode

    {
        std::lock_guard<std::mutex> lock(EventCallbacksMapMutex);
        eventCallbacks[ctx] = eventCallback;
    }
    create_and_associate_thread(ctx);
    napi_value externalCtx;
    napi_create_external(env, ctx, nullptr, nullptr, &externalCtx);

    return externalCtx;
}

napi_value call_thread(napi_env env, napi_callback_info info)
{
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2)
    {
        napi_throw_type_error(env, nullptr, "Expected a context and JSON value to pass to thread");
        return nullptr;
    }

    duk_context *ctx;
    napi_get_value_external(env, args[0], (void **)&ctx);

    size_t jsonStrSize;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &jsonStrSize);
    std::string jsonString(jsonStrSize + 1, '\0');
    napi_get_value_string_utf8(env, args[1], jsonString.data(), jsonStrSize + 1, nullptr);

    emit_to_thread(ctx, jsonString);

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}
napi_value swap_contexts(napi_env env, napi_callback_info info)
{
    Napi::Env napiEnv(env);
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2)
    {
        napi_throw_type_error(env, nullptr, "Expected two arguments");
        return nullptr;
    }

    duk_context *oldCtx;
    napi_get_value_external(env, args[0], reinterpret_cast<void **>(&oldCtx));

    int64_t newCtxUInt64;
    napi_get_value_int64(env, args[1], &newCtxUInt64);
    uintptr_t newCtxPtr = static_cast<uintptr_t>(newCtxUInt64);
    duk_context *newCtx = reinterpret_cast<duk_context *>(newCtxPtr);

    {
        std::lock_guard<std::mutex> lock(contextThreadMapMutex);
        auto threadData = contextThreadMap[oldCtx];
        contextThreadMap[newCtx] = threadData;
        contextThreadMap.erase(oldCtx);
    }

    {
        std::lock_guard<std::mutex> lock(EventCallbacksMapMutex);
        auto eventCallback = eventCallbacks[oldCtx];
        // std::cout << "Erased ctx " << (void *)oldCtx << ", new context: " << (void *)newCtx << std::endl;
        eventCallbacks[newCtx] = eventCallback;
        eventCallbacks.erase(oldCtx);
    }

    contextFunctionMap.erase(oldCtx);

    napi_value newExternalCtx;
    napi_create_external(env, newCtx, nullptr, nullptr, &newExternalCtx);

    return newExternalCtx;
}
void emit_event_callback(duk_context *ctx, const std::string &message)
{
    Napi::ThreadSafeFunction callback;

    {
        std::lock_guard<std::mutex> lock(EventCallbacksMapMutex);
        auto it = eventCallbacks.find(ctx);
        if (it != eventCallbacks.end())
        {
            callback = it->second;
        }
    }

    if (callback)
    {
        std::string *data = new std::string(message);
        callback.NonBlockingCall(data, call_event_callback);
    }
}

napi_value notify_waiting_execdata(napi_env env, napi_callback_info info)
{
    size_t argc = 0;
    napi_get_cb_info(env, info, &argc, nullptr, nullptr, nullptr);

    if (argc < 2)
    {
        napi_throw_type_error(env, nullptr, "Expected two arguments: execution data pointer and response string");
        return nullptr;
    }

    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int64_t ptrAsInt;
    napi_get_value_int64(env, args[0], &ptrAsInt);
    NapiFunctionExecutionData *executionData = reinterpret_cast<NapiFunctionExecutionData *>(ptrAsInt);

    size_t strSize;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strSize);
    std::string response(strSize, '\0');
    napi_get_value_string_utf8(env, args[1], response.data(), response.size() + 1, &strSize);

    {
        std::lock_guard<std::mutex> lock(executionData->mtx);
        executionData->response = std::move(response);
        executionData->ready = true;
        executionData->cv.notify_one();
    }

    napi_value result;
    napi_get_undefined(env, &result);
    return result;
}

napi_value Init(napi_env env, napi_value exports)
{
    napi_value createContext, callFunctionByPtr, callThread, swapContexts, notifyWaitingExecData;

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, create_context, nullptr, &createContext);
    napi_set_named_property(env, exports, "createContext", createContext);

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, notify_waiting_execdata, nullptr, &notifyWaitingExecData);
    napi_set_named_property(env, exports, "__notifyWaitingExecData", notifyWaitingExecData);

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, swap_contexts, nullptr, &swapContexts);
    napi_set_named_property(env, exports, "__swapContexts", swapContexts);

    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, call_thread, nullptr, &callThread);
    napi_set_named_property(env, exports, "__callThread", callThread);
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)