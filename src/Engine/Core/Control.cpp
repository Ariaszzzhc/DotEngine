module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Core.Control;

import Dot.Core.StagedDirectory;
import Dot.Core.Text;
import Dot.Core.Paths;

namespace Dot::Core {
  namespace {
    namespace Fs = std::filesystem;
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t requestBytes = 1024 * 1024;
    constexpr std::string_view receiptCache = ".dot-stage-control";

    auto utf(const Fs::path& value) -> std::string {
      const auto bytes = value.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto path(std::string_view value) -> Fs::path {
      return Fs::path(std::u8string(value.begin(), value.end()));
    }

    auto validate(const ControlLimits& limits) -> Result<void> {
      if (limits.responseBytes < 2 || limits.responseBytes > 64 * requestBytes
          || limits.receiptBytes < limits.responseBytes + 6 * requestBytes + 4096
          || limits.receiptBytes > 1024 * requestBytes || limits.sessions == 0
          || limits.sessions > 64) {
        return fail(ErrorCode::InvalidArgument, "Invalid control response/receipt/session limits");
      }

      return {};
    }

    auto validId(std::string_view id) -> bool {
      return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
               || c == '_' || c == '-';
      }) && isPortableRelativePath(std::string(id) + ".json");
    }

    auto folded(std::string value) -> std::string {
      for (auto& c : value) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }

