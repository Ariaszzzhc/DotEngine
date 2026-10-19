module;

#include <exception>
#include <expected>
#include <memory>
#include <string>
#include <utility>
#include <vector>

module Dot.Editor.MaterialDocument;

import Dot.Editor.Resources;

namespace Dot::Editor {
  struct MaterialDocument::Impl {
    TextDocument text;
    std::vector<std::string> history;
    std::size_t cursor{};

    auto check(std::uint64_t revision) const -> Core::Result<void> {
      auto current = text.snapshot();

      if (!current) {
        return std::unexpected(current.error());
      }

      if (current->revision != revision) {
        return Core::fail(Core::ErrorCode::InvalidState, "Stale material revision");
      }

      return {};
    }

    auto travel(std::uint64_t revision, bool forward) -> Core::Result<void> {
      auto valid = check(revision);

      if (!valid) {
        return valid;
      }

      if (forward ? cursor + 1 == history.size() : cursor == 0) {
        return Core::fail(Core::ErrorCode::InvalidState, "Material history boundary");
      }

      const auto next = forward ? cursor + 1 : cursor - 1;
      auto changed = text.replace(history[next], revision);

      if (changed) {
        cursor = next;
      }

      return changed;
    }
  };

  MaterialDocument::MaterialDocument(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  MaterialDocument::MaterialDocument(MaterialDocument&&) noexcept = default;

  auto MaterialDocument::operator=(MaterialDocument&&) noexcept -> MaterialDocument& = default;

  MaterialDocument::~MaterialDocument() = default;

  auto MaterialDocument::open(const Project::Project& project, std::string_view path)
      -> Core::Result<MaterialDocument> {
    if (!path.starts_with("assets/") || !path.ends_with(".dotmaterial")) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Expected an assets/*.dotmaterial path");
    }

    try {
      auto store = ResourceStore::open(project);

      if (!store) {
        return std::unexpected(store.error());
      }

      auto text = store->openText(path);

      if (!text) {
        return std::unexpected(text.error());
      }

      auto source = text->snapshot();

      if (!source) {
        return std::unexpected(source.error());
      }

      auto value = Assets::decodeMaterial(source->text);

      if (!value) {
        return std::unexpected(value.error());
      }

      return MaterialDocument(
          std::make_unique<Impl>(Impl{std::move(*text), {std::move(source->text)}, 0}));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Open material: " + std::string(error.what()));
    }
  }

  auto MaterialDocument::snapshot() const -> Core::Result<MaterialSnapshot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Material document was moved from");
    }

    auto source = m_impl->text.snapshot();

    if (!source) {
      return std::unexpected(source.error());
    }

    auto value = Assets::decodeMaterial(source->text);

    if (!value) {
      return std::unexpected(value.error());
    }

    return MaterialSnapshot{std::move(source->path), std::move(*value),
                            source->revision,        source->dirty,
                            m_impl->cursor > 0,      m_impl->cursor + 1 < m_impl->history.size()};
  }

  auto MaterialDocument::replace(const Assets::MaterialDescription& value, std::uint64_t revision)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Material document was moved from");
    }

    auto valid = m_impl->check(revision);

    if (!valid) {
      return valid;
    }

    try {
      auto encoded = Assets::encodeMaterial(value);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto current = snapshot();

      if (!current) {
        return std::unexpected(current.error());
      }

      auto canonical = Assets::encodeMaterial(current->value);

      if (!canonical) {
        return std::unexpected(canonical.error());
      }

      if (*canonical == *encoded) {
        return {};
      }

      auto history = m_impl->history;
      history.resize(m_impl->cursor + 1);
      history.push_back(*encoded);

      if (history.size() > 128) {
        history.erase(history.begin());
      }

      auto changed = m_impl->text.replace(*encoded, revision);

      if (!changed) {
        return changed;
      }

      m_impl->history.swap(history);
      m_impl->cursor = m_impl->history.size() - 1;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Edit material: " + std::string(error.what()));
    }
  }

  auto MaterialDocument::undo(std::uint64_t revision) -> Core::Result<void> {
    return m_impl ? m_impl->travel(revision, false)
                  : Core::fail(Core::ErrorCode::InvalidState, "Material document was moved from");
  }

  auto MaterialDocument::redo(std::uint64_t revision) -> Core::Result<void> {
    return m_impl ? m_impl->travel(revision, true)
                  : Core::fail(Core::ErrorCode::InvalidState, "Material document was moved from");
  }

  auto MaterialDocument::save(std::uint64_t revision) -> Core::Result<void> {
    return m_impl ? m_impl->text.save(revision)
                  : Core::fail(Core::ErrorCode::InvalidState, "Material document was moved from");
  }
}  // namespace Dot::Editor
