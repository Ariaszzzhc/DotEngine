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

namespace Dot::Script {
  namespace {
    std::atomic<std::uint64_t> liveBytes{};
    std::atomic<std::uint64_t> liveAllocations{};
    std::atomic<std::uint64_t> nextRuntime{1};

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

    auto canonical(std::string_view input, std::string_view base = {})
        -> Core::Result<std::string> {
      if (input.empty() || input.size() > 4096 || !Core::isValidUtf8(input)
          || input.find_first_of("\\:<>\"|?*") != std::string_view::npos
          || input.find('\0') != std::string_view::npos || input.front() == '/'
          || input.back() == '/') {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid script module path");
      }

      const bool importing = !base.empty();

      if (importing && !input.starts_with("./") && !input.starts_with("../")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script imports require ./ or ../ paths");
      }

      std::string combined;

      if (importing) {
        const auto slash = base.rfind('/');
        combined = slash == std::string_view::npos ? "" : std::string{base.substr(0, slash + 1)};
      }

      combined += input;
      std::vector<std::string_view> components;
      std::size_t begin = 0;

      while (begin < combined.size()) {
        const auto end = combined.find('/', begin);
        const auto part = std::string_view{combined}.substr(
            begin, end == std::string::npos ? end : end - begin);

        if (part.empty() || (!importing && (part == "." || part == ".."))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Script paths require canonical components");
        }

        if (part == "..") {
          if (components.empty()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Script import escapes project root");
          }

          components.pop_back();
        } else if (part != ".") {
          if (part.back() == '.' || part.back() == ' '
              || std::ranges::any_of(part,
                                     [](unsigned char character) { return character < 32; })) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid script path component");
          }

          components.push_back(part);
        }

        if (end == std::string::npos) {
          break;
        }

        begin = end + 1;
      }

      std::string result;

      for (auto part : components) {
        if (!result.empty()) {
          result += '/';
        }

        result += part;
      }

      if (result.size() > 4096 || (!result.ends_with(".js") && !result.ends_with(".mjs"))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script module must end in .js or .mjs");
      }

      return result;
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

    Core::FileSystem m_files;
    RuntimeConfig m_config;
    Heap m_heap;
    JSRuntime* m_runtime{};
    JSContext* m_context{};
    std::uint64_t m_identity;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::map<std::uint64_t, Entry> m_entries;
    std::map<std::string, std::uint64_t> m_entryNames;
    std::map<std::string, JSModuleDef*> m_definitions;
    std::set<std::string> m_sources;
    std::vector<Rejection> m_rejections;
    bool m_faulted{};
    bool m_active{};
    bool m_interrupted{};
    bool m_loaderFailed{};
    bool m_trackerFailed{};
    std::uint32_t m_jobs{};
    std::string m_loaderError;
    Clock::time_point m_deadline{};

    Impl(Core::FileSystem files, RuntimeConfig config, std::uint64_t identity)
        : m_files(std::move(files)),
          m_config(config),
          m_heap{config.memoryBytes},
          m_identity(identity) {
      m_rejections.reserve(config.maxJobs);
    }

