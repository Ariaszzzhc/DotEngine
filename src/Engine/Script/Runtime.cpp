module;

#include <quickjs.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

module Dot.Script.Runtime;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Text;
import Dot.Script.Host;
import Dot.Script.Modules;
import Dot.Core.Paths;

namespace Dot::Script {
  namespace {
    std::atomic<std::uint64_t> liveBytes{};
    std::atomic<std::uint64_t> liveAllocations{};
    std::atomic<std::uint64_t> nextRuntime{1};

    auto hostNameValid(std::string_view name) -> bool {
      if (name.empty() || name.size() > 128) {
        return false;
      }

      bool first = true;

      for (const char character : name) {
        const bool letter = (character >= 'a' && character <= 'z')
                            || (character >= 'A' && character <= 'Z') || character == '_';

        if (character == '.') {
          if (first) {
            return false;
          }

          first = true;
        } else if (letter || (!first && character >= '0' && character <= '9')) {
          first = false;
        } else {
          return false;
        }
      }

      return !first;
    }

    auto errorName(Core::ErrorCode code) -> const char* {
      switch (code) {
        case Core::ErrorCode::InvalidArgument:
          return "InvalidArgument";
        case Core::ErrorCode::NotFound:
          return "NotFound";
        case Core::ErrorCode::Io:
          return "Io";
        case Core::ErrorCode::Unavailable:
          return "Unavailable";
        case Core::ErrorCode::InvalidState:
          return "InvalidState";
        case Core::ErrorCode::External:
          return "External";
      }

      return "External";
    }

    auto symbolValid(std::string_view name) -> bool {
      return !name.empty() && name.size() <= 256 && name.find('\0') == std::string_view::npos
             && Core::isValidUtf8(name);
    }

    struct alignas(std::max_align_t) Allocation {
      std::size_t size;
    };

    struct Heap {
      std::uint64_t limit;
      std::uint64_t bytes{};
      std::uint64_t allocations{};
      bool failed{};

      static auto allocate(void* opaque, std::size_t size) noexcept -> void* {
        auto& heap = *static_cast<Heap*>(opaque);

        if (size == 0) {
          return nullptr;
        }

        if (size > heap.limit - heap.bytes
            || size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)) {
          heap.failed = true;
          return nullptr;
        }

        auto* block = static_cast<Allocation*>(std::malloc(size + sizeof(Allocation)));

        if (!block) {
          heap.failed = true;
          return nullptr;
        }

        block->size = size;
        heap.bytes += size;
        ++heap.allocations;
        liveBytes.fetch_add(size);
        liveAllocations.fetch_add(1);
        return block + 1;
      }

      static auto release(void* opaque, void* pointer) noexcept -> void {
        if (!pointer) {
          return;
        }

        auto& heap = *static_cast<Heap*>(opaque);
        auto* block = static_cast<Allocation*>(pointer) - 1;
        heap.bytes -= block->size;
        --heap.allocations;
        liveBytes.fetch_sub(block->size);
        liveAllocations.fetch_sub(1);
        std::free(block);
      }

      static auto resize(void* opaque, void* pointer, std::size_t size) noexcept -> void* {
        if (!pointer) {
          return allocate(opaque, size);
        }

        if (size == 0) {
          release(opaque, pointer);
          return nullptr;
        }

        auto& heap = *static_cast<Heap*>(opaque);
        auto* old = static_cast<Allocation*>(pointer) - 1;
        const auto previous = old->size;

        if (size > heap.limit - (heap.bytes - previous)
            || size > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)) {
          heap.failed = true;
          return nullptr;
        }

        auto* block = static_cast<Allocation*>(std::realloc(old, size + sizeof(Allocation)));

        if (!block) {
          heap.failed = true;
          return nullptr;
        }

        block->size = size;
        heap.bytes = heap.bytes - previous + size;

        if (size >= previous) {
          liveBytes.fetch_add(size - previous);
        } else {
          liveBytes.fetch_sub(previous - size);
        }

        return block + 1;
      }

      static auto zeroed(void* opaque, std::size_t count, std::size_t size) noexcept -> void* {
        if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
          static_cast<Heap*>(opaque)->failed = true;
          return nullptr;
        }

        auto* pointer = allocate(opaque, count * size);

        if (pointer) {
          std::memset(pointer, 0, count * size);
        }

