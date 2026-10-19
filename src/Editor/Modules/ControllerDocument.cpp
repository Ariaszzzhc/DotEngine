module;

#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

module Dot.Editor.ControllerDocument;

import Dot.Animation.ControllerFiles;
import Dot.Core.Result;
import Dot.Editor.Resources;

namespace Dot::Editor {
  struct ControllerDocument::Impl {
    struct State {
      Animation::Controller value;
      std::shared_ptr<const std::string> text;
    };

    TextDocument m_text;
    std::string m_path;
    std::vector<State> m_history;
    std::shared_ptr<const std::string> m_saved;
    ControllerHistoryLimits m_limits;
    std::size_t m_cursor{};
    std::uint64_t m_revision{1};
    bool m_dirty{};
    std::thread::id m_thread{std::this_thread::get_id()};

    Impl(TextDocument text, TextSnapshot snapshot, Animation::Controller value,
         ControllerHistoryLimits limits)
        : m_text(std::move(text)),
          m_path(std::move(snapshot.path)),
          m_saved(std::make_shared<const std::string>(std::move(snapshot.text))),
          m_limits(limits),
          m_revision(snapshot.revision) {
      m_history.push_back({std::move(value), m_saved});
    }

    auto check(std::optional<std::uint64_t> revision = {}) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || (revision && *revision != m_revision)) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Controller document requires owner thread/current revision");
      }

      return {};
    }

    auto bytes() const -> std::size_t {
      std::size_t total{};

      for (const auto& state : m_history) {
        total += state.text->size();
      }

      return total;
    }

    auto travel(std::uint64_t revision, bool forward) -> Core::Result<void> {
      auto checked = check(revision);

      if (!checked) {
        return checked;
      }

      if (forward ? m_cursor + 1 == m_history.size() : m_cursor == 0) {
        return Core::fail(Core::ErrorCode::InvalidState, "Controller history boundary");
      }

      const auto next = forward ? m_cursor + 1 : m_cursor - 1;
      auto changed = m_text.replace(*m_history[next].text, revision);

      if (changed) {
        m_cursor = next;
        ++m_revision;
        m_dirty = *m_history[m_cursor].text != *m_saved;
      }

      return changed;
    }
  };

  ControllerDocument::ControllerDocument(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  ControllerDocument::ControllerDocument(ControllerDocument&&) noexcept = default;

  auto ControllerDocument::operator=(ControllerDocument&&) noexcept
      -> ControllerDocument& = default;

  ControllerDocument::~ControllerDocument() = default;

  auto ControllerDocument::open(const Project::Project& project, std::string_view path,
                                ControllerHistoryLimits limits)
      -> Core::Result<ControllerDocument> {
    if (!path.starts_with("assets/") || !path.ends_with(".dotcontroller") || limits.states < 2
        || limits.states > 128 || limits.bytes == 0 || limits.bytes > 32 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid controller path/history limits");
    }

    try {
      ResourceLimits resources;
      resources.textBytes = 1024 * 1024;
      auto store = ResourceStore::open(project, resources);
      auto text = store ? store->openText(path)
                        : Core::Result<TextDocument>{std::unexpected(store.error())};

      if (!text) {
        return std::unexpected(text.error());
      }

      auto source = text->snapshot();

      if (!source) {
        return std::unexpected(source.error());
      }

      if (source->text.size() > limits.bytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Controller source exceeds history byte budget");
      }

      auto clip = Animation::decodeController(source->text);

      if (!clip) {
        return std::unexpected(clip.error());
      }

      return ControllerDocument(
          std::make_unique<Impl>(std::move(*text), std::move(*source), std::move(*clip), limits));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Open controller: " + std::string(error.what()));
    }
  }

  auto ControllerDocument::snapshot() const -> Core::Result<ControllerResourceSnapshot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller document was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return ControllerResourceSnapshot{
          m_impl->m_path,           m_impl->m_history[m_impl->m_cursor].value,
          m_impl->m_revision,       m_impl->m_dirty,
          m_impl->m_cursor > 0,     m_impl->m_cursor + 1 < m_impl->m_history.size(),
          m_impl->m_history.size(), m_impl->bytes()};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect controller: " + std::string(error.what()));
    }
  }

  auto ControllerDocument::replace(Animation::Controller value, std::uint64_t revision)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    try {
      auto reset = value.reset();

      if (!reset) {
        return std::unexpected(reset.error());
      }

      value = std::move(*reset);
      auto encoded = Animation::encodeController(value);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto current = Animation::encodeController(m_impl->m_history[m_impl->m_cursor].value);

      if (!current) {
        return std::unexpected(current.error());
      }

      if (*current == *encoded) {
        return {};
      }

      if (encoded->size() > m_impl->m_limits.bytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Controller state exceeds history byte budget");
      }

      auto history = m_impl->m_history;
      history.resize(m_impl->m_cursor + 1);
      history.push_back(
          {std::move(value), std::make_shared<const std::string>(std::move(*encoded))});
      std::size_t bytes{};

      for (const auto& state : history) {
        bytes += state.text->size();
      }

      std::size_t remove{};

      while (history.size() - remove > m_impl->m_limits.states || bytes > m_impl->m_limits.bytes) {
        bytes -= history[remove++].text->size();
      }

      history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(remove));
      auto changed = m_impl->m_text.replace(*history.back().text, revision);

      if (!changed) {
        return changed;
      }

      m_impl->m_history.swap(history);
      m_impl->m_cursor = m_impl->m_history.size() - 1;
      ++m_impl->m_revision;
      m_impl->m_dirty = *m_impl->m_history.back().text != *m_impl->m_saved;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Replace controller: " + std::string(error.what()));
    }
  }

  auto ControllerDocument::historyValue(bool forward, std::uint64_t revision) const
      -> Core::Result<Animation::Controller> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (forward ? m_impl->m_cursor + 1 == m_impl->m_history.size() : m_impl->m_cursor == 0) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller history boundary");
    }

    return m_impl->m_history[forward ? m_impl->m_cursor + 1 : m_impl->m_cursor - 1].value;
  }

  auto ControllerDocument::undo(std::uint64_t revision) -> Core::Result<void> {
    return m_impl ? m_impl->travel(revision, false)
                  : Core::fail(Core::ErrorCode::InvalidState, "Controller document was moved");
  }

  auto ControllerDocument::redo(std::uint64_t revision) -> Core::Result<void> {
    return m_impl ? m_impl->travel(revision, true)
                  : Core::fail(Core::ErrorCode::InvalidState, "Controller document was moved");
  }

  auto ControllerDocument::save(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Controller document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    auto saved = m_impl->m_text.save(revision);

    if (saved) {
      m_impl->m_revision += m_impl->m_dirty;
      m_impl->m_saved = m_impl->m_history[m_impl->m_cursor].text;
      m_impl->m_dirty = false;
    }

    return saved;
  }
}  // namespace Dot::Editor
