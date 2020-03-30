#include "hello_async.hpp"
#include "../cpu_intensive_task.hpp"
#include "../module_utils.hpp"

#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>

/**
 * This is an asynchronous standalone function that logs a string.
 * @name helloAsync
 * @param {Object} args - different ways to alter the string
 * @param {boolean} args.louder - adds exclamation points to the string
 * @param {boolean} args.buffer - returns value as a node buffer rather than a string
 * @param {Function} callback - from whence the hello comes, returns a string
 * @returns {string}
 * @example
 * var module = require('./path/to/lib/index.js');
 * module.helloAsync({ louder: true }, function(err, result) {
 *   if (err) throw err;
 *   console.log(result); // => "...threads are busy async bees...hello
 * world!!!!"
 * });
 */

namespace standalone_async {

// This is the worker running asynchronously and calling a user-provided
// callback when done.
// Consider storing all C++ objects you need by value or by shared_ptr to keep
// them alive until done.
// Napi::AsyncWorker docs:
// https://github.com/nodejs/node-addon-api/blob/master/doc/async_worker.md
struct AsyncHelloWorker : Napi::AsyncWorker
{
    using Base = Napi::AsyncWorker;
    // not copyable
    AsyncHelloWorker(AsyncHelloWorker const&) = delete;
    AsyncHelloWorker& operator=(AsyncHelloWorker const&) = delete;
    // ctor
    AsyncHelloWorker(bool louder, bool buffer, Napi::Function const& cb)
        : Base(cb),
          louder_(louder),
          buffer_(buffer) {}

    ~AsyncHelloWorker() {} // empty dtor

    // The Execute() function is getting called when the worker starts to run.
    // - You only have access to member variables stored in this worker.
    // - You do not have access to Javascript v8 objects here.
    void Execute() final
    {
        // The try/catch is critical here: if code was added that could throw an
        // unhandled error INSIDE the threadpool, it would be disasterous
        try
        {
            result_ = detail::do_expensive_work("world", louder_);
        }
        catch (std::exception const& e)
        {
            SetError(e.what());
        }
    }

    // The OnOK() is getting called when Execute() successfully
    // completed.
    // - In case Execute() invoked SetErrorMessage("") this function is not
    // getting called.
    // - You have access to Javascript v8 objects again
    // - You have to translate from C++ member variables to Javascript v8 objects
    // - Finally, you call the user's callback with your results
    void OnOK() final
    {
        Napi::HandleScope scope(Env());
        if (buffer_)
        {
            Callback().Call({Env().Null(), Napi::Buffer<char>::Copy(Env(), result_->data(), result_->size())});
        }
        else
        {
            Callback().Call({Env().Null(), Napi::String::New(Env(), *result_)});
        }
    }

    std::unique_ptr<std::string> result_ = std::make_unique<std::string>();
    const bool louder_;
    const bool buffer_;
};

// helloAsync is a "standalone function" because it's not a class.
// If this function was not defined within a namespace ("standalone_async"
// specified above), it would be in the global scope.
Napi::Value helloAsync(Napi::CallbackInfo const& info)
{
    bool louder = false;
    bool buffer = false;

    // Check second argument, should be a 'callback' function.
    if (!info[1].IsFunction())
    {
        Napi::TypeError::New(info.Env(), "second arg 'callback' must be a function").ThrowAsJavaScriptException();
        return info.Env().Null();
    }

    Napi::Function callback = info[1].As<Napi::Function>();

    // Check first argument, should be an 'options' object
    if (!info[0].IsObject())
    {
        return utils::CallbackError("first arg 'options' must be an object", info);
    }
    Napi::Object options = info[0].As<Napi::Object>();

    // Check options object for the "louder" property, which should be a boolean
    // value
    if (options.Has(Napi::String::New(info.Env(), "louder")))
    {
        Napi::Value louder_val = options.Get(Napi::String::New(info.Env(), "louder"));
        if (!louder_val.IsBoolean())
        {
            return utils::CallbackError("option 'louder' must be a boolean", info);
        }
        louder = louder_val.As<Napi::Boolean>().Value();
    }
    // Check options object for the "buffer" property, which should be a boolean value
    if (options.Has(Napi::String::New(info.Env(), "buffer")))
    {
        Napi::Value buffer_val = options.Get(Napi::String::New(info.Env(), "buffer"));
        if (!buffer_val.IsBoolean())
        {
            return utils::CallbackError("option 'buffer' must be a boolean", info);
        }
        buffer = buffer_val.As<Napi::Boolean>().Value();
    }

    // Creates a worker instance and queues it to run asynchronously, invoking the
    // callback when done.
    // - Napi::AsyncWorker takes a pointer to a Napi::FunctionReference and deletes the
    // pointer automatically.
    // - Napi::AsyncQueueWorker takes a pointer to a Napi::AsyncWorker and deletes
    // the pointer automatically.
    auto* worker = new AsyncHelloWorker{louder, buffer, callback}; // NOLINT
    worker->Queue();
    return info.Env().Undefined(); // NOLINT
}

} // namespace standalone_async