        return pointer;
      }

      static auto usable(const void* pointer) noexcept -> std::size_t {
        return pointer ? (static_cast<const Allocation*>(pointer) - 1)->size : 0;
      }
    };

    struct Value {
      JSContext* context;
      JSValue value;

      Value(JSContext* owner, JSValue owned) : context(owner), value(owned) {
      }

      Value(const Value&) = delete;
      auto operator=(const Value&) -> Value& = delete;

      Value(Value&& other) noexcept : context(other.context), value(other.take()) {
      }

      auto operator=(Value&& other) noexcept -> Value& {
        if (this != &other) {
          JS_FreeValue(context, value);
          context = other.context;
          value = other.take();
        }

        return *this;
      }

      ~Value() {
        JS_FreeValue(context, value);
      }

      auto take() noexcept -> JSValue {
        return std::exchange(value, JS_UNDEFINED);
      }
    };

    struct CString {
      JSContext* context;
      std::size_t size{};
      const char* value;

      CString(JSContext* owner, JSValueConst source)
          : context(owner), value(JS_ToCStringLen(owner, &size, source)) {
      }

      CString(const CString&) = delete;
      auto operator=(const CString&) -> CString& = delete;

      ~CString() {
        JS_FreeCString(context, value);
      }
    };

    auto canonical(std::string_view input) -> Core::Result<std::string> {
      if (!Core::isPortableRelativePath(input)
          || (!input.ends_with(".js") && !input.ends_with(".mjs"))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Entry must be a canonical root-relative .js/.mjs compiled file");
      }

      return std::string(input);
    }

  }  // namespace

  struct Runtime::Impl {
    using Clock = std::chrono::steady_clock;

    struct Entry {
      std::string path;
      Value space;
    };

    struct Rejection {
      Value promise;
      Value reason;
    };

    struct Instance {
      std::string path;
      Value object;
    };

    struct Arguments {
      std::vector<Value> owners;
      std::array<JSValue, 64> values{};
      int count{};
    };

    Core::FileSystem m_files;
    RuntimeConfig m_config;
    std::map<std::string, HostCallback, std::less<>> m_bindings;
    Heap m_heap;
    JSRuntime* m_runtime{};
    JSContext* m_context{};
    std::uint64_t m_identity;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::map<std::uint64_t, Entry> m_entries;
    std::map<std::uint64_t, Instance> m_instances;
    std::uint64_t m_nextInstance{1};
    std::map<std::string, std::uint64_t> m_entryNames;
    std::map<std::string, JSModuleDef*> m_definitions;
    std::set<std::string> m_sources;
    std::vector<Rejection> m_rejections;
    bool m_faulted{};
    bool m_active{};
    bool m_interrupted{};
    bool m_loaderFailed{};
    bool m_trackerFailed{};
    bool m_hostLimitExceeded{};
    std::uint32_t m_hostCalls{};
    std::uint32_t m_jobs{};
    std::string m_loaderError;
    Clock::time_point m_deadline{};

    Impl(Core::FileSystem files, RuntimeConfig config, std::uint64_t identity,
         const HostBindings& bindings)
        : m_files(std::move(files)),
          m_config(config),
          m_heap{config.memoryBytes},
          m_identity(identity) {
      m_rejections.reserve(config.maxJobs);

      for (const auto& binding : bindings) {
        m_bindings.emplace(binding.name, binding.callback);
      }
    }

    ~Impl() {
      if (m_runtime) {
        JS_SetHostPromiseRejectionTracker(m_runtime, nullptr, nullptr);
      }

      m_rejections.clear();
      m_instances.clear();
      m_entries.clear();

      if (m_context) {
        JS_FreeContext(m_context);
      }

      if (m_runtime) {
        JS_FreeRuntime(m_runtime);
      }
    }

    auto check(bool healthy = true) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Script operation requires its owner thread without reentry");
      }

      if (healthy && m_faulted) {
        return Core::fail(Core::ErrorCode::InvalidState, "Script runtime is faulted; recreate it");
      }

      return {};
    }

    struct Execution {
      Impl& owner;

      explicit Execution(Impl& impl) : owner(impl) {
        owner.m_active = true;
        owner.m_interrupted = false;
        owner.m_heap.failed = false;
        owner.m_jobs = 0;
        owner.m_hostCalls = 0;
        owner.m_hostLimitExceeded = false;
        owner.m_deadline = Clock::now() + std::chrono::milliseconds{owner.m_config.milliseconds};
        JS_UpdateStackTop(owner.m_runtime);
      }

      ~Execution() {
        owner.m_active = false;
      }
    };

    static auto interrupt(JSRuntime*, void* opaque) noexcept -> int {
      auto& owner = *static_cast<Impl*>(opaque);

      if (owner.m_active && Clock::now() >= owner.m_deadline) {
        owner.m_interrupted = true;
        return 1;
      }

      return 0;
    }

    static auto normalize(JSContext* context, const char* base, const char* name,
                          void* opaque) noexcept -> char* {
      auto& owner = *static_cast<Impl*>(opaque);

      try {
        auto path = resolveImport(owner.m_files, base ? std::string_view(base) : std::string_view{},
                                  name);

        if (!path) {
          owner.m_loaderFailed = true;
          owner.m_loaderError = path.error().message;
          JS_ThrowReferenceError(context, "%s", owner.m_loaderError.c_str());
          return nullptr;
        }

        return js_strdup(context, path->c_str());
      } catch (...) {
        owner.m_loaderFailed = true;
        JS_ThrowInternalError(context, "Script module normalization failed");
        return nullptr;
      }
    }

    auto compile(const std::string& path) -> Core::Result<Value> {
      const auto projectCount = std::ranges::count_if(
          m_sources, [](const auto& name) { return !name.starts_with("dot:"); });

      if (!path.starts_with("dot:") && projectCount >= m_config.maxModules
          && !m_sources.contains(path)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Script module count exceeds limit");
      }

      auto source = [&]() -> Core::Result<std::string> {
        if (path.starts_with("dot:")) {
          auto builtin = engineModule(path);

          if (!builtin) {
            return std::unexpected(builtin.error());
          }

          return std::string(builtin->source);
        }

        return m_files.readText(std::filesystem::path{std::u8string(path.begin(), path.end())},
                                m_config.sourceBytes);
      }();

      if (!source) {
        return Core::fail(source.error().code,
                          std::format("Module '{}': {}", path, source.error().message));
      }

      if (source->size() > m_config.sourceBytes || !Core::isValidUtf8(*source)
          || source->find('\0') != std::string::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script source must be UTF-8 without NUL");
      }

      m_sources.insert(path);
      Value compiled{m_context, JS_Eval(m_context, source->c_str(), source->size(), path.c_str(),
                                        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY)};

      if (JS_IsException(compiled.value)) {
        return std::unexpected(exception("Compile " + path));
      }

      return compiled;
    }

    static auto loader(JSContext* context, const char* name, void* opaque, JSValueConst) noexcept
        -> JSModuleDef* {
      auto& owner = *static_cast<Impl*>(opaque);

      try {
        if (std::string_view{name} == "dot:host") {
          auto* module = JS_NewCModule(context, "dot:host", hostInit);

          if (!module || JS_AddModuleExport(context, module, "invoke") < 0) {
            owner.m_loaderFailed = true;
            return nullptr;
          }

          owner.m_sources.insert(name);
          owner.m_definitions.emplace(name, module);
          return module;
        }

        auto result = owner.compile(name);

        if (!result) {
          owner.m_loaderFailed = true;
          owner.m_loaderError = result.error().message;
          JS_ThrowReferenceError(context, "%s", owner.m_loaderError.c_str());
          return nullptr;
        }

        auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(result->value));
        owner.m_definitions.emplace(name, module);
        return module;
      } catch (...) {
        owner.m_loaderFailed = true;
        JS_ThrowInternalError(context, "Script module loading failed");
        return nullptr;
      }
    }

    static auto attributes(JSContext* context, void*, JSValueConst attributesValue) noexcept
        -> int {
      if (JS_IsUndefined(attributesValue)) {
        return 0;
      }

      JSPropertyEnum* properties = nullptr;
      std::uint32_t count = 0;

      if (JS_GetOwnPropertyNames(context, &properties, &count, attributesValue,
                                 JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK)
          < 0) {
        return -1;
      }

      JS_FreePropertyEnum(context, properties, count);

      if (count != 0) {
        JS_ThrowTypeError(context, "Script import attributes are unsupported");
        return -1;
      }

      return 0;
    }

    static auto rejection(JSContext* context, JSValueConst promise, JSValueConst reason,
                          bool handled, void* opaque) noexcept -> void {
      auto& owner = *static_cast<Impl*>(opaque);

      try {
        const auto found = std::ranges::find_if(owner.m_rejections, [&](const Rejection& value) {
          return JS_VALUE_GET_PTR(value.promise.value) == JS_VALUE_GET_PTR(promise);
        });

        if (handled) {
          if (found != owner.m_rejections.end()) {
            owner.m_rejections.erase(found);
          }
        } else if (found == owner.m_rejections.end()) {
          if (owner.m_rejections.size() >= owner.m_config.maxJobs) {
            owner.m_trackerFailed = true;
            return;
          }

          owner.m_rejections.push_back({Value{context, JS_DupValue(context, promise)},
                                        Value{context, JS_DupValue(context, reason)}});
        }
      } catch (...) {
        owner.m_trackerFailed = true;
      }
    }

    auto diagnostic(JSValueConst reason) -> std::string {
      std::string message = "JavaScript exception (diagnostic unavailable)";
      CString text{m_context, reason};

      if (text.value && text.size <= 16384
          && Core::isValidUtf8(std::string_view{text.value, text.size})) {
        message.assign(text.value, text.size);
      }

      if (JS_IsError(reason)) {
        Value stack{m_context, JS_GetPropertyStr(m_context, reason, "stack")};
        if (!JS_IsException(stack.value)) {
          CString trace{m_context, stack.value};

          if (trace.value && trace.size <= 16384
              && Core::isValidUtf8(std::string_view{trace.value, trace.size})) {
            message += "\n";
            message.append(trace.value, trace.size);
          }
        }
      }

      Value cleared{m_context, JS_GetException(m_context)};
      return message;
    }

    auto failure(std::string_view operation, std::string message) -> Core::Error {
      /** The VM can reject an allocation before invoking the custom allocator.
       * VM 可在调用自定义分配器之前拒绝分配，须保留这个故障诊断。 */
      if (message == "InternalError: out of memory"
          || message.starts_with("InternalError: out of memory\n")) {
        m_heap.failed = true;
      }

      if (m_interrupted || m_heap.failed || m_loaderFailed || m_trackerFailed || m_hostLimitExceeded
          || JS_IsJobPending(m_runtime) || !m_rejections.empty()) {
        m_faulted = true;
      }

      if (m_interrupted) {
        message = "Script execution time limit exceeded";
      } else if (m_heap.failed) {
        message = "Script VM memory limit/allocation failure";
      } else if (m_loaderFailed && !m_loaderError.empty()) {
        message = m_loaderError;
      } else if (m_hostLimitExceeded) {
        message = "Script host-call limit exceeded";
      }

      return Core::Error{Core::ErrorCode::External, std::format("{}: {}", operation, message),
                         std::source_location::current()};
    }

    auto exception(std::string_view operation, JSContext* context = nullptr) -> Core::Error {
      Value reason{context ? context : m_context, JS_GetException(context ? context : m_context)};
      return failure(operation, diagnostic(reason.value));
    }

    auto drain() -> Core::Result<void> {
      while (JS_IsJobPending(m_runtime)) {
        if (m_jobs++ >= m_config.maxJobs) {
          m_faulted = true;
          return Core::fail(Core::ErrorCode::Unavailable, "Script pending-job limit exceeded");
        }

        JSContext* context = nullptr;

        if (JS_ExecutePendingJob(m_runtime, &context) < 0) {
          return std::unexpected(exception("Execute script job", context));
        }
      }

      if (m_trackerFailed || !m_rejections.empty()) {
        m_faulted = true;
        return std::unexpected(failure("Unhandled script rejection",
                                       m_rejections.empty()
                                           ? "Rejection tracking overflow"
                                           : diagnostic(m_rejections.front().reason.value)));
      }

      if (m_loaderFailed || m_heap.failed || m_interrupted || m_hostLimitExceeded
          || Clock::now() >= m_deadline) {
        m_faulted = true;
        m_interrupted = m_interrupted || Clock::now() >= m_deadline;
        return std::unexpected(failure("Script execution", "Runtime resource failure"));
      }

      return {};
    }

    auto settle(Value& value) -> Core::Result<void> {
      if (JS_IsPromise(value.value)) {
        JS_PromiseMarkAsHandled(m_context, value.value);
      }

      auto drained = drain();

      if (!drained) {
        return drained;
      }

      if (JS_IsPromise(value.value)) {
        const auto state = JS_PromiseState(m_context, value.value);

        if (state == JS_PROMISE_PENDING) {
          m_faulted = true;
          return Core::fail(Core::ErrorCode::Unavailable, "Script promise has no completing jobs");
        }

        Value result{m_context, JS_PromiseResult(m_context, value.value)};

        if (state == JS_PROMISE_REJECTED) {
          return std::unexpected(failure("Script promise rejected", diagnostic(result.value)));
        }

        value = std::move(result);
      }

      return {};
    }

    auto argumentsValid(std::string_view arguments) const -> bool {
      return !arguments.empty() && arguments.size() <= m_config.jsonBytes
             && arguments.find('\0') == std::string_view::npos && Core::isValidUtf8(arguments);
    }

    auto instance(InstanceId id) -> Core::Result<Instance*> {
      const auto found = m_instances.find(id.value);

      if (id.runtime != m_identity || found == m_instances.end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Script instance handle is stale or foreign");
      }

      return &found->second;
    }

    auto parseArguments(std::string_view arguments, std::string_view operation)
        -> Core::Result<Arguments> {
      /** The SDK requires termination beyond the view's final byte.
       * SDK 要求视图最后字节之后存在终止符，必须先拥有缓冲。 */
      const std::string json{arguments};
      Value array{m_context, JS_ParseJSON(m_context, json.c_str(), json.size(), "<arguments>")};

      if (JS_IsException(array.value)) {
        return std::unexpected(exception(operation));
      }

      if (!JS_IsArray(array.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script arguments must be a JSON array");
      }

      Value length{m_context, JS_GetPropertyStr(m_context, array.value, "length")};

      if (JS_IsException(length.value)) {
        return std::unexpected(exception(operation));
      }

      std::uint32_t count = 0;

      if (JS_ToUint32(m_context, &count, length.value) < 0 || count > 64) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Script argument count exceeds 64");
      }

      Arguments result;
      result.owners.reserve(count);
      result.count = static_cast<int>(count);

      for (std::uint32_t index = 0; index < count; ++index) {
        result.owners.emplace_back(m_context, JS_GetPropertyUint32(m_context, array.value, index));

        if (JS_IsException(result.owners.back().value)) {
          return std::unexpected(exception(operation));
        }

        result.values[index] = result.owners.back().value;
      }

      return result;
    }

    auto finish(Value result, std::string_view operation)
        -> Core::Result<std::optional<std::string>> {
      auto settled = settle(result);

      if (!settled) {
        return std::unexpected(settled.error());
      }

      if (JS_IsUndefined(result.value)) {
        return std::optional<std::string>{};
      }

      Value replacer{m_context, JS_NewCFunction(m_context, jsonValue, "validateJson", 2)};

      if (JS_IsException(replacer.value)) {
        return std::unexpected(exception(operation));
      }

      Value encoded{m_context,
                    JS_JSONStringify(m_context, result.value, replacer.value, JS_UNDEFINED)};

      if (JS_IsException(encoded.value)) {
        return std::unexpected(exception(operation));
      }

      if (!JS_IsString(encoded.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script result is not JSON-serializable");
      }

      auto drained = drain();

      if (!drained) {
        return std::unexpected(drained.error());
      }

      CString text{m_context, encoded.value};

      if (!text.value) {
        return std::unexpected(exception(operation));
      }

      if (text.size > m_config.jsonBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Script JSON result exceeds limit");
      }

      return std::optional<std::string>{std::string{text.value, text.size}};
    }

    auto invoke(JSValueConst target, JSValueConst receiver, Arguments& arguments,
                std::string_view operation) -> Core::Result<std::optional<std::string>> {
      Value result{m_context,
                   JS_Call(m_context, target, receiver, arguments.count, arguments.values.data())};

      if (JS_IsException(result.value)) {
        return std::unexpected(exception(operation));
      }

      return finish(std::move(result), operation);
    }

    auto transportFailure(std::string_view operation) -> std::unexpected<Core::Error> {
      Value reason{m_context, JS_GetException(m_context)};
      auto message = diagnostic(reason.value);

      if (message == "InternalError: out of memory"
          || message.starts_with("InternalError: out of memory\n")) {
        m_heap.failed = true;
      }

      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("{}: {}", operation, message));
    }

    auto hostArguments(JSValueConst arguments) -> Core::Result<std::string> {
      if (!JS_IsArray(arguments)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Host arguments must be an array");
      }

      Value replacer{m_context, JS_NewCFunction(m_context, jsonValue, "validateHostJson", 2)};

      if (JS_IsException(replacer.value)) {
        return transportFailure("Create host JSON validator");
      }

      Value encoded{m_context,
                    JS_JSONStringify(m_context, arguments, replacer.value, JS_UNDEFINED)};

      if (JS_IsException(encoded.value)) {
        return transportFailure("Encode host arguments");
      }

      if (!JS_IsString(encoded.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Host arguments are not JSON");
      }

      CString text{m_context, encoded.value};

      if (!text.value) {
        return transportFailure("Read host arguments");
      }

      if (text.size > m_config.jsonBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Host argument bytes exceed limit");
      }

      std::string result{text.value, text.size};

      try {
        const auto parsed = nlohmann::json::parse(result);

        if (!parsed.is_array() || parsed.size() > 64) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Host encoded arguments must be an array of at most 64 values");
        }
      } catch (const nlohmann::json::exception& error) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Host arguments require scalar Unicode JSON: " + std::string(error.what()));
      }

      return result;
    }

    static auto hostError(JSContext* context, Core::ErrorCode code, std::string_view method,
                          std::string_view detail) noexcept -> JSValue {
      try {
        auto bounded = detail.substr(0, 8192);

        for (int attempt = 0;
             detail.size() > 8192 && attempt < 4 && !bounded.empty() && !Core::isValidUtf8(bounded);
             ++attempt) {
          bounded.remove_suffix(1);
        }

        if (!Core::isValidUtf8(bounded)) {
          bounded = "Host diagnostic has invalid UTF-8";
        }

        const auto message = std::format("Host {}: {}{}", method, bounded,
                                         detail.size() > bounded.size() ? "..." : "");
        Value error{context, JS_NewError(context)};

        if (JS_IsException(error.value)) {
          return JS_EXCEPTION;
        }

        const auto property = [&](const char* key, std::string_view text) {
          Value value{context, JS_NewStringLen(context, text.data(), text.size())};

          if (JS_IsException(value.value)) {
            return false;
          }

          return JS_DefinePropertyValueStr(context, error.value, key, value.take(), JS_PROP_C_W_E)
                 >= 0;
        };

        if (!property("name", "DotHostError") || !property("code", errorName(code))
            || !property("message", message)) {
          return JS_EXCEPTION;
        }

        return JS_Throw(context, error.take());
      } catch (...) {
        return JS_ThrowInternalError(context, "Host error construction failed");
      }
    }

    static auto hostInvoke(JSContext* context, JSValueConst, int count,
                           JSValueConst* arguments) noexcept -> JSValue {
      auto* owner = static_cast<Impl*>(JS_GetContextOpaque(context));

      if (!owner || !owner->m_active) {
        return JS_ThrowInternalError(context, "Host invocation has no active runtime operation");
      }

      std::string method;

      try {
        method = "invoke";

        if (owner->m_hostCalls >= owner->m_config.maxHostCalls) {
          owner->m_hostLimitExceeded = true;
          return JS_ThrowInternalError(context, "Script host-call limit exceeded");
        }

        ++owner->m_hostCalls;

        if (Clock::now() >= owner->m_deadline) {
          owner->m_interrupted = true;
          return JS_ThrowInternalError(context, "Script execution time limit exceeded");
        }

        if (count != 2 || !JS_IsString(arguments[0])) {
          return hostError(context, Core::ErrorCode::InvalidArgument, method,
                           "invoke requires a method string and argument array");
        }

        CString name{context, arguments[0]};

        if (!name.value) {
          return JS_EXCEPTION;
        }

        if (!hostNameValid(std::string_view{name.value, name.size})) {
          return hostError(context, Core::ErrorCode::InvalidArgument, method,
                           "Invalid method name");
        }

        method.assign(name.value, name.size);
        const auto found = owner->m_bindings.find(method);

        if (found == owner->m_bindings.end()) {
          return hostError(context, Core::ErrorCode::NotFound, method, "Method is not registered");
        }

        auto encoded = owner->hostArguments(arguments[1]);

        if (!encoded) {
          return hostError(context, encoded.error().code, method, encoded.error().message);
        }

        /** Argument getters/toJSON can recursively call the native bridge.
         * 参数 getter/toJSON 可递归调用原生桥；副作用前必须重新检查共享限制。 */
        if (owner->m_hostLimitExceeded || owner->m_heap.failed || owner->m_interrupted
            || Clock::now() >= owner->m_deadline) {
          owner->m_interrupted = owner->m_interrupted || Clock::now() >= owner->m_deadline;
          return JS_ThrowInternalError(context, "Host argument execution exceeded runtime limits");
        }

        auto result = found->second(*encoded);

        if (Clock::now() >= owner->m_deadline) {
          owner->m_interrupted = true;
          return JS_ThrowInternalError(context, "Native host work exceeded execution time limit");
        }

        if (!result) {
          return hostError(context, result.error().code, method, result.error().message);
        }

        if (!result->has_value()) {
          return JS_UNDEFINED;
        }

        const auto& text = **result;

        if (text.empty() || text.size() > owner->m_config.jsonBytes
            || text.find('\0') != std::string::npos || !Core::isValidUtf8(text)) {
          return hostError(context, Core::ErrorCode::External, method,
                           "Invalid host JSON result bytes");
        }

        try {
          static_cast<void>(nlohmann::json::parse(text));
        } catch (const nlohmann::json::exception&) {
          return hostError(context, Core::ErrorCode::External, method,
                           "Invalid finite Unicode host JSON result");
        }

        Value parsed{context, JS_ParseJSON(context, text.c_str(), text.size(), "<host-result>")};

        if (JS_IsException(parsed.value)) {
          auto failure = owner->transportFailure("Parse host result");
          return hostError(context, Core::ErrorCode::External, method, failure.error().message);
        }

        return parsed.take();
      } catch (const std::exception& error) {
        return hostError(context, Core::ErrorCode::External, method, error.what());
      } catch (...) {
        return hostError(context, Core::ErrorCode::External, method,
                         "Non-standard native exception");
      }
    }

    static auto hostInit(JSContext* context, JSModuleDef* module) noexcept -> int {
      Value function{context, JS_NewCFunction(context, hostInvoke, "invoke", 2)};

      if (JS_IsException(function.value)) {
        return -1;
      }

      return JS_SetModuleExport(context, module, "invoke", function.take());
    }

    static auto jsonValue(JSContext* context, JSValueConst, int count,
                          JSValueConst* arguments) noexcept -> JSValue {
      if (count != 2) {
        return JS_ThrowTypeError(context, "Invalid JSON replacer invocation");
      }

      const auto value = arguments[1];
      double number = 0;

      if (JS_IsUndefined(value) || JS_IsSymbol(value) || JS_IsBigInt(value) || JS_IsPromise(value)
          || JS_IsFunction(context, value)
          || (JS_IsNumber(value)
              && (JS_ToFloat64(context, &number, value) < 0 || !std::isfinite(number)))) {
        return JS_ThrowTypeError(context, "Script result contains a non-JSON value");
      }

      if (JS_IsObject(value)) {
        JSPropertyEnum* properties = nullptr;
        std::uint32_t symbols = 0;

        if (JS_GetOwnPropertyNames(context, &properties, &symbols, value,
                                   JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)
            < 0) {
          return JS_EXCEPTION;
        }

        JS_FreePropertyEnum(context, properties, symbols);

        if (symbols != 0) {
          return JS_ThrowTypeError(context, "Script result contains enumerable symbol keys");
        }
      }

      return JS_DupValue(context, value);
    }
  };

  Runtime::Runtime(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Runtime::Runtime(Runtime&&) noexcept = default;

  auto Runtime::operator=(Runtime&&) noexcept -> Runtime& = default;

  Runtime::~Runtime() = default;

  auto Runtime::backendMemory() noexcept -> RuntimeMemory {
    return {liveBytes.load(), liveAllocations.load()};
  }

  auto Runtime::create(const Core::FileSystem& files, const RuntimeConfig& config,
                       const HostBindings& bindings) -> Core::Result<Runtime> {
    if (config.memoryBytes == 0 || config.memoryBytes > 1024ULL * 1024 * 1024
        || config.stackBytes < 64 * 1024 || config.stackBytes > 256 * 1024
        || config.sourceBytes == 0 || config.sourceBytes > 16 * 1024 * 1024 || config.jsonBytes == 0
        || config.jsonBytes > 16 * 1024 * 1024 || config.maxModules == 0 || config.maxModules > 1024
        || config.maxJobs == 0 || config.maxJobs > 65536 || config.milliseconds == 0
        || config.milliseconds > 1000 || config.maxHostCalls == 0 || config.maxHostCalls > 65536
        || bindings.size() > 256 || config.maxInstances == 0 || config.maxInstances > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid script runtime limits");
    }

    auto identity = nextRuntime.load();

    while (identity != std::numeric_limits<std::uint64_t>::max()
           && !nextRuntime.compare_exchange_weak(identity, identity + 1)) {
    }

    if (identity == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime identity exhausted");
    }

    try {
      std::set<std::string_view> names;

      for (const auto& binding : bindings) {
        if ((binding.access != HostAccess::ReadOnly && binding.access != HostAccess::ReadWrite)
            || !hostNameValid(binding.name) || !binding.callback
            || !names.insert(binding.name).second) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Host bindings need unique canonical names, callbacks and valid access");
        }
      }

      auto impl = std::make_unique<Impl>(files, config, identity, bindings);
      const JSMallocFunctions allocator{Heap::zeroed, Heap::allocate, Heap::release, Heap::resize,
                                        Heap::usable};
      impl->m_runtime = JS_NewRuntime2(&allocator, &impl->m_heap);

      if (!impl->m_runtime) {
        return Core::fail(Core::ErrorCode::External,
                          "Initialize script runtime: allocation failure");
      }

      JS_SetMemoryLimit(impl->m_runtime, static_cast<std::size_t>(config.memoryBytes));
      JS_SetMaxStackSize(impl->m_runtime, config.stackBytes);
      JS_SetCanBlock(impl->m_runtime, false);
      JS_SetInterruptHandler(impl->m_runtime, Impl::interrupt, impl.get());
      JS_SetModuleLoaderFunc2(impl->m_runtime, Impl::normalize, Impl::loader, Impl::attributes,
                              impl.get());
      JS_SetHostPromiseRejectionTracker(impl->m_runtime, Impl::rejection, impl.get());
      impl->m_context = JS_NewContext(impl->m_runtime);

      if (!impl->m_context) {
        return Core::fail(Core::ErrorCode::External,
                          "Initialize script context: allocation failure");
      }

      JS_SetContextOpaque(impl->m_context, impl.get());

      return Runtime{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create script runtime: {}", error.what()));
    }
  }

  auto Runtime::validateProgram(const Core::FileSystem& files, std::span<const std::string> modules)
      -> Core::Result<void> {
    RuntimeConfig config;
    config.memoryBytes = 256 * 1024 * 1024;
    config.sourceBytes = 16 * 1024 * 1024;
    config.maxModules = 1024;
    config.milliseconds = 1000;
    auto candidate = Runtime::create(files, config);

    if (!candidate) {
      return std::unexpected(candidate.error());
    }

    try {
      for (const auto& name : modules) {
        auto path = canonical(name);

        if (!path) {
          return std::unexpected(path.error());
        }

        if (candidate->m_impl->m_sources.contains(*path)) {
          continue;
        }

        Impl::Execution execution{*candidate->m_impl};
        auto compiled = candidate->m_impl->compile(*path);

        if (!compiled) {
          return std::unexpected(compiled.error());
        }

        if (JS_ResolveModule(candidate->m_impl->m_context, compiled->value) < 0) {
          return std::unexpected(candidate->m_impl->exception("Resolve " + *path));
        }

        if (Impl::Clock::now() >= candidate->m_impl->m_deadline) {
          return Core::fail(Core::ErrorCode::External,
                            "Validate compiled graph exceeded cooperative time limit: " + *path);
        }
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Validate compiled modules: " + std::string(error.what()));
    }
  }

  auto Runtime::loadModule(std::string_view relative) -> Core::Result<ModuleId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto path = canonical(relative);

    if (!path) {
      return std::unexpected(path.error());
    }

    const auto existing = m_impl->m_entryNames.find(*path);

    if (existing != m_impl->m_entryNames.end()) {
      return ModuleId{m_impl->m_identity, existing->second};
    }

    Impl::Execution execution{*m_impl};

    try {
      auto found = m_impl->m_definitions.find(*path);
      JSModuleDef* module = nullptr;

      if (found == m_impl->m_definitions.end()) {
        auto compiled = m_impl->compile(*path);

        if (!compiled) {
          m_impl->m_faulted = true;
          return std::unexpected(compiled.error());
        }

        module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled->value));
        m_impl->m_definitions.emplace(*path, module);
        Value evaluated{m_impl->m_context, JS_EvalFunction(m_impl->m_context, compiled->take())};

        if (JS_IsException(evaluated.value)) {
          m_impl->m_faulted = true;
          return std::unexpected(m_impl->exception("Evaluate " + *path));
        }

        auto settled = m_impl->settle(evaluated);

        if (!settled) {
          m_impl->m_faulted = true;
          return std::unexpected(settled.error());
        }
      } else {
        module = found->second;
      }

      Value space{m_impl->m_context, JS_GetModuleNamespace(m_impl->m_context, module)};

      if (JS_IsException(space.value)) {
        m_impl->m_faulted = true;
        return std::unexpected(m_impl->exception("Read namespace " + *path));
      }

      const auto value = static_cast<std::uint64_t>(m_impl->m_entries.size()) + 1;
      m_impl->m_entries.emplace(value, Impl::Entry{*path, std::move(space)});
      m_impl->m_entryNames.emplace(*path, value);
      return ModuleId{m_impl->m_identity, value};
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Load script module '{}': {}", relative, error.what()));
    }
  }

  auto Runtime::call(ModuleId module, std::string_view function, std::string_view arguments)
      -> Core::Result<std::optional<std::string>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto found = m_impl->m_entries.find(module.value);

    if (module.runtime != m_impl->m_identity || found == m_impl->m_entries.end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Script module handle is stale or foreign");
    }

    if (function.empty() || function.size() > 256 || function.find('\0') != std::string_view::npos
        || !Core::isValidUtf8(function) || arguments.empty()
        || arguments.find('\0') != std::string_view::npos
        || arguments.size() > m_impl->m_config.jsonBytes || !Core::isValidUtf8(arguments)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid script export name/JSON argument size/encoding");
    }

    Impl::Execution execution{*m_impl};

    try {
      const auto operation = std::format("Call {}::{}", found->second.path, function);
      const std::string name{function};
      Value target{m_impl->m_context,
                   JS_GetPropertyStr(m_impl->m_context, found->second.space.value, name.c_str())};

      if (JS_IsException(target.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      if (!JS_IsFunction(m_impl->m_context, target.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          std::format("{} is not an exported function", operation));
      }

      auto parsed = m_impl->parseArguments(arguments, operation);

      if (!parsed) {
        return std::unexpected(parsed.error());
      }

      return m_impl->invoke(target.value, JS_UNDEFINED, *parsed, operation);
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Call script export: {}", error.what()));
    }
  }

  auto Runtime::createInstance(ModuleId module, std::string_view constructor,
                               std::string_view arguments) -> Core::Result<InstanceId> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto found = m_impl->m_entries.find(module.value);

    if (module.runtime != m_impl->m_identity || found == m_impl->m_entries.end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Script module handle is stale or foreign");
    }

    if (!symbolValid(constructor) || !m_impl->argumentsValid(arguments)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid constructor name/JSON arguments");
    }

    if (m_impl->m_instances.size() >= m_impl->m_config.maxInstances
        || m_impl->m_nextInstance == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Script instance capacity/identity limit exceeded");
    }

    Impl::Execution execution{*m_impl};

    try {
      const auto path = std::format("{}::{}", found->second.path, constructor);
      const auto operation = "Construct " + path;
      auto parsed = m_impl->parseArguments(arguments, operation);

      if (!parsed) {
        return std::unexpected(parsed.error());
      }

      const std::string name{constructor};
      Value target{m_impl->m_context,
                   JS_GetPropertyStr(m_impl->m_context, found->second.space.value, name.c_str())};

      if (JS_IsException(target.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      if (!JS_IsConstructor(m_impl->m_context, target.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, operation + " is not constructable");
      }

      Value object{m_impl->m_context, JS_CallConstructor(m_impl->m_context, target.value,
                                                         parsed->count, parsed->values.data())};

      if (JS_IsException(object.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      const bool promise = JS_IsPromise(object.value);

      if (promise) {
        JS_PromiseMarkAsHandled(m_impl->m_context, object.value);
      }

      auto drained = m_impl->drain();

      if (!drained) {
        return std::unexpected(drained.error());
      }

      if (!JS_IsObject(object.value) || promise) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          operation + " must return a synchronous object");
      }

      const auto value = m_impl->m_nextInstance;
      m_impl->m_instances.emplace(value, Impl::Instance{path, std::move(object)});
      ++m_impl->m_nextInstance;
      return InstanceId{m_impl->m_identity, value};
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Construct script instance: " + std::string(error.what()));
    }
  }

  auto Runtime::callMethod(InstanceId instance, std::string_view method, std::string_view arguments)
      -> Core::Result<std::optional<std::string>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto found = m_impl->instance(instance);

    if (!found) {
      return std::unexpected(found.error());
    }

    if (!symbolValid(method) || !m_impl->argumentsValid(arguments)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid method name/JSON arguments");
    }

    Impl::Execution execution{*m_impl};

    try {
      const auto operation
          = std::format("Method {}[{}].{}", (*found)->path, instance.value, method);
      auto parsed = m_impl->parseArguments(arguments, operation);

      if (!parsed) {
        return std::unexpected(parsed.error());
      }

      const std::string name{method};
      Value target{m_impl->m_context,
                   JS_GetPropertyStr(m_impl->m_context, (*found)->object.value, name.c_str())};

      if (JS_IsException(target.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      if (!JS_IsFunction(m_impl->m_context, target.value)) {
        auto drained = m_impl->drain();

        if (!drained) {
          return std::unexpected(drained.error());
        }

        return Core::fail(Core::ErrorCode::InvalidArgument, operation + " is not callable");
      }

      return m_impl->invoke(target.value, (*found)->object.value, *parsed, operation);
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Call script method: " + std::string(error.what()));
    }
  }

  auto Runtime::hasMethod(InstanceId instance, std::string_view method) -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto found = m_impl->instance(instance);

    if (!found) {
      return std::unexpected(found.error());
    }

    if (!symbolValid(method)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid method name");
    }

    Impl::Execution execution{*m_impl};

    try {
      const auto operation
          = std::format("Discover {}[{}].{}", (*found)->path, instance.value, method);
      const std::string name{method};
      Value target{m_impl->m_context,
                   JS_GetPropertyStr(m_impl->m_context, (*found)->object.value, name.c_str())};

      if (JS_IsException(target.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      auto drained = m_impl->drain();

      if (!drained) {
        return std::unexpected(drained.error());
      }

      if (JS_IsUndefined(target.value)) {
        return false;
      }

      if (!JS_IsFunction(m_impl->m_context, target.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, operation + " is not callable");
      }

      return true;
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        "Discover script method: " + std::string(error.what()));
    }
  }

  auto Runtime::releaseInstance(InstanceId instance) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto found = m_impl->instance(instance);

    if (!found) {
      return std::unexpected(found.error());
    }

    m_impl->m_instances.erase(instance.value);
    return {};
  }

  auto Runtime::stats() const -> Core::Result<RuntimeStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Script runtime has been moved");
    }

    auto checked = m_impl->check(false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return RuntimeStats{{m_impl->m_heap.bytes, m_impl->m_heap.allocations},
                        m_impl->m_definitions.size(),
                        m_impl->m_entries.size(),
                        m_impl->m_faulted,
                        m_impl->m_instances.size()};
  }
}  // namespace Dot::Script