    ~Impl() {
      if (m_runtime) {
        JS_SetHostPromiseRejectionTracker(m_runtime, nullptr, nullptr);
      }

      m_rejections.clear();
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
        auto path = canonical(name, base);

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
      if (m_sources.size() >= m_config.maxModules && !m_sources.contains(path)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Script module count exceeds limit");
      }

      auto source = m_files.readText(std::filesystem::path{std::u8string(path.begin(), path.end())},
                                     m_config.sourceBytes);

      if (!source) {
        return Core::fail(source.error().code,
                          std::format("Module '{}': {}", path, source.error().message));
      }

      if (!Core::isValidUtf8(*source) || source->find('\0') != std::string::npos) {
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

      if (m_interrupted || m_heap.failed || m_loaderFailed || m_trackerFailed
          || JS_IsJobPending(m_runtime) || !m_rejections.empty()) {
        m_faulted = true;
      }

      if (m_interrupted) {
        message = "Script execution time limit exceeded";
      } else if (m_heap.failed) {
        message = "Script VM memory limit/allocation failure";
      } else if (m_loaderFailed && !m_loaderError.empty()) {
        message = m_loaderError;
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

      if (m_loaderFailed || m_heap.failed || m_interrupted || Clock::now() >= m_deadline) {
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

  auto Runtime::create(const Core::FileSystem& files, const RuntimeConfig& config)
      -> Core::Result<Runtime> {
    if (config.memoryBytes == 0 || config.memoryBytes > 1024ULL * 1024 * 1024
        || config.stackBytes < 64 * 1024 || config.stackBytes > 256 * 1024
        || config.sourceBytes == 0 || config.sourceBytes > 16 * 1024 * 1024 || config.jsonBytes == 0
        || config.jsonBytes > 16 * 1024 * 1024 || config.maxModules == 0 || config.maxModules > 1024
        || config.maxJobs == 0 || config.maxJobs > 65536 || config.milliseconds == 0
        || config.milliseconds > 1000) {
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
      auto impl = std::make_unique<Impl>(files, config, identity);
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

      return Runtime{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        std::format("Create script runtime: {}", error.what()));
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

      /** The SDK requires termination beyond the view's final byte.
       * SDK 要求视图最后字节之后存在终止符，必须先拥有缓冲。 */
      const std::string json{arguments};
      Value array{m_impl->m_context,
                  JS_ParseJSON(m_impl->m_context, json.c_str(), json.size(), "<arguments>")};

      if (JS_IsException(array.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      if (!JS_IsArray(array.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script arguments must be a JSON array");
      }

      Value length{m_impl->m_context, JS_GetPropertyStr(m_impl->m_context, array.value, "length")};
      if (JS_IsException(length.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      std::uint32_t count = 0;

      if (JS_ToUint32(m_impl->m_context, &count, length.value) < 0 || count > 64) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Script argument count exceeds 64");
      }

      std::vector<Value> values;
      values.reserve(count);
      std::array<JSValue, 64> argv{};

      for (std::uint32_t index = 0; index < count; ++index) {
        values.emplace_back(m_impl->m_context,
                            JS_GetPropertyUint32(m_impl->m_context, array.value, index));
        if (JS_IsException(values.back().value)) {
          return std::unexpected(m_impl->exception(operation));
        }

        argv[index] = values.back().value;
      }

      Value result{m_impl->m_context, JS_Call(m_impl->m_context, target.value, JS_UNDEFINED,
                                              static_cast<int>(count), argv.data())};

      if (JS_IsException(result.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      auto settled = m_impl->settle(result);

      if (!settled) {
        return std::unexpected(settled.error());
      }

      if (JS_IsUndefined(result.value)) {
        return std::optional<std::string>{};
      }

      Value replacer{m_impl->m_context,
                     JS_NewCFunction(m_impl->m_context, Impl::jsonValue, "validateJson", 2)};
      if (JS_IsException(replacer.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      Value encoded{m_impl->m_context, JS_JSONStringify(m_impl->m_context, result.value,
                                                        replacer.value, JS_UNDEFINED)};

      if (JS_IsException(encoded.value)) {
        return std::unexpected(m_impl->exception(operation));
      }

      if (!JS_IsString(encoded.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Script result is not JSON-serializable");
      }

      auto drained = m_impl->drain();

      if (!drained) {
        return std::unexpected(drained.error());
      }

      CString text{m_impl->m_context, encoded.value};

      if (!text.value) {
        return std::unexpected(m_impl->exception(operation));
      }

      if (text.size > m_impl->m_config.jsonBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Script JSON result exceeds limit");
      }

      return std::optional<std::string>{std::string{text.value, text.size}};
    } catch (const std::exception& error) {
      m_impl->m_faulted = true;
      return Core::fail(Core::ErrorCode::External,
                        std::format("Call script export: {}", error.what()));
    }
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
                        m_impl->m_faulted};
  }
}  // namespace Dot::Script
