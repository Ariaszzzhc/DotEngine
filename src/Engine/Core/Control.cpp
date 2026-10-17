module;

#include <algorithm>
#include <array>
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
  }  // namespace

  struct Control::Impl {
    StagedDirectory m_storage;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::optional<Error> m_fault;
    bool m_processing{};

    explicit Impl(StagedDirectory value) : m_storage(std::move(value)) {
    }

    auto check() const -> Result<void> {
      if (m_thread != std::this_thread::get_id() || m_processing) {
        return fail(ErrorCode::InvalidState, "Control requires its owner without reentry");
      }

      return {};
    }
  };

  Control::Control(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Control::Control(Control&&) noexcept = default;

  auto Control::operator=(Control&&) noexcept -> Control& = default;

  Control::~Control() = default;

  auto Control::create(const FileSystem& parent, std::string_view metadata) -> Result<Control> {
    auto parsed = object(metadata, 64 * 1024 * 1024);

    if (!parsed) {
      return std::unexpected(parsed.error());
    }

    if (parsed->contains("protocol") || parsed->contains("state")) {
      return fail(ErrorCode::InvalidArgument, "Control metadata cannot replace protocol/state");
    }

    try {
      auto storage = StagedDirectory::create(parent);

      if (!storage) {
        return std::unexpected(storage.error());
      }

      auto files = storage->files().value();
      auto requests = files.createDirectories("requests");
      auto responses = files.createDirectories("responses");

      if (!requests || !responses) {
        return std::unexpected(!requests ? requests.error() : responses.error());
      }

      (*parsed)["protocol"] = 1;
      (*parsed)["state"] = "ready";
      auto saved = files.writeTextAtomic("session.json", parsed->dump());

      if (!saved) {
        return std::unexpected(saved.error());
      }

      return Control{std::make_unique<Impl>(std::move(*storage))};
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Create control: " + std::string(error.what()));
    }
  }

  auto Control::files() const -> Result<FileSystem> {
    if (!m_impl) {
      return fail(ErrorCode::InvalidState, "Control was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_storage.files() : std::unexpected(checked.error());
  }

  auto Control::process(const ControlHandler& handler, const std::function<bool()>& keepRunning)
      -> Result<std::size_t> {
    auto root = files();

    if (!root) {
      return std::unexpected(root.error());
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
      auto requests = root->resolve("requests");
      auto responses = root->resolve("responses");

      if (!requests || !responses) {
        return std::unexpected(!requests ? requests.error() : responses.error());
      }

      std::vector<Fs::path> pending;
      std::set<std::string> requestNames;
      std::map<std::string, std::string> responseNames;
      std::size_t entries{};

      for (const auto& entry : Fs::directory_iterator(*responses)) {
        if (++entries > 4096) {
          return fail(ErrorCode::Unavailable, "Control response entry limit exceeded");
        }

        const auto utf = entry.path().filename().u8string();
        const std::string name(utf.begin(), utf.end());

        if (!responseNames.emplace(folded(name), name).second) {
          return fail(ErrorCode::InvalidArgument, "Control response has a case alias");
        }
      }

      entries = 0;

      for (const auto& entry : Fs::directory_iterator(*requests)) {
        if (++entries > 4096) {
          return fail(ErrorCode::Unavailable, "Control session request limit exceeded");
        }

        if (entry.path().extension() != ".json") {
          continue;
        }

        const auto utf = entry.path().stem().u8string();
        const std::string id(utf.begin(), utf.end());

        if (!validId(id) || !Fs::is_regular_file(entry.symlink_status())) {
          return fail(ErrorCode::InvalidArgument, "Invalid control request filename/type");
        }

        if (!requestNames.insert(folded(id)).second) {
          return fail(ErrorCode::InvalidArgument, "Control request has a case alias");
        }

        const auto name = id + ".json";
        const auto previous = responseNames.find(folded(name));

        if (previous != responseNames.end() && previous->second != name) {
          return fail(ErrorCode::InvalidArgument, "Control response spelling differs from request");
        }

        auto response = root->resolve(Fs::path{"responses"} / entry.path().filename());

        if (!response) {
          return std::unexpected(response.error());
        }

        if (Fs::exists(*response) && !Fs::is_regular_file(Fs::symlink_status(*response))) {
          return fail(ErrorCode::InvalidArgument, "Invalid control response file type");
        }

        if (!Fs::exists(*response)) {
          pending.push_back(entry.path().filename());
        }

        if (pending.size() > 256) {
          return fail(ErrorCode::Unavailable, "Control pending request capacity exceeded");
        }
      }

      std::ranges::sort(pending);
      std::size_t processed{};

      for (const auto& name : pending) {
        if (processed == 16 || (keepRunning && !keepRunning())) {
          break;
        }

        auto source = root->readText(Fs::path{"requests"} / name, 1024 * 1024);
        auto parsed
            = source ? object(*source, 1024 * 1024) : Result<Json>{std::unexpected(source.error())};
        m_impl->m_fault = Error{ErrorCode::External,
                                "Control request may have committed; response did not complete",
                                {}};
        auto response
            = parsed ? handler(*source) : Result<std::string>{std::unexpected(parsed.error())};
        auto value = response ? object(*response, 64 * 1024 * 1024)
                              : Result<Json>{errorJson(response.error())};

        if (!value) {
          return std::unexpected(*m_impl->m_fault);
        }

        Json record{{"source", source && isValidUtf8(*source) ? Json(*source) : Json(nullptr)},
                    {"response", std::move(*value)}};
        auto saved = root->writeTextAtomic(Fs::path{"responses"} / name, record.dump());

        if (!saved) {
          m_impl->m_fault = Error{saved.error().code,
                                  "Control request may have committed; terminal response failure: "
                                      + saved.error().message,
                                  {}};
          return std::unexpected(*m_impl->m_fault);
        }

        m_impl->m_fault.reset();
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
    return checked ? m_impl->m_storage.discard() : checked;
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

      const auto name = std::string(id) + ".json";
      const auto destination = Fs::path{"requests"} / name;
      auto resolved = files->resolve(destination);
      auto directory = files->resolve("requests");

      if (!resolved || !directory) {
        return std::unexpected(!resolved ? resolved.error() : directory.error());
      }

      std::size_t entries{};
      std::size_t pending{};

      for (const auto& entry : Fs::directory_iterator(*directory)) {
        if (++entries > 4096) {
          return fail(ErrorCode::Unavailable, "Control session request limit exceeded");
        }

        const auto utf = entry.path().filename().u8string();
        const std::string existing(utf.begin(), utf.end());

        if (folded(existing) == folded(name) && existing != name) {
          return fail(ErrorCode::InvalidState, "Control ID has a case alias");
        }

        if (entry.path().extension() == ".json") {
          auto response = files->resolve(Fs::path{"responses"} / entry.path().filename());

          if (!response) {
            return std::unexpected(response.error());
          }

          pending += static_cast<std::size_t>(!Fs::exists(*response));
        }
      }

      if (Fs::exists(*resolved)) {
        auto old = files->readText(destination, 1024 * 1024);

        if (!old || *old != request) {
          return fail(ErrorCode::InvalidState, "Control ID already has different content");
        }
      } else {
        if (entries == 4096 || pending >= 256) {
          return fail(ErrorCode::Unavailable, "Control session request/pending limit exceeded");
        }

        auto saved = files->writeTextAtomic(destination, request);

        if (!saved) {
          return std::unexpected(saved.error());
        }
      }

      return files->resolve(Fs::path{"responses"} / name);
    } catch (const std::exception& error) {
      return fail(ErrorCode::External, "Submit control: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Core
