module;

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

export module Dot.Animation.Skin;

import Dot.Core.Result;
import Dot.Math;

export namespace Dot::Animation {
  struct SkinInfluence {
    std::array<std::uint32_t, 8> joints{};
    std::array<float, 8> weights{1, 0, 0, 0, 0, 0, 0, 0};
  };

  struct SkinDescription {
    std::vector<Math::Mat4> inverseBindMatrices;
    std::vector<SkinInfluence> vertices;
  };

  /** Immutable shared bind data: 1..1024 joints, 1..1M vertices, eight slots per
   * vertex. Every index is in range; finite weights are [0,1], positive in total,
   * with no repeated positive joint. Creation normalizes quantized weights once.
   * Matrices are finite invertible affine. Default/moved handles fail explicitly.
   * 不可变共享绑定数据：1..1024 关节、1..百万顶点，每顶点八槽。所有索引有效，权重有限
   * 且在 [0,1]、总量为正、正权重关节不重复；创建时一次归一化量化权重。矩阵有限、
   * 仿射且可逆。默认及移后句柄明确失败。 */
  class Skin {
  public:
    Skin() = default;
    [[nodiscard]] static auto create(SkinDescription description) -> Core::Result<Skin>;
    [[nodiscard]] auto description() const -> Core::Result<const SkinDescription*>;
    /** Mesh-local palette: inverse(meshWorld) * jointWorld * inverseBind. Inputs
     * are current world matrices in joint order, not local node transforms.
     * Own the result; no scene/device/file is borrowed or changed.
     * 网格局部调色板：inverse(meshWorld) * jointWorld * inverseBind，按关节顺序传入
     * 当前世界矩阵而非节点局部变换。结果独立拥有，不借用或修改场景/设备/文件。 */
    [[nodiscard]] auto palette(const Math::Mat4& meshWorld,
                               std::span<const Math::Mat4> jointWorld) const
        -> Core::Result<std::vector<Math::Mat4>>;

  private:
    struct Data;
    explicit Skin(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> m_data;
  };

  /** Pose boundary shared by reference/GPU paths. Exact joint count, finite affine
   * entries of magnitude <=1e8 and invertible individual matrices. Blended-vertex
   * singularity is checked during deformation, not inferred from joint validity.
   * 参考/GPU 共用姿态边界：关节数匹配、有限仿射元素绝对值 <=1e8、各矩阵可逆；
   * 混合后顶点矩阵的奇异性在变形阶段检查，不能从单关节合法推断。 */
  [[nodiscard]] auto validateSkinPalette(const Skin& skin, std::span<const Math::Mat4> palette)
      -> Core::Result<void>;
}  // namespace Dot::Animation
