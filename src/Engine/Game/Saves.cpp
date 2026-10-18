module;

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Game.Saves;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Core.Text;
import Dot.Game.State;

namespace Dot::Game {
  namespace {
    namespace Fs = std::filesystem;
    using Json = Serialization::Detail::Json;

    auto utf(const Fs::path& path) -> std::string {
      const auto text = path.generic_u8string();
      return {text.begin(), text.end()};
    }

    auto folded(std::string text) -> std::string {
      for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }

      return text;
    }

    auto error(std::string_view operation, const std::exception& value)
        -> std::unexpected<Core::Error> {
      const auto code = dynamic_cast<const Fs::filesystem_error*>(&value) ? Core::ErrorCode::Io
                        : dynamic_cast<const std::invalid_argument*>(&value)
                                || dynamic_cast<const Json::exception*>(&value)
                            ? Core::ErrorCode::InvalidArgument
                            : Core::ErrorCode::External;
      return Core::fail(code, "Save " + std::string(operation) + ": " + value.what());
    }

    auto context(Core::Error problem, std::string_view operation, std::string_view slot)
        -> std::unexpected<Core::Error> {
      problem.message
          = "Save " + std::string(operation) + " '" + std::string(slot) + "': " + problem.message;
      return std::unexpected(std::move(problem));
    }

    auto revision(const Json& value) -> std::uint64_t {
      if (!value.is_string()) {
        throw std::invalid_argument("Snapshot revision must be a decimal string");
      }

      const auto& text = value.get_ref<const std::string&>();
      std::uint64_t result{};
      const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), result);

      if (text.empty() || text.size() > 20 || text.front() == '0' || ec != std::errc{}
          || end != text.data() + text.size()
          || result == std::numeric_limits<std::uint64_t>::max()) {
        throw std::invalid_argument("Invalid canonical snapshot revision");
      }

      return result;
    }
  }  // namespace

  auto validateSaveSlot(std::string_view slot) -> bool {
    return !slot.empty() && slot.size() <= 64 && std::ranges::all_of(slot, [](unsigned char c) {
      return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }) && Core::isPortableRelativePath(std::string(slot) + ".dotsave");
  }

  struct Saves::Impl {
    Core::FileSystem m_root;
    SaveLimits m_limits;
    std::thread::id m_thread{std::this_thread::get_id()};

    Impl(Core::FileSystem root, SaveLimits limits) : m_root(std::move(root)), m_limits(limits) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Save store requires its owner thread");
      }

      std::error_code ec;
      const auto status = Fs::symlink_status(m_root.root(), ec);

      if (ec || !Fs::is_directory(status) || Fs::is_symlink(status)) {
        return Core::fail(Core::ErrorCode::Io,
                          "Save root is missing, inaccessible or not a real directory");
      }

      return {};
    }

    auto scan() const -> Core::Result<std::vector<SaveSlot>> {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      std::vector<SaveSlot> slots;
      std::size_t visited{};

      for (const auto& entry : Fs::directory_iterator(m_root.root())) {
        if (++visited > 4096) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Save root exceeds 4096 directory entries");
        }

        const auto name = utf(entry.path().filename());

        if (!folded(name).ends_with(".dotsave")) {
          continue;
        }

        const auto slot = name.substr(0, name.size() - 8);

        if (!validateSaveSlot(slot) || name != slot + ".dotsave") {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Noncanonical save filename: " + name);
        }

        const auto status = entry.symlink_status();

        if (!Fs::is_regular_file(status) || Fs::is_symlink(status)) {
          return Core::fail(Core::ErrorCode::Io, "Save slot is a symlink or special file: " + name);
        }

        slots.push_back({slot, entry.file_size()});
      }

      std::ranges::sort(slots, {}, &SaveSlot::name);
      return slots;
    }
  };

  Saves::Saves(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Saves::Saves(Saves&&) noexcept = default;

  auto Saves::operator=(Saves&&) noexcept -> Saves& = default;

  Saves::~Saves() = default;

  auto Saves::open(const Core::FileSystem& root, const SaveLimits& limits) -> Core::Result<Saves> {
    auto valid = normalizeStateData("{}", limits.state);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (limits.slots == 0 || limits.slots > 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Save slot limit must be 1..1024");
    }

    try {
      auto impl = std::make_unique<Impl>(root, limits);
      auto checked = impl->check();
      return checked ? Core::Result<Saves>{Saves(std::move(impl))}
                     : Core::Result<Saves>{std::unexpected(checked.error())};
    } catch (const std::exception& value) {
      return error("open", value);
    }
  }

  auto Saves::list() const -> Core::Result<std::vector<SaveSlot>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save store was moved");
    }

    try {
      return m_impl->scan();
    } catch (const std::exception& value) {
      return error("list", value);
    }
  }

  auto Saves::read(std::string_view slot) const -> Core::Result<StateSnapshot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save store was moved");
    }

    try {
      if (!validateSaveSlot(slot)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid lowercase save slot name");
      }

      auto entries = m_impl->scan();

      if (!entries) {
        return std::unexpected(entries.error());
      }

      if (std::ranges::find(*entries, slot, &SaveSlot::name) == entries->end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Save slot is missing: " + std::string(slot));
      }

      auto bytes = m_impl->m_root.readText(std::string(slot) + ".dotsave",
                                           m_impl->m_limits.state.bytes + 512);

      if (!bytes) {
        return context(std::move(bytes.error()), "read", slot);
      }

      if (!Core::isValidUtf8(*bytes)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Save slot is not UTF-8: " + std::string(slot));
      }

      const auto object = Serialization::Detail::parseJson(*bytes, 34);
      Serialization::Detail::requireFields(object, {"format", "slot", "revision", "data"});

      if (object["format"] != "dot-save" || object["slot"] != slot) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Save format/slot does not match: " + std::string(slot));
      }

      const auto savedRevision = revision(object["revision"]);
      auto data = normalizeStateData(object["data"].dump(), m_impl->m_limits.state);

      if (!data) {
        return context(std::move(data.error()), "read", slot);
      }

      return StateSnapshot{savedRevision, std::move(*data)};
    } catch (const std::exception& value) {
      return error("read " + std::string(slot), value);
    }
  }

  auto Saves::write(std::string_view slot, const StateSnapshot& snapshot, bool overwrite)
      -> Core::Result<SaveSlot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save store was moved");
    }

    try {
      if (!validateSaveSlot(slot) || snapshot.revision == 0
          || snapshot.revision == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid save slot/snapshot revision");
      }

      auto entries = m_impl->scan();

      if (!entries) {
        return std::unexpected(entries.error());
      }

      const bool exists = std::ranges::find(*entries, slot, &SaveSlot::name) != entries->end();

      if (exists && !overwrite) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Save slot already exists: " + std::string(slot));
      }

      if (!exists && entries->size() >= m_impl->m_limits.slots) {
        return Core::fail(Core::ErrorCode::Unavailable, "Save slot capacity reached");
      }

      auto normalized = normalizeStateData(snapshot.data, m_impl->m_limits.state);

      if (!normalized) {
        return context(std::move(normalized.error()), "write", slot);
      }

      auto payload = Json{{"format", "dot-save"},
                          {"slot", slot},
                          {"revision", std::to_string(snapshot.revision)},
                          {"data", Serialization::Detail::parseJson(*normalized, 32)}}
                         .dump();
      const auto path = std::string(slot) + ".dotsave";
      SaveSlot result{std::string(slot), payload.size()};
      auto written = m_impl->m_root.writeTextAtomic(path, payload);

      if (!written) {
        return context(std::move(written.error()), "write", slot);
      }

      return result;
    } catch (const std::exception& value) {
      return error("write " + std::string(slot), value);
    }
  }

  auto Saves::remove(std::string_view slot) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Save store was moved");
    }

    try {
      if (!validateSaveSlot(slot)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid lowercase save slot name");
      }

      auto entries = m_impl->scan();

      if (!entries) {
        return std::unexpected(entries.error());
      }

      if (std::ranges::find(*entries, slot, &SaveSlot::name) == entries->end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Save slot is missing: " + std::string(slot));
      }

      if (!Fs::remove(m_impl->m_root.root() / (std::string(slot) + ".dotsave"))) {
        return Core::fail(Core::ErrorCode::Io, "Save slot disappeared before deletion");
      }

      return {};
    } catch (const std::exception& value) {
      return error("remove " + std::string(slot), value);
    }
  }
}  // namespace Dot::Game