      return value;
    }

    struct ResponseEntry {
      std::string name;
      bool regular;
    };

    struct MailboxIndex {
      std::size_t requestEntries{};
      std::map<std::string, std::string> requests;
      std::map<std::string, ResponseEntry> responses;
      std::vector<Fs::path> pending;
      std::size_t responseBytes{};
    };

    /** Index directory entries once, without resolving every retained receipt.
     * Retained replies require matching regular non-symlink leaves. Actual reads and
     * writes still use FileSystem confinement; callers serialize local access.
     * 每次仅索引一次目录项，不逐项规范化保留回执路径；保留响应须为同名普通非符号链接
     * 文件。实际读写仍通过 FileSystem 限制路径，调用者串行访问本地邮箱。 */
    auto scanReplies(const FileSystem& archive, std::size_t maximum) -> Result<MailboxIndex> {
      if (Fs::symlink_status(archive.root() / "responses").type() != Fs::file_type::directory) {
        return fail(ErrorCode::InvalidArgument,
                    "Control responses must be a non-symlink directory");
      }

      auto responses = archive.resolve("responses");

      if (!responses) {
        return std::unexpected(responses.error());
      }

      MailboxIndex result;
      std::size_t entries{};

      for (const auto& entry : Fs::directory_iterator(*responses)) {
        if (++entries > 4096) {
          return fail(ErrorCode::Unavailable, "Control response entry limit exceeded");
        }

        const auto name = utf(entry.path().filename());
        const auto stem = utf(entry.path().stem());
        const bool regular = Fs::is_regular_file(entry.symlink_status());

        if (!regular || entry.path().extension() != ".json" || !validId(stem)) {
          return fail(ErrorCode::InvalidArgument, "Invalid control response filename/type");
        }

        if (!result.responses.emplace(folded(name), ResponseEntry{name, regular}).second) {
          return fail(ErrorCode::InvalidArgument, "Control response has a case alias");
        }

        const auto size = entry.file_size();

        if (size > maximum - result.responseBytes) {
          return fail(ErrorCode::Unavailable, "Control receipt byte limit exceeded");
        }

        result.responseBytes += static_cast<std::size_t>(size);
      }

      return result;
    }

    auto scan(const FileSystem& root, const FileSystem& archive, std::size_t maximum)
        -> Result<MailboxIndex> {
      auto requests = root.resolve("requests");
      auto indexed = scanReplies(archive, maximum);

      if (!requests || !indexed) {
        return std::unexpected(!requests ? requests.error() : indexed.error());
      }

      auto result = std::move(*indexed);

      for (const auto& entry : Fs::directory_iterator(*requests)) {
        if (++result.requestEntries > 4096) {
          return fail(ErrorCode::Unavailable, "Control session request limit exceeded");
        }

        const auto utf = entry.path().filename().u8string();
        const std::string name(utf.begin(), utf.end());
        const auto normalized = folded(name);

        if (!result.requests.emplace(normalized, name).second) {
          return fail(ErrorCode::InvalidArgument, "Control request has a case alias");
        }

        if (entry.path().extension() != ".json") {
          continue;
        }

        const auto stem = entry.path().stem().u8string();
        const std::string id(stem.begin(), stem.end());

        if (!validId(id) || !Fs::is_regular_file(entry.symlink_status())) {
          return fail(ErrorCode::InvalidArgument, "Invalid control request filename/type");
        }

        const auto previous = result.responses.find(normalized);

        if (previous != result.responses.end()) {
          if (previous->second.name != name) {
            return fail(ErrorCode::InvalidArgument,
                        "Control response spelling differs from request");
          }

          if (!previous->second.regular) {
            return fail(ErrorCode::InvalidArgument, "Invalid control response file type");
          }
        } else {
          result.pending.push_back(entry.path().filename());
        }

        if (result.pending.size() > 256) {
          return fail(ErrorCode::Unavailable, "Control pending request capacity exceeded");
        }
      }

      /** Orphan leaves still consume space; reserve every pending reply before
       * any handler can commit effects.
       * 孤立文件仍占容量；分发任何副作用前须预留全部待处理回复的空间。 */
      if (result.pending.size() > 4096 - result.responses.size()) {
        return fail(ErrorCode::Unavailable,
                    "Control response capacity cannot hold pending replies");
      }

      std::ranges::sort(result.pending);
      return result;
    }

    auto object(std::string_view text, std::size_t maximum) -> Result<Json> {
      if (text.empty() || text.size() > maximum || text.find('\0') != std::string_view::npos
          || !isValidUtf8(text)) {
        return fail(ErrorCode::InvalidArgument, "Control requires bounded UTF-8 JSON without NUL");
      }

      try {
        auto parsed = Codec::parseJson(text);

        if (!parsed.is_object()) {
          return fail(ErrorCode::InvalidArgument, "Control JSON must be an object");
        }

        return parsed;
      } catch (const std::exception& error) {
        return fail(ErrorCode::InvalidArgument, "Control JSON: " + std::string(error.what()));
      }
    }

    auto errorJson(const Error& error) -> Json {
      constexpr std::array names{"InvalidArgument", "NotFound",     "Io",
                                 "Unavailable",     "InvalidState", "External"};
      return {
          {"ok", false},
          {"error",
           {{"code", names.at(static_cast<std::size_t>(error.code))}, {"message", error.message}}}};
    }

    auto faultFrom(const Json& value) -> Result<std::optional<Error>> {
      if (value.is_null()) {
        return std::optional<Error>{};
      }

      Codec::requireFields(value, {"code", "message"});
      constexpr std::array names{"InvalidArgument", "NotFound",     "Io",
                                 "Unavailable",     "InvalidState", "External"};
      const auto found = std::ranges::find(names, value["code"].get<std::string>());

      if (found == names.end() || !value["message"].is_string()) {
        return fail(ErrorCode::InvalidArgument, "Invalid archived control failure");
      }

      return std::optional<Error>{
          fail(static_cast<ErrorCode>(found - names.begin()), value["message"].get<std::string>())
              .error()};
    }

    auto receiptRecord(std::string_view text, const ControlLimits& limits) -> Result<Json> {
      auto value = object(text, limits.responseBytes + 6 * requestBytes + 128);

      if (!value || *value == Json{{"consumed", true}}) {
        return value;
      }

      Codec::requireFields(*value, {"source", "response"});

      if ((!(*value)["source"].is_null() && !(*value)["source"].is_string())
          || ((*value)["source"].is_string()
              && (*value)["source"].get_ref<const std::string&>().size() > requestBytes)
          || !(*value)["response"].is_object()
          || (*value)["response"].dump().size() > limits.responseBytes) {
        return fail(ErrorCode::InvalidArgument, "Malformed control response record");
      }

      return value;
    }

    /** Stage outside responses so client acknowledgements cannot expose atomic temporary files
     * to the product's concurrent receipt scan. Final rename replaces one complete leaf.
     * 在 responses
     * 之外暂存，避免客户端确认的原子临时文件进入产品并发回执扫描；最终重命名替换完整文件。
     */
    auto writeReceipt(const FileSystem& archive, const Fs::path& name, std::string_view text)
        -> Result<void> {
      auto staging = StagedDirectory::create(archive);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto files = staging->files();
      auto written = files ? files->writeTextAtomic("record.json", text)
                           : Result<void>{std::unexpected(files.error())};
      auto target = archive.resolve(Fs::path("responses") / name);

      if (!written || !target) {
        return std::unexpected(!written ? written.error() : target.error());
      }

      std::error_code error;
      Fs::rename(files->root() / "record.json", *target, error);

      if (error) {
        return fail(ErrorCode::Io, "Publish control receipt: " + error.message());
      }

      auto discarded = staging->discard();
      return discarded ? Result<void>{}
                       : fail(discarded.error().code, "Receipt published; staging cleanup failed: "
                                                          + discarded.error().message);
    }

    struct ReceiptArchive {
      FileSystem files;
      Json metadata;
      ControlLimits limits;
    };

    auto archiveAt(const Fs::path& directory) -> Result<ReceiptArchive> {
      if (Fs::symlink_status(directory).type() != Fs::file_type::directory
          || Fs::symlink_status(directory.parent_path()).type() != Fs::file_type::directory
          || Fs::symlink_status(directory / "session.json").type() != Fs::file_type::regular) {
        return fail(ErrorCode::InvalidArgument,
                    "Receipt paths must not be symlinks or special files");
      }

      auto files = FileSystem::open(directory);

      if (!files) {
        return std::unexpected(files.error());
      }

      const auto root = files->root();
      const auto token = utf(root.filename());

      if (root.parent_path().filename() != path(receiptCache) || !validId(token)) {
        return fail(ErrorCode::InvalidArgument, "Not a native control receipt session");
      }

      auto text = files->readText("session.json", 1024 * 1024);
      auto metadata
          = text ? object(*text, 1024 * 1024) : Result<Json>{std::unexpected(text.error())};

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      Codec::requireFields(*metadata, {"protocol", "control", "state", "blocked", "responseBytes",
                                       "receiptBytes", "fault", "uncertain", "unprocessed"});
      const auto expected = root.parent_path().parent_path() / (".dot-stage-" + token);

      if (!(*metadata)["protocol"].is_number_unsigned() || (*metadata)["protocol"] != 1
          || (*metadata)["control"] != utf(expected)
          || ((*metadata)["state"] != "ready" && (*metadata)["state"] != "closed")
          || !(*metadata)["blocked"].is_boolean()
          || !(*metadata)["responseBytes"].is_number_unsigned()
          || !(*metadata)["receiptBytes"].is_number_unsigned()) {
        return fail(ErrorCode::InvalidArgument, "Invalid control receipt provenance/state");
      }

      const auto responseLimit = (*metadata)["responseBytes"].get<std::uint64_t>();
      const auto receiptLimit = (*metadata)["receiptBytes"].get<std::uint64_t>();

      if (responseLimit > 64 * requestBytes || receiptLimit > 1024 * requestBytes) {
        return fail(ErrorCode::InvalidArgument, "Invalid control receipt capacities");
      }

      ControlLimits limits{static_cast<std::size_t>(responseLimit),
                           static_cast<std::size_t>(receiptLimit), 64};
      auto valid = validate(limits);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto failure = faultFrom((*metadata)["fault"]);
      const auto& pending = (*metadata)["unprocessed"];
      const auto& uncertain = (*metadata)["uncertain"];

      if (!failure || !uncertain.is_array() || uncertain.size() > 4096
          || (!uncertain.empty() && !*failure)
          || (!pending.is_null() && (!pending.is_array() || pending.size() > 256))
          || (pending.is_null() && (!*failure || (*metadata)["state"] != "closed"))) {
        return fail(ErrorCode::InvalidArgument, "Invalid control receipt retirement report");
      }

      std::set<std::string> identities;

      for (const auto* list : {&uncertain, &pending}) {
        if (list->is_array()) {
          for (const auto& id : *list) {
            if (!id.is_string() || !validId(id.get<std::string>())
                || !identities.insert(folded(id.get<std::string>())).second) {
              return fail(ErrorCode::InvalidArgument, "Invalid/duplicate retired control identity");
            }
          }
        }
      }

      return ReceiptArchive{std::move(*files), std::move(*metadata), limits};
    }

  }  // namespace

  struct Control::Impl {
    StagedDirectory m_storage;
    FileSystem m_receipts;
    Json m_metadata;
    Json m_receiptMetadata;
    ControlLimits m_limits;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::optional<Error> m_fault;
    std::optional<std::string> m_uncertain;
    /** Response deletion is loss, never authority to repeat a dispatched operation.
     * 响应删除表示结果丢失，不能成为重放已分发操作的依据。 */
    std::set<std::string> m_dispatched;
    bool m_processing{};
    bool m_exposed{};
    bool m_published{};
    bool m_closing{};
    bool m_retired{};
    bool m_blocked{};

    Impl(StagedDirectory storage, FileSystem receipts, Json metadata, Json receiptMetadata,
         ControlLimits limits)
        : m_storage(std::move(storage)),
          m_receipts(std::move(receipts)),
          m_metadata(std::move(metadata)),
          m_receiptMetadata(std::move(receiptMetadata)),
          m_limits(limits) {
    }

    ~Impl() {
      if (m_published) {
        try {
          auto retired = finish();

          if (retired && !m_exposed && !m_fault && m_dispatched.empty()) {
            auto index = scanReplies(m_receipts, m_limits.receiptBytes);

            if (index && index->responses.empty() && m_receiptMetadata["unprocessed"].is_array()
                && m_receiptMetadata["unprocessed"].empty()) {
              static_cast<void>(clearControlReceipts(m_receipts.root()));
            }
          }
        } catch (...) {
        }
      }
    }

    auto check() const -> Result<void> {
      if (m_thread != std::this_thread::get_id() || m_processing) {
        return fail(ErrorCode::InvalidState, "Control requires its owner without reentry");
      }

      return {};
    }

    auto blocked(bool value) -> Result<void> {
      if (m_blocked == value) {
        return {};
      }

      m_receiptMetadata["blocked"] = value;
      auto saved = m_receipts.writeTextAtomic("session.json", m_receiptMetadata.dump());

      if (saved) {
        m_blocked = value;
      }

      return saved;
    }

    auto finish() -> Result<void> {
      if (m_retired) {
        return {};
      }

      m_closing = true;
      m_receiptMetadata["state"] = "closed";
      m_receiptMetadata["blocked"] = false;
      m_receiptMetadata["fault"] = m_fault ? errorJson(*m_fault)["error"] : Json(nullptr);

      std::set<std::string> uncertain;

      if (m_uncertain) {
        uncertain.insert(*m_uncertain);
      }

      m_receiptMetadata["uncertain"] = Json::array();
      m_receiptMetadata["unprocessed"] = Json::array();
      auto live = m_storage.files();

      std::optional<Error> indexError;

      if (!live) {
        indexError = live.error();
        m_receiptMetadata["unprocessed"] = nullptr;

        if (!m_fault) {
          m_fault = live.error();
        }
      } else {
        auto index = scan(*live, m_receipts, m_limits.receiptBytes);

        if (!index) {
          indexError = index.error();
          m_receiptMetadata["unprocessed"] = nullptr;

          if (!m_fault) {
            m_fault = index.error();
            m_receiptMetadata["fault"] = errorJson(*m_fault)["error"];
          }
        } else {
          for (const auto& request : index->pending) {
            const auto id = utf(request.stem());

            if (m_dispatched.contains(id)) {
              uncertain.insert(id);

              if (!m_fault) {
                m_fault = fail(ErrorCode::InvalidState,
                               "Dispatched control receipt was lost; effects may have committed; "
                               "never replay")
                              .error();
              }
            } else {
              m_receiptMetadata["unprocessed"].push_back(id);
            }
          }
        }
      }

      m_receiptMetadata["uncertain"] = std::vector<std::string>(uncertain.begin(), uncertain.end());
      m_receiptMetadata["fault"] = m_fault ? errorJson(*m_fault)["error"] : Json(nullptr);

      auto marked = m_receipts.writeTextAtomic("session.json", m_receiptMetadata.dump());

      if (!marked) {
        return fail(marked.error().code, "Retain control retirement: " + marked.error().message);
      }

      auto files = m_storage.files();

      if (files) {
        m_metadata["state"] = "closed";
        auto closed = files->writeTextAtomic("session.json", m_metadata.dump());

        if (!closed) {
          return std::unexpected(closed.error());
        }
      }

      auto discarded = m_storage.discard();
      m_retired = discarded.has_value();
      return !discarded   ? discarded
             : indexError ? Result<void>{std::unexpected(*indexError)}
                          : Result<void>{};
    }
  };

  Control::Control(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Control::Control(Control&&) noexcept = default;

  auto Control::operator=(Control&&) noexcept -> Control& = default;

  Control::~Control() = default;

  auto Control::create(const FileSystem& parent, std::string_view metadata,
                       const ControlLimits& limits) -> Result<Control> {
    auto parsed = object(metadata, 64 * 1024 * 1024);
    auto valid = validate(limits);

    if (!parsed || !valid) {
      return std::unexpected(!parsed ? parsed.error() : valid.error());
    }

    if (parsed->contains("protocol") || parsed->contains("state") || parsed->contains("receipts")) {
      return fail(ErrorCode::InvalidArgument,
                  "Control metadata cannot replace protocol/state/receipts");
    }

    try {
      auto created = parent.createDirectories(path(receiptCache));

      if (created
          && Fs::symlink_status(parent.root() / path(receiptCache)).type()
                 != Fs::file_type::directory) {
        return fail(ErrorCode::InvalidArgument, "Receipt cache must be a non-symlink directory");
      }

      auto cache = created ? FileSystem::open(parent.root() / path(receiptCache))
                           : Result<FileSystem>{std::unexpected(created.error())};

      if (!cache) {
        return std::unexpected(cache.error());
      }

      std::size_t entries{};

      for (const auto& entry : Fs::directory_iterator(cache->root())) {
        static_cast<void>(entry);

        if (++entries >= limits.sessions) {
          return fail(ErrorCode::Unavailable, "Control receipt session limit reached in '"
                                                  + utf(cache->root())
                                                  + "'; clear owned closed sessions first");
        }
      }

      auto storage = StagedDirectory::create(parent);
      auto replies = StagedDirectory::create(*cache);

      if (!storage || !replies) {
        return std::unexpected(!storage ? storage.error() : replies.error());
      }

      auto files = storage->files().value();
      auto replyFiles = replies->files().value();
      const auto token = utf(files.root().filename()).substr(std::string(".dot-stage-").size());
      const auto replyPath = cache->root() / token;
      Json replyMetadata{{"protocol", 1},
                         {"control", utf(files.root())},
                         {"state", "ready"},
                         {"blocked", false},
                         {"responseBytes", limits.responseBytes},
                         {"receiptBytes", limits.receiptBytes},
                         {"fault", nullptr},
                         {"uncertain", Json::array()},
                         {"unprocessed", Json::array()}};
      (*parsed)["protocol"] = 1;
      (*parsed)["state"] = "ready";
      (*parsed)["receipts"] = utf(replyPath);
      auto requests = files.createDirectories("requests");
      auto responses = replyFiles.createDirectories("responses");

      if (!requests || !responses) {
        return std::unexpected(!requests ? requests.error() : responses.error());
      }

      auto saved = files.writeTextAtomic("session.json", parsed->dump());
      auto replySaved = replyFiles.writeTextAtomic("session.json", replyMetadata.dump());

      if (!saved || !replySaved) {
        return std::unexpected(!saved ? saved.error() : replySaved.error());
      }

      auto impl = std::make_unique<Impl>(std::move(*storage), std::move(replyFiles),
                                         std::move(*parsed), std::move(replyMetadata), limits);
      auto published = replies->publish(path(token));

      if (!published) {
        return fail(
            published.error().code,
            published.error().message + "; receipt archive may remain at '" + utf(replyPath) + "'");
      }

      impl->m_receipts = std::move(*published);
      impl->m_published = true;
      return Control{std::move(impl)};
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Create control: " + std::string(error.what()));
    }
  }

  auto Control::files() const -> Result<FileSystem> {
    if (!m_impl) {
      return fail(ErrorCode::InvalidState, "Control was moved");
    }

    auto checked = m_impl->check();

    if (checked) {
      m_impl->m_exposed = true;
    }

    return checked ? m_impl->m_storage.files() : std::unexpected(checked.error());
  }

  auto Control::receipts() const -> Result<FileSystem> {
    if (!m_impl) {
      return fail(ErrorCode::InvalidState, "Control was moved");
    }

    auto checked = m_impl->check();

    if (checked) {
      m_impl->m_exposed = true;
    }

    return checked ? Result<FileSystem>{m_impl->m_receipts} : std::unexpected(checked.error());
  }

  auto Control::process(const ControlHandler& handler, const std::function<bool()>& keepRunning)
      -> Result<std::size_t> {
    auto root = files();

    if (!root) {
      return std::unexpected(root.error());
    }

    if (m_impl->m_closing) {
      return fail(ErrorCode::InvalidState, "Control is closing");
    }

    if (!handler || m_impl->m_fault) {
      return m_impl->m_fault ? std::unexpected(*m_impl->m_fault)
                             : fail(ErrorCode::InvalidArgument, "Control requires a handler");
    }

    struct Guard {
      bool& value;

      ~Guard() {
        value = false;
      }
    } guard{m_impl->m_processing};

    m_impl->m_processing = true;

    try {
      auto index = scan(*root, m_impl->m_receipts, m_impl->m_limits.receiptBytes);

      if (!index) {
        return std::unexpected(index.error());
      }

      for (const auto& name : index->pending) {
        const auto id = utf(name.stem());

        if (m_impl->m_dispatched.contains(id)) {
          m_impl->m_uncertain = id;
          m_impl->m_fault = fail(ErrorCode::InvalidState,
                                 "Control receipt '" + id
                                     + "' was lost; effects may have committed; never replay")
                                .error();
          return std::unexpected(*m_impl->m_fault);
        }
      }

      std::size_t processed{};

      for (const auto& name : index->pending) {
        if (processed == 16 || (keepRunning && !keepRunning())) {
          break;
        }

        auto source = root->readText(Fs::path{"requests"} / name, 1024 * 1024);
        auto parsed
            = source ? object(*source, 1024 * 1024) : Result<Json>{std::unexpected(source.error())};
        /** Invalid input may still be retained as source: JSON string escaping can expand it
         * sixfold. Reserve the whole bounded result before effects, never drop a completed reply
         * for space. 非法输入仍可能作为 source 保留，JSON
         * 字符串转义最多膨胀六倍；副作用前预留完整结果， 不因容量不足丢弃已完成回执。 */
        const auto reservation
            = m_impl->m_limits.responseBytes + (source ? 6 * source->size() : 0) + 128;
        auto blocked
            = m_impl->blocked(reservation > m_impl->m_limits.receiptBytes - index->responseBytes);

        if (!blocked) {
          return std::unexpected(blocked.error());
        }

        if (m_impl->m_blocked) {
          break;
        }

        if (m_impl->m_dispatched.size() == 4096) {
          return fail(ErrorCode::Unavailable, "Control dispatched identity limit reached");
        }

        m_impl->m_dispatched.insert(utf(name.stem()));
        m_impl->m_uncertain = utf(name.stem());
        m_impl->m_fault = Error{ErrorCode::External,
                                "Control request may have committed; response did not complete",
                                {}};
        auto response
            = parsed ? handler(*source) : Result<std::string>{std::unexpected(parsed.error())};
        auto value = object(response ? *response : errorJson(response.error()).dump(),
                            m_impl->m_limits.responseBytes);

        if (!value) {
          return std::unexpected(*m_impl->m_fault);
        }

        Json record{{"source", source && isValidUtf8(*source) ? Json(*source) : Json(nullptr)},
                    {"response", std::move(*value)}};
        auto encoded = record.dump();

        if (encoded.size() > m_impl->m_limits.receiptBytes - index->responseBytes) {
          return std::unexpected(*m_impl->m_fault);
        }

        auto saved = writeReceipt(m_impl->m_receipts, name, encoded);

        if (!saved) {
          m_impl->m_fault = Error{saved.error().code,
                                  "Control request may have committed; terminal response failure: "
                                      + saved.error().message,
                                  {}};
          return std::unexpected(*m_impl->m_fault);
        }

        index->responseBytes += encoded.size();
        m_impl->m_fault.reset();
        m_impl->m_uncertain.reset();
        ++processed;
      }

      return processed;
    } catch (const std::exception& error) {
      return m_impl->m_fault
                 ? std::unexpected(*m_impl->m_fault)
                 : fail(ErrorCode::External, "Process control: " + std::string(error.what()));
    } catch (...) {
      return m_impl->m_fault ? std::unexpected(*m_impl->m_fault)
                             : fail(ErrorCode::External, "Process control: non-standard exception");
    }
  }

  auto Control::failure() const -> Result<std::optional<Error>> {
    if (!m_impl) {
      return fail(ErrorCode::InvalidState, "Control was moved");
    }

    auto checked = m_impl->check();
    return checked ? Result<std::optional<Error>>{m_impl->m_fault}
                   : std::unexpected(checked.error());
  }

  auto Control::close() -> Result<void> {
    if (!m_impl) {
      return {};
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    try {
      m_impl->m_exposed = true;
      return m_impl->finish();
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Close control: " + std::string(error.what()));
    }
  }

  auto submitControlRequest(const Fs::path& control, std::string_view id, std::string_view request)
      -> Result<Fs::path> {
    if (!validId(id)) {
      return fail(ErrorCode::InvalidArgument, "Control ID must be 1..64 ASCII letters/digits/_/-");
    }

    auto valid = object(request, 1024 * 1024);
    auto files = FileSystem::open(control);

    if (!valid || !files) {
      return std::unexpected(!valid ? valid.error() : files.error());
    }

    auto text = files->readText("session.json", 64 * 1024 * 1024);
    auto metadata
        = text ? object(*text, 64 * 1024 * 1024) : Result<Json>{std::unexpected(text.error())};

    if (!metadata) {
      return std::unexpected(metadata.error());
    }

    try {
      if (metadata->value("protocol", Json{}) != 1 || metadata->value("state", Json{}) != "ready") {
        return fail(ErrorCode::InvalidState, "Control session is not ready");
      }

      auto archive = archiveAt(path(metadata->at("receipts").get<std::string>()));

      if (!archive) {
        return std::unexpected(archive.error());
      }

      if (archive->metadata["state"] != "ready"
          || archive->metadata["control"] != utf(files->root())) {
        return fail(ErrorCode::InvalidState, "Control receipt session changed or closed");
      }

      const auto name = std::string(id) + ".json";
      const auto destination = Fs::path{"requests"} / name;
      auto index = scan(*files, archive->files, archive->limits.receiptBytes);

      if (!index) {
        return std::unexpected(index.error());
      }

      const auto normalized = folded(name);
      const auto existing = index->requests.find(normalized);
      const auto response = index->responses.find(normalized);

      if (existing != index->requests.end()) {
        if (existing->second != name) {
          return fail(ErrorCode::InvalidState, "Control ID has a case alias");
        }

        auto old = files->readText(destination, 1024 * 1024);

        if (!old || *old != request) {
          return fail(ErrorCode::InvalidState, "Control ID already has different content");
        }
      } else {
        /** An orphan response cannot acknowledge an unrelated new request.
         * 孤立响应不能被当作无关新请求的回执。 */
        if (response != index->responses.end()) {
          return fail(ErrorCode::InvalidState, "Control response has no matching request");
        }

        if (index->requestEntries == 4096 || index->pending.size() >= 256
            || index->pending.size() >= 4096 - index->responses.size()) {
          return fail(ErrorCode::Unavailable,
                      "Control session request/response/pending limit exceeded");
        }

        auto saved = files->writeTextAtomic(destination, request);

        if (!saved) {
          return std::unexpected(saved.error());
        }
      }

      return archive->files.resolve(Fs::path{"responses"} / name);
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Submit control: " + std::string(error.what()));
    }
  }

  auto inspectControlReceipts(const Fs::path& directory) -> Result<ControlReceiptStatus> {
    try {
      auto archive = archiveAt(directory);

      if (!archive) {
        return std::unexpected(archive.error());
      }

      auto index = scanReplies(archive->files, archive->limits.receiptBytes);
      auto fault = faultFrom(archive->metadata["fault"]);

      if (!index || !fault) {
        return std::unexpected(!index ? index.error() : fault.error());
      }

      return ControlReceiptStatus{
          path(archive->metadata["control"].get<std::string>()),
          archive->metadata["state"] == "closed",
          archive->metadata["blocked"].get<bool>(),
          index->responses.size(),
          index->responseBytes,
          archive->limits.receiptBytes,
          std::move(*fault),
          archive->metadata["uncertain"].get<std::vector<std::string>>(),
          archive->metadata["unprocessed"].is_null()
              ? std::optional<std::vector<std::string>>{}
              : std::optional<std::vector<std::string>>{
                    archive->metadata["unprocessed"].get<std::vector<std::string>>()}};
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Inspect control receipts: " + std::string(error.what()));
    }
  }

  auto acknowledgeControlResponse(const Fs::path& response) -> Result<void> {
    try {
      const auto name = response.filename();

      if (response.parent_path().filename() != "responses" || name.extension() != ".json"
          || !validId(utf(name.stem()))) {
        return fail(ErrorCode::InvalidArgument, "Not a native control response path");
      }

      auto archive = archiveAt(response.parent_path().parent_path());

      if (!archive) {
        return std::unexpected(archive.error());
      }

      auto index = scanReplies(archive->files, archive->limits.receiptBytes);

      if (!index) {
        return std::unexpected(index.error());
      }

      const auto found = index->responses.find(folded(utf(name)));

      if (found == index->responses.end() || found->second.name != utf(name)) {
        return fail(ErrorCode::InvalidArgument, "Control response is missing or has a case alias");
      }

      auto text = archive->files.readText(Fs::path("responses") / name,
                                          archive->limits.responseBytes + 6 * requestBytes + 128);
      auto value = text ? receiptRecord(*text, archive->limits)
                        : Result<Json>{std::unexpected(text.error())};

      if (!value) {
        return std::unexpected(value.error());
      }

      if (*value == Json{{"consumed", true}}) {
        return {};
      }

      return writeReceipt(archive->files, name, R"({"consumed":true})");
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Consume control response: " + std::string(error.what()));
    }
  }

  auto clearControlReceipts(const Fs::path& directory) -> Result<void> {
    try {
      const auto requested = Fs::absolute(directory).lexically_normal();
      const auto token = utf(requested.filename());

      if (requested.parent_path().filename() != path(receiptCache) || !validId(token)) {
        return fail(ErrorCode::InvalidArgument, "Not a native control receipt session");
      }

      auto parent = FileSystem::open(requested.parent_path().parent_path());

      if (!parent) {
        return std::unexpected(parent.error());
      }

      const auto cache = parent->root() / path(receiptCache);
      const auto root = cache / path(token);
      std::error_code error;
      const auto live = Fs::symlink_status(parent->root() / (".dot-stage-" + token), error);

      if (live.type() != Fs::file_type::not_found) {
        return fail(ErrorCode::InvalidState,
                    "Control endpoint has not retired; retry its close first");
      }

      if (error && error != std::errc::no_such_file_or_directory) {
        return fail(ErrorCode::Io, "Check retired control: " + error.message());
      }

      const auto cacheState = Fs::symlink_status(cache);

      if (cacheState.type() == Fs::file_type::not_found) {
        return {};
      }

      if (cacheState.type() != Fs::file_type::directory) {
        return fail(ErrorCode::InvalidArgument, "Receipt cache must be an owned directory");
      }

      const auto state = Fs::symlink_status(root);

      if (state.type() != Fs::file_type::not_found && state.type() != Fs::file_type::directory) {
        return fail(ErrorCode::InvalidArgument, "Receipt session must be an owned directory");
      }

      /**
       * Remove bodies before metadata so interrupted cleanup remains retryable.
       * 先清理正文再清理元数据，确保清理中断后仍可重试。
       */
      if (state.type() != Fs::file_type::not_found && !Fs::is_empty(root)) {
        auto archive = archiveAt(root);

        if (!archive) {
          return std::unexpected(archive.error());
        }

        if (archive->metadata["state"] != "closed") {
          return fail(ErrorCode::InvalidState, "Cannot clear active control receipts");
        }

        for (const auto& entry : Fs::directory_iterator(root)) {
          if (entry.path().filename() != "session.json" && entry.path().filename() != "responses") {
            return fail(ErrorCode::InvalidArgument, "Receipt session contains unowned entries");
          }
        }

        const auto responses = root / "responses";

        if (Fs::symlink_status(responses).type() != Fs::file_type::not_found) {
          auto index = scanReplies(archive->files, archive->limits.receiptBytes);

          if (!index) {
            return std::unexpected(index.error());
          }

          for (const auto& [key, entry] : index->responses) {
            static_cast<void>(key);
            auto text
                = archive->files.readText(Fs::path("responses") / path(entry.name),
                                          archive->limits.responseBytes + 6 * requestBytes + 128);
            auto record = text ? receiptRecord(*text, archive->limits)
                               : Result<Json>{std::unexpected(text.error())};

            if (!record) {
              return std::unexpected(record.error());
            }
          }

          for (const auto& [key, entry] : index->responses) {
            static_cast<void>(key);
            Fs::remove(responses / path(entry.name));
          }

          Fs::remove(responses);
        }

        Fs::remove(root / "session.json");
      }

      Fs::remove(root);

      if (Fs::is_empty(cache)) {
        Fs::remove(cache);
      }

      return {};
    } catch (const std::exception& error) {
      return fail(ErrorCode::External,
                  "Clear control receipts (may be partial; retry): " + std::string(error.what()));
    }
  }

  auto performControlReceiptAction(std::string_view command, const Fs::path& target)
      -> Result<std::string> {
    try {
      if (command == "control-status") {
        auto status = inspectControlReceipts(target);

        if (!status) {
          return std::unexpected(status.error());
        }

        return Json{
            {"ok", true},
            {"control", utf(status->control)},
            {"state", status->closed ? "closed" : "ready"},
            {"blocked", status->blocked},
            {"count", status->count},
            {"bytes", status->bytes},
            {"capacity", status->capacity},
            {"fault", status->fault ? errorJson(*status->fault)["error"] : Json(nullptr)},
            {"uncertain", status->uncertain},
            {"unprocessed", status->unprocessed ? Json(*status->unprocessed) : Json(nullptr)}}
            .dump();
      }

      if (command != "ack-control" && command != "clear-control") {
        return fail(ErrorCode::InvalidArgument, "Unknown control receipt action");
      }

      auto applied = command == "ack-control" ? acknowledgeControlResponse(target)
                                              : clearControlReceipts(target);
      return applied ? Result<std::string>{Json{
                           {"ok", true},
                           {command == "ack-control" ? "consumed" : "cleared",
                            true}}.dump()}
                     : std::unexpected(applied.error());
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Control receipt action: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Core
