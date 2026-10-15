module;

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

export module Dot.Editor.MaterialDocument;

import Dot.Core.Result;
import Dot.Project;
export import Dot.Assets.Material;

export namespace Dot::Editor {
  struct MaterialSnapshot {
    std::string path;
    Assets::MaterialDescription value;
    std::uint64_t revision;
    bool dirty;
    bool canUndo;
    bool canRedo;
  };

  /** Typed, owned material draft with 128 retained edit states. All mutations
   * require the current revision and creating thread; no-op edits retain it.
   * Save checks the exact disk baseline before atomic replacement. Undo preserves
   * original source bytes; saving retains history. Destruction never saves.
   * 拥有类型化材质草稿并保留 128 个编辑状态；修改须匹配当前版本且限创建线程，
   * 无变化编辑保留版本。保存校验磁盘原文后原子替换，撤销保留初始字节，保存不清历史，
   * 销毁不保存。纹理内容由实际渲染路径单独验证。 */
  class MaterialDocument {
  public:
    [[nodiscard]] static auto open(const Project::Project& project, std::string_view path)
        -> Core::Result<MaterialDocument>;
    MaterialDocument(const MaterialDocument&) = delete;
    auto operator=(const MaterialDocument&) -> MaterialDocument& = delete;
    MaterialDocument(MaterialDocument&&) noexcept;
    auto operator=(MaterialDocument&&) noexcept -> MaterialDocument&;
    ~MaterialDocument();
    [[nodiscard]] auto snapshot() const -> Core::Result<MaterialSnapshot>;
    [[nodiscard]] auto replace(const Assets::MaterialDescription& value, std::uint64_t revision)
        -> Core::Result<void>;
    [[nodiscard]] auto undo(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto redo(std::uint64_t revision) -> Core::Result<void>;
    [[nodiscard]] auto save(std::uint64_t revision) -> Core::Result<void>;

  private:
    struct Impl;
    explicit MaterialDocument(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor
