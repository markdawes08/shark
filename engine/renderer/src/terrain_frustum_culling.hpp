#pragma once

#include <shark/core/math.hpp>

#include <array>
#include <optional>

namespace shark::renderer::detail {

struct FrustumPlane final {
    math::Float3 normal;
    double distance{};
};

struct ViewFrustum final {
    std::array<FrustumPlane, 6> planes;
};

// Extracts normalized inward-facing planes for Shark's row-vector
// world-to-clip transform and Direct3D clip volume:
// -w <= x <= w, -w <= y <= w, and 0 <= z <= w.
[[nodiscard]] std::optional<ViewFrustum> make_view_frustum(
    const math::Matrix4x4& view_projection) noexcept;

// Tangent and intersecting bounds remain visible. The test is conservative:
// it can retain a box at a frustum corner, but it never rejects a box that
// intersects the intersection of all six half-spaces (the frustum).
[[nodiscard]] bool intersects_view_frustum(
    const ViewFrustum& frustum,
    math::Float3 minimum,
    math::Float3 maximum) noexcept;

} // namespace shark::renderer::detail
