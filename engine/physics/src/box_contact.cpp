#include <shark/physics/box_contact.hpp>

#include <shark/core/error.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace shark::physics {
namespace {

inline constexpr double contact_tolerance_meters = 0.00001;
inline constexpr double axis_tie_tolerance = contact_tolerance_meters;
inline constexpr double parallel_relative_tolerance = 1.0e-12;
inline constexpr double coincident_distance_squared = 1.0e-20;
inline constexpr std::size_t clipping_polygon_capacity = 16;

using PreciseVector = std::array<double, 3>;

struct PreciseBox final {
    PreciseVector center{};
    std::array<PreciseVector, 3> axes{};
    std::array<double, 3> half_extents{};
    std::array<PreciseVector, 8> vertices{};
};

struct Projection final {
    double minimum{};
    double maximum{};
};

struct Polygon final {
    std::array<PreciseVector, clipping_polygon_capacity> vertices{};
    std::size_t count{};
};

struct RawContactPoint final {
    PreciseVector first{};
    PreciseVector second{};
    PreciseVector position{};
    double separation{};
};

struct RawContactSet final {
    std::array<RawContactPoint, clipping_polygon_capacity> points{};
    std::size_t count{};
};

struct BoxBoxAxis final {
    PreciseVector normal{};
    double separation{};
    BoxBoxSatFeature feature{BoxBoxSatFeature::first_face};
    std::uint8_t first_axis_index{};
    std::uint8_t second_axis_index{};
};

struct BoxBoxSatResult final {
    bool separated{};
    std::optional<BoxBoxAxis> axis;
};

struct BoxTerrainAxis final {
    PreciseVector normal{};
    double comparison_separation{};
    double separation{};
    BoxTerrainSatFeature feature{BoxTerrainSatFeature::terrain_face};
    std::uint8_t box_axis_index{};
    std::uint8_t triangle_edge_index{};
};

struct BoxTerrainSatResult final {
    bool separated{};
    std::optional<BoxTerrainAxis> axis;
};

struct SegmentPair final {
    PreciseVector first{};
    PreciseVector second{};
};

[[nodiscard]] core::Error contact_error(
    const core::ErrorCode code,
    std::string message)
{
    return core::Error{
        core::ErrorCategory::simulation,
        code,
        std::move(message),
    };
}

[[nodiscard]] constexpr PreciseVector precise(
    const math::Float3 value) noexcept
{
    return {
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z),
    };
}

[[nodiscard]] constexpr PreciseVector add(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return {
        first[0] + second[0],
        first[1] + second[1],
        first[2] + second[2],
    };
}

[[nodiscard]] constexpr PreciseVector subtract(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return {
        first[0] - second[0],
        first[1] - second[1],
        first[2] - second[2],
    };
}

[[nodiscard]] constexpr PreciseVector scale(
    const PreciseVector value,
    const double scalar) noexcept
{
    return {
        value[0] * scalar,
        value[1] * scalar,
        value[2] * scalar,
    };
}

[[nodiscard]] constexpr double dot(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return first[0] * second[0] +
        first[1] * second[1] +
        first[2] * second[2];
}

[[nodiscard]] constexpr PreciseVector cross(
    const PreciseVector first,
    const PreciseVector second) noexcept
{
    return {
        first[1] * second[2] - first[2] * second[1],
        first[2] * second[0] - first[0] * second[2],
        first[0] * second[1] - first[1] * second[0],
    };
}

[[nodiscard]] constexpr double length_squared(
    const PreciseVector value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] PreciseVector normalize(
    const PreciseVector value) noexcept
{
    return scale(value, 1.0 / std::sqrt(length_squared(value)));
}

[[nodiscard]] bool representable_float(
    const double value) noexcept
{
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(std::numeric_limits<float>::max());
}

[[nodiscard]] bool representable(
    const PreciseVector value) noexcept
{
    return representable_float(value[0]) &&
        representable_float(value[1]) &&
        representable_float(value[2]);
}

[[nodiscard]] math::Float3 float3(
    const PreciseVector value) noexcept
{
    return {
        static_cast<float>(value[0]),
        static_cast<float>(value[1]),
        static_cast<float>(value[2]),
    };
}

[[nodiscard]] PreciseBox precise_box(
    const BoxWorldGeometry& geometry) noexcept
{
    PreciseBox result{
        .center = precise(geometry.center),
        .axes = {
            precise(geometry.axes[0]),
            precise(geometry.axes[1]),
            precise(geometry.axes[2]),
        },
        .half_extents = {
            static_cast<double>(geometry.half_extents.x),
            static_cast<double>(geometry.half_extents.y),
            static_cast<double>(geometry.half_extents.z),
        },
    };
    for (std::size_t index = 0; index < result.vertices.size(); ++index) {
        result.vertices[index] = precise(geometry.vertices[index]);
    }
    return result;
}

template<std::size_t Count>
[[nodiscard]] Projection project(
    const std::array<PreciseVector, Count>& vertices,
    const PreciseVector axis) noexcept
{
    auto minimum = dot(vertices[0], axis);
    auto maximum = minimum;
    for (std::size_t index = 1; index < Count; ++index) {
        const auto value = dot(vertices[index], axis);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return {minimum, maximum};
}

[[nodiscard]] double interval_separation(
    const Projection first,
    const Projection second) noexcept
{
    return std::max(
        second.minimum - first.maximum,
        first.minimum - second.maximum);
}

[[nodiscard]] PreciseVector canonicalize_sign(
    PreciseVector axis) noexcept
{
    for (const auto component : axis) {
        if (component > 0.0) {
            return axis;
        }
        if (component < 0.0) {
            return scale(axis, -1.0);
        }
    }
    return axis;
}

[[nodiscard]] PreciseVector orient_first_to_second(
    PreciseVector axis,
    const Projection first,
    const Projection second,
    const PreciseVector center_offset) noexcept
{
    const auto positive_gap = second.minimum - first.maximum;
    const auto negative_gap = first.minimum - second.maximum;
    if (positive_gap > negative_gap + axis_tie_tolerance) {
        return axis;
    }
    if (negative_gap > positive_gap + axis_tie_tolerance) {
        return scale(axis, -1.0);
    }
    const auto center_direction = dot(center_offset, axis);
    if (center_direction > axis_tie_tolerance) {
        return axis;
    }
    if (center_direction < -axis_tie_tolerance) {
        return scale(axis, -1.0);
    }
    return canonicalize_sign(axis);
}

[[nodiscard]] PreciseVector orient_terrain_tangent_to_box(
    PreciseVector axis,
    const Projection box,
    const Projection triangle,
    const PreciseVector box_center,
    const std::array<PreciseVector, 3>& triangle_vertices) noexcept
{
    const auto positive_gap = box.minimum - triangle.maximum;
    const auto negative_gap = triangle.minimum - box.maximum;
    if (positive_gap > negative_gap + axis_tie_tolerance) {
        return axis;
    }
    if (negative_gap > positive_gap + axis_tie_tolerance) {
        return scale(axis, -1.0);
    }
    const auto triangle_center = scale(
        add(add(triangle_vertices[0], triangle_vertices[1]),
            triangle_vertices[2]),
        1.0 / 3.0);
    const auto center_direction = dot(
        subtract(box_center, triangle_center),
        axis);
    if (center_direction > axis_tie_tolerance) {
        return axis;
    }
    if (center_direction < -axis_tie_tolerance) {
        return scale(axis, -1.0);
    }
    return canonicalize_sign(axis);
}

void consider_box_box_axis(
    const PreciseBox& first,
    const PreciseBox& second,
    const PreciseVector candidate,
    const BoxBoxSatFeature feature,
    const std::uint8_t first_axis_index,
    const std::uint8_t second_axis_index,
    BoxBoxSatResult& result) noexcept
{
    const auto candidate_length_squared = length_squared(candidate);
    if (candidate_length_squared <= parallel_relative_tolerance) {
        return;
    }
    const auto axis = normalize(candidate);
    const auto first_projection = project(first.vertices, axis);
    const auto second_projection = project(second.vertices, axis);
    const auto separation = interval_separation(
        first_projection,
        second_projection);
    if (separation > contact_tolerance_meters) {
        result.separated = true;
        return;
    }
    if (result.axis.has_value() &&
        separation <=
            result.axis->separation + axis_tie_tolerance) {
        return;
    }
    result.axis = BoxBoxAxis{
        .normal = orient_first_to_second(
            axis,
            first_projection,
            second_projection,
            subtract(second.center, first.center)),
        .separation = separation,
        .feature = feature,
        .first_axis_index = first_axis_index,
        .second_axis_index = second_axis_index,
    };
}

[[nodiscard]] BoxBoxSatResult select_box_box_axis(
    const PreciseBox& first,
    const PreciseBox& second) noexcept
{
    BoxBoxSatResult result;
    for (std::uint8_t axis = 0; axis < 3U; ++axis) {
        consider_box_box_axis(
            first,
            second,
            first.axes[axis],
            BoxBoxSatFeature::first_face,
            axis,
            0U,
            result);
        if (result.separated) {
            return result;
        }
    }
    for (std::uint8_t axis = 0; axis < 3U; ++axis) {
        consider_box_box_axis(
            first,
            second,
            second.axes[axis],
            BoxBoxSatFeature::second_face,
            0U,
            axis,
            result);
        if (result.separated) {
            return result;
        }
    }
    for (std::uint8_t first_axis = 0; first_axis < 3U; ++first_axis) {
        for (std::uint8_t second_axis = 0;
             second_axis < 3U;
             ++second_axis) {
            consider_box_box_axis(
                first,
                second,
                cross(
                    first.axes[first_axis],
                    second.axes[second_axis]),
                BoxBoxSatFeature::edge_pair,
                first_axis,
                second_axis,
                result);
            if (result.separated) {
                return result;
            }
        }
    }
    return result;
}

void consider_box_terrain_axis(
    const PreciseBox& box,
    const std::array<PreciseVector, 3>& triangle,
    const PreciseVector triangle_normal,
    const PreciseVector candidate,
    const BoxTerrainSatFeature feature,
    const std::uint8_t box_axis_index,
    const std::uint8_t triangle_edge_index,
    BoxTerrainSatResult& result) noexcept
{
    const auto candidate_length_squared = length_squared(candidate);
    if (candidate_length_squared <= parallel_relative_tolerance) {
        return;
    }
    auto axis = normalize(candidate);
    const auto initial_upward_alignment = dot(axis, triangle_normal);
    const auto face_tangent =
        std::abs(initial_upward_alignment) <= parallel_relative_tolerance;
    if (face_tangent) {
        const auto tangent_axis = subtract(
            axis,
            scale(triangle_normal, initial_upward_alignment));
        if (length_squared(tangent_axis) <= parallel_relative_tolerance) {
            return;
        }
        axis = normalize(tangent_axis);
    }
    const auto box_projection = project(box.vertices, axis);
    const auto triangle_projection = project(triangle, axis);
    const auto comparison_separation = interval_separation(
        box_projection,
        triangle_projection);
    if (comparison_separation > contact_tolerance_meters) {
        result.separated = true;
        return;
    }
    if (result.axis.has_value() &&
        comparison_separation <=
            result.axis->comparison_separation + axis_tie_tolerance) {
        return;
    }

    if (!face_tangent && initial_upward_alignment < 0.0) {
        axis = scale(axis, -1.0);
    }
    else if (face_tangent) {
        axis = orient_terrain_tangent_to_box(
            axis,
            box_projection,
            triangle_projection,
            box.center,
            triangle);
    }
    const auto oriented_box_projection = project(box.vertices, axis);
    const auto oriented_triangle_projection = project(triangle, axis);
    result.axis = BoxTerrainAxis{
        .normal = axis,
        .comparison_separation = comparison_separation,
        .separation = oriented_box_projection.minimum -
            oriented_triangle_projection.maximum,
        .feature = feature,
        .box_axis_index = box_axis_index,
        .triangle_edge_index = triangle_edge_index,
    };
}

[[nodiscard]] BoxTerrainSatResult select_box_terrain_axis(
    const PreciseBox& box,
    const std::array<PreciseVector, 3>& triangle,
    const PreciseVector triangle_normal) noexcept
{
    BoxTerrainSatResult result;
    consider_box_terrain_axis(
        box,
        triangle,
        triangle_normal,
        triangle_normal,
        BoxTerrainSatFeature::terrain_face,
        0U,
        0U,
        result);
    if (result.separated) {
        return result;
    }
    for (std::uint8_t axis = 0; axis < 3U; ++axis) {
        consider_box_terrain_axis(
            box,
            triangle,
            triangle_normal,
            box.axes[axis],
            BoxTerrainSatFeature::box_face,
            axis,
            0U,
            result);
        if (result.separated) {
            return result;
        }
    }
    for (std::uint8_t box_axis = 0; box_axis < 3U; ++box_axis) {
        for (std::uint8_t edge = 0; edge < 3U; ++edge) {
            const auto edge_direction = subtract(
                triangle[(edge + 1U) % 3U],
                triangle[edge]);
            consider_box_terrain_axis(
                box,
                triangle,
                triangle_normal,
                cross(box.axes[box_axis], normalize(edge_direction)),
                BoxTerrainSatFeature::edge_pair,
                box_axis,
                edge,
                result);
            if (result.separated) {
                return result;
            }
        }
    }
    return result;
}

[[nodiscard]] PreciseVector face_center(
    const PreciseBox& box,
    const std::size_t axis_index,
    const PreciseVector outward_normal) noexcept
{
    const auto sign = dot(box.axes[axis_index], outward_normal) >= 0.0
        ? 1.0
        : -1.0;
    return add(
        box.center,
        scale(
            box.axes[axis_index],
            sign * box.half_extents[axis_index]));
}

[[nodiscard]] std::size_t most_aligned_axis(
    const PreciseBox& box,
    const PreciseVector direction) noexcept
{
    std::size_t result = 0;
    auto best = std::abs(dot(box.axes[0], direction));
    for (std::size_t axis = 1; axis < 3U; ++axis) {
        const auto alignment = std::abs(dot(box.axes[axis], direction));
        if (alignment > best + axis_tie_tolerance) {
            best = alignment;
            result = axis;
        }
    }
    return result;
}

[[nodiscard]] Polygon box_face_polygon(
    const PreciseBox& box,
    const std::size_t axis_index,
    const PreciseVector outward_normal) noexcept
{
    const auto center = face_center(box, axis_index, outward_normal);
    const auto first_tangent = (axis_index + 1U) % 3U;
    const auto second_tangent = (axis_index + 2U) % 3U;
    const auto first_offset = scale(
        box.axes[first_tangent],
        box.half_extents[first_tangent]);
    const auto second_offset = scale(
        box.axes[second_tangent],
        box.half_extents[second_tangent]);
    return Polygon{
        .vertices = {
            subtract(subtract(center, first_offset), second_offset),
            add(subtract(center, second_offset), first_offset),
            add(add(center, first_offset), second_offset),
            add(subtract(center, first_offset), second_offset),
        },
        .count = 4U,
    };
}

[[nodiscard]] Polygon incident_box_face(
    const PreciseBox& box,
    const PreciseVector reference_outward_normal) noexcept
{
    const auto axis_index = most_aligned_axis(
        box,
        reference_outward_normal);
    const auto projection = dot(
        box.axes[axis_index],
        reference_outward_normal);
    const auto outward = scale(
        box.axes[axis_index],
        projection > 0.0 ? -1.0 : 1.0);
    return box_face_polygon(box, axis_index, outward);
}

[[nodiscard]] Polygon clip_polygon(
    const Polygon& input,
    const PreciseVector plane_normal,
    const double plane_offset) noexcept
{
    Polygon output;
    if (input.count == 0U) {
        return output;
    }

    auto previous = input.vertices[input.count - 1U];
    auto previous_distance = dot(previous, plane_normal) - plane_offset;
    auto previous_inside = previous_distance <= contact_tolerance_meters;
    for (std::size_t index = 0; index < input.count; ++index) {
        const auto current = input.vertices[index];
        const auto current_distance = dot(current, plane_normal) - plane_offset;
        const auto current_inside =
            current_distance <= contact_tolerance_meters;
        if (current_inside != previous_inside) {
            const auto denominator = previous_distance - current_distance;
            const auto parameter = std::clamp(
                previous_distance / denominator,
                0.0,
                1.0);
            if (output.count < output.vertices.size()) {
                output.vertices[output.count] = add(
                    previous,
                    scale(subtract(current, previous), parameter));
                ++output.count;
            }
        }
        if (current_inside && output.count < output.vertices.size()) {
            output.vertices[output.count] = current;
            ++output.count;
        }
        previous = current;
        previous_distance = current_distance;
        previous_inside = current_inside;
    }
    return output;
}

[[nodiscard]] Polygon clip_to_box_face(
    Polygon polygon,
    const PreciseBox& reference,
    const std::size_t reference_axis,
    const PreciseVector reference_center) noexcept
{
    const auto first_tangent = (reference_axis + 1U) % 3U;
    const auto second_tangent = (reference_axis + 2U) % 3U;
    const std::array<std::size_t, 4> tangent_indices{
        first_tangent,
        first_tangent,
        second_tangent,
        second_tangent,
    };
    const std::array<double, 4> signs{-1.0, 1.0, -1.0, 1.0};
    for (std::size_t plane = 0; plane < 4U; ++plane) {
        const auto tangent = tangent_indices[plane];
        const auto normal = scale(reference.axes[tangent], signs[plane]);
        const auto offset = dot(reference_center, normal) +
            reference.half_extents[tangent];
        polygon = clip_polygon(polygon, normal, offset);
    }
    return polygon;
}

[[nodiscard]] Polygon clip_to_triangle(
    Polygon polygon,
    const std::array<PreciseVector, 3>& triangle,
    const PreciseVector normal) noexcept
{
    for (std::size_t edge = 0; edge < 3U; ++edge) {
        const auto first = triangle[edge];
        const auto edge_direction = subtract(
            triangle[(edge + 1U) % 3U],
            first);
        const auto outward = normalize(cross(edge_direction, normal));
        polygon = clip_polygon(polygon, outward, dot(first, outward));
    }
    return polygon;
}

[[nodiscard]] PreciseVector support_point(
    const PreciseBox& box,
    const PreciseVector direction) noexcept
{
    auto result = box.center;
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        const auto sign = dot(box.axes[axis], direction) >= 0.0
            ? 1.0
            : -1.0;
        result = add(
            result,
            scale(
                box.axes[axis],
                sign * box.half_extents[axis]));
    }
    return result;
}

[[nodiscard]] std::array<PreciseVector, 2> support_edge(
    const PreciseBox& box,
    const std::size_t edge_axis,
    const PreciseVector direction) noexcept
{
    auto center = box.center;
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (axis == edge_axis) {
            continue;
        }
        const auto sign = dot(box.axes[axis], direction) >= 0.0
            ? 1.0
            : -1.0;
        center = add(
            center,
            scale(
                box.axes[axis],
                sign * box.half_extents[axis]));
    }
    const auto offset = scale(
        box.axes[edge_axis],
        box.half_extents[edge_axis]);
    return {subtract(center, offset), add(center, offset)};
}

[[nodiscard]] SegmentPair closest_segment_points(
    const std::array<PreciseVector, 2>& first_segment,
    const std::array<PreciseVector, 2>& second_segment) noexcept
{
    const auto first_direction = subtract(
        first_segment[1],
        first_segment[0]);
    const auto second_direction = subtract(
        second_segment[1],
        second_segment[0]);
    const auto offset = subtract(first_segment[0], second_segment[0]);
    const auto first_length_squared = length_squared(first_direction);
    const auto second_length_squared = length_squared(second_direction);
    const auto directions_projection = dot(
        first_direction,
        second_direction);
    const auto first_offset_projection = dot(first_direction, offset);
    const auto second_offset_projection = dot(second_direction, offset);
    const auto denominator =
        first_length_squared * second_length_squared -
        directions_projection * directions_projection;

    auto first_parameter = 0.0;
    if (denominator >
        parallel_relative_tolerance *
            first_length_squared * second_length_squared) {
        first_parameter = std::clamp(
            (directions_projection * second_offset_projection -
             first_offset_projection * second_length_squared) /
                denominator,
            0.0,
            1.0);
    }
    auto second_parameter =
        (directions_projection * first_parameter +
         second_offset_projection) /
        second_length_squared;
    if (second_parameter < 0.0) {
        second_parameter = 0.0;
        first_parameter = std::clamp(
            -first_offset_projection / first_length_squared,
            0.0,
            1.0);
    }
    else if (second_parameter > 1.0) {
        second_parameter = 1.0;
        first_parameter = std::clamp(
            (directions_projection - first_offset_projection) /
                first_length_squared,
            0.0,
            1.0);
    }
    return {
        add(
            first_segment[0],
            scale(first_direction, first_parameter)),
        add(
            second_segment[0],
            scale(second_direction, second_parameter)),
    };
}

void append_raw_contact(
    RawContactSet& contacts,
    const PreciseVector first,
    const PreciseVector second,
    const PreciseVector normal) noexcept
{
    const auto position = add(scale(first, 0.5), scale(second, 0.5));
    for (std::size_t index = 0; index < contacts.count; ++index) {
        if (length_squared(subtract(
                contacts.points[index].position,
                position)) <= coincident_distance_squared) {
            return;
        }
    }
    if (contacts.count >= contacts.points.size()) {
        return;
    }
    contacts.points[contacts.count] = RawContactPoint{
        .first = first,
        .second = second,
        .position = position,
        .separation = dot(subtract(second, first), normal),
    };
    ++contacts.count;
}

[[nodiscard]] RawContactSet reduce_contacts(
    const RawContactSet& input) noexcept
{
    if (input.count <= box_contact_manifold_capacity) {
        return input;
    }

    std::array<std::size_t, box_contact_manifold_capacity> selected{};
    std::array<bool, clipping_polygon_capacity> used{};
    std::size_t deepest = 0U;
    for (std::size_t index = 1; index < input.count; ++index) {
        if (input.points[index].separation <
            input.points[deepest].separation) {
            deepest = index;
        }
    }
    selected[0] = deepest;
    used[deepest] = true;

    std::size_t farthest = deepest;
    auto farthest_distance = -1.0;
    for (std::size_t index = 0; index < input.count; ++index) {
        if (used[index]) {
            continue;
        }
        const auto distance = length_squared(subtract(
            input.points[index].position,
            input.points[deepest].position));
        if (distance > farthest_distance) {
            farthest = index;
            farthest_distance = distance;
        }
    }
    selected[1] = farthest;
    used[farthest] = true;

    std::size_t widest = deepest;
    auto widest_area = -1.0;
    const auto first_edge = subtract(
        input.points[farthest].position,
        input.points[deepest].position);
    for (std::size_t index = 0; index < input.count; ++index) {
        if (used[index]) {
            continue;
        }
        const auto second_edge = subtract(
            input.points[index].position,
            input.points[deepest].position);
        const auto area = length_squared(cross(first_edge, second_edge));
        if (area > widest_area) {
            widest = index;
            widest_area = area;
        }
    }
    selected[2] = widest;
    used[widest] = true;

    std::size_t fourth = deepest;
    auto fourth_distance = -1.0;
    for (std::size_t index = 0; index < input.count; ++index) {
        if (used[index]) {
            continue;
        }
        auto minimum_distance = std::numeric_limits<double>::max();
        for (std::size_t selected_index = 0;
             selected_index < 3U;
             ++selected_index) {
            minimum_distance = std::min(
                minimum_distance,
                length_squared(subtract(
                    input.points[index].position,
                    input.points[selected[selected_index]].position)));
        }
        if (minimum_distance > fourth_distance) {
            fourth = index;
            fourth_distance = minimum_distance;
        }
    }
    selected[3] = fourth;
    std::sort(selected.begin(), selected.end());

    RawContactSet result;
    result.count = box_contact_manifold_capacity;
    for (std::size_t index = 0; index < result.count; ++index) {
        result.points[index] = input.points[selected[index]];
    }
    return result;
}

[[nodiscard]] RawContactSet box_box_contact_points(
    const PreciseBox& first,
    const PreciseBox& second,
    const BoxBoxAxis& axis) noexcept
{
    RawContactSet contacts;
    if (axis.feature == BoxBoxSatFeature::first_face) {
        const auto reference_center = face_center(
            first,
            axis.first_axis_index,
            axis.normal);
        auto polygon = incident_box_face(second, axis.normal);
        polygon = clip_to_box_face(
            polygon,
            first,
            axis.first_axis_index,
            reference_center);
        for (std::size_t index = 0; index < polygon.count; ++index) {
            const auto second_point = polygon.vertices[index];
            const auto separation = dot(
                subtract(second_point, reference_center),
                axis.normal);
            if (separation <= contact_tolerance_meters) {
                append_raw_contact(
                    contacts,
                    subtract(
                        second_point,
                        scale(axis.normal, separation)),
                    second_point,
                    axis.normal);
            }
        }
    }
    else if (axis.feature == BoxBoxSatFeature::second_face) {
        const auto reference_outward = scale(axis.normal, -1.0);
        const auto reference_center = face_center(
            second,
            axis.second_axis_index,
            reference_outward);
        auto polygon = incident_box_face(first, reference_outward);
        polygon = clip_to_box_face(
            polygon,
            second,
            axis.second_axis_index,
            reference_center);
        for (std::size_t index = 0; index < polygon.count; ++index) {
            const auto first_point = polygon.vertices[index];
            const auto separation = dot(
                subtract(first_point, reference_center),
                reference_outward);
            if (separation <= contact_tolerance_meters) {
                append_raw_contact(
                    contacts,
                    first_point,
                    subtract(
                        first_point,
                        scale(reference_outward, separation)),
                    axis.normal);
            }
        }
    }
    else {
        const auto first_edge = support_edge(
            first,
            axis.first_axis_index,
            axis.normal);
        const auto second_edge = support_edge(
            second,
            axis.second_axis_index,
            scale(axis.normal, -1.0));
        const auto closest = closest_segment_points(
            first_edge,
            second_edge);
        append_raw_contact(
            contacts,
            closest.first,
            closest.second,
            axis.normal);
    }

    if (contacts.count == 0U) {
        append_raw_contact(
            contacts,
            support_point(first, axis.normal),
            support_point(second, scale(axis.normal, -1.0)),
            axis.normal);
    }
    return reduce_contacts(contacts);
}

[[nodiscard]] std::array<PreciseVector, 3> precise_triangle(
    const terrain::HeightTileTriangleGeometry& triangle) noexcept
{
    return {
        precise(triangle.positions[0]),
        precise(triangle.positions[1]),
        precise(triangle.positions[2]),
    };
}

[[nodiscard]] PreciseVector triangle_support_point(
    const std::array<PreciseVector, 3>& triangle,
    const PreciseVector direction) noexcept
{
    std::size_t selected = 0;
    auto best = dot(triangle[0], direction);
    for (std::size_t index = 1; index < 3U; ++index) {
        const auto value = dot(triangle[index], direction);
        if (value > best) {
            best = value;
            selected = index;
        }
    }
    return triangle[selected];
}

[[nodiscard]] RawContactSet box_terrain_contact_points(
    const PreciseBox& box,
    const std::array<PreciseVector, 3>& triangle,
    const BoxTerrainAxis& axis) noexcept
{
    RawContactSet contacts;
    if (axis.feature == BoxTerrainSatFeature::terrain_face) {
        auto polygon = incident_box_face(
            box,
            axis.normal);
        polygon = clip_to_triangle(polygon, triangle, axis.normal);
        for (std::size_t index = 0; index < polygon.count; ++index) {
            const auto box_point = polygon.vertices[index];
            const auto separation = dot(
                subtract(box_point, triangle[0]),
                axis.normal);
            if (separation <= contact_tolerance_meters) {
                append_raw_contact(
                    contacts,
                    box_point,
                    subtract(
                        box_point,
                        scale(axis.normal, separation)),
                    scale(axis.normal, -1.0));
            }
        }
    }
    else if (axis.feature == BoxTerrainSatFeature::box_face) {
        const auto reference_outward = scale(axis.normal, -1.0);
        const auto reference_center = face_center(
            box,
            axis.box_axis_index,
            reference_outward);
        Polygon polygon{
            .vertices = {
                triangle[0],
                triangle[1],
                triangle[2],
            },
            .count = 3U,
        };
        polygon = clip_to_box_face(
            polygon,
            box,
            axis.box_axis_index,
            reference_center);
        for (std::size_t index = 0; index < polygon.count; ++index) {
            const auto terrain_point = polygon.vertices[index];
            const auto separation = dot(
                subtract(terrain_point, reference_center),
                reference_outward);
            if (separation <= contact_tolerance_meters) {
                append_raw_contact(
                    contacts,
                    subtract(
                        terrain_point,
                        scale(reference_outward, separation)),
                    terrain_point,
                    scale(axis.normal, -1.0));
            }
        }
    }
    else {
        const auto box_edge = support_edge(
            box,
            axis.box_axis_index,
            scale(axis.normal, -1.0));
        const std::array<PreciseVector, 2> triangle_edge{
            triangle[axis.triangle_edge_index],
            triangle[(axis.triangle_edge_index + 1U) % 3U],
        };
        const auto closest = closest_segment_points(
            box_edge,
            triangle_edge);
        append_raw_contact(
            contacts,
            closest.first,
            closest.second,
            scale(axis.normal, -1.0));
    }

    if (contacts.count == 0U) {
        append_raw_contact(
            contacts,
            support_point(box, scale(axis.normal, -1.0)),
            triangle_support_point(triangle, axis.normal),
            scale(axis.normal, -1.0));
    }
    return reduce_contacts(contacts);
}

[[nodiscard]] bool valid_raw_contact(
    const RawContactPoint& point) noexcept
{
    return representable(point.first) &&
        representable(point.second) &&
        representable(point.position) &&
        representable_float(point.separation) &&
        representable_float(std::max(0.0, -point.separation));
}

[[nodiscard]] core::Result<BoxBoxContactManifold>
make_box_box_manifold(
    const PreciseBox& first,
    const PreciseBox& second,
    const BoxBoxAxis& axis)
{
    const auto raw = box_box_contact_points(first, second, axis);
    if (!representable(axis.normal) ||
        raw.count == 0U ||
        raw.count > box_contact_manifold_capacity) {
        return core::Result<BoxBoxContactManifold>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Box pair manifold exceeded finite range or capacity"));
    }

    BoxBoxContactManifold manifold{
        .normal = float3(axis.normal),
        .feature = axis.feature,
        .first_axis_index = axis.first_axis_index,
        .second_axis_index = axis.second_axis_index,
    };
    auto aggregate_separation = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < raw.count; ++index) {
        const auto& source = raw.points[index];
        if (!valid_raw_contact(source)) {
            return core::Result<BoxBoxContactManifold>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Box pair witnesses exceeded finite float range"));
        }
        const BoxContactPoint point{
            .point_on_first = float3(source.first),
            .point_on_second = float3(source.second),
            .position = float3(source.position),
            .separation = static_cast<float>(source.separation),
            .penetration_depth = static_cast<float>(
                std::max(0.0, -source.separation)),
        };
        auto duplicate = false;
        for (std::size_t prior = 0;
             prior < manifold.point_count;
             ++prior) {
            if (manifold.points[prior].position == point.position) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            manifold.points[manifold.point_count] = point;
            ++manifold.point_count;
            aggregate_separation = std::min(
                aggregate_separation,
                source.separation);
        }
    }
    if (manifold.point_count == 0U ||
        !representable_float(aggregate_separation) ||
        !representable_float(std::max(0.0, -aggregate_separation))) {
        return core::Result<BoxBoxContactManifold>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Box pair aggregate contact exceeded finite range"));
    }
    manifold.separation = static_cast<float>(aggregate_separation);
    manifold.penetration_depth = static_cast<float>(
        std::max(0.0, -aggregate_separation));
    return core::Result<BoxBoxContactManifold>::success(manifold);
}

[[nodiscard]] std::optional<PreciseVector> barycentrics(
    const std::array<PreciseVector, 3>& triangle,
    const PreciseVector point) noexcept
{
    // Canonical height-tile triangles are always nondegenerate in X/Z. Using
    // that projection avoids cancellation from arbitrarily large Y relief.
    const auto first_edge_x = triangle[1][0] - triangle[0][0];
    const auto first_edge_z = triangle[1][2] - triangle[0][2];
    const auto second_edge_x = triangle[2][0] - triangle[0][0];
    const auto second_edge_z = triangle[2][2] - triangle[0][2];
    const auto offset_x = point[0] - triangle[0][0];
    const auto offset_z = point[2] - triangle[0][2];
    const auto denominator =
        first_edge_x * second_edge_z -
        second_edge_x * first_edge_z;
    if (!std::isfinite(denominator) || denominator == 0.0) {
        return std::nullopt;
    }
    const auto second =
        (offset_x * second_edge_z -
         second_edge_x * offset_z) /
        denominator;
    const auto third =
        (first_edge_x * offset_z -
         offset_x * first_edge_z) /
        denominator;
    const auto first = 1.0 - second - third;
    auto result = PreciseVector{
        std::clamp(first, 0.0, 1.0),
        std::clamp(second, 0.0, 1.0),
        std::clamp(third, 0.0, 1.0),
    };
    const auto total = result[0] + result[1] + result[2];
    if (!std::isfinite(total) || total <= 0.0) {
        return std::nullopt;
    }
    result = scale(result, 1.0 / total);
    if (!representable(result)) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] core::Result<BoxTerrainContactManifold>
make_box_terrain_manifold(
    const PreciseBox& box,
    const terrain::HeightTileTriangleGeometry& source_triangle,
    const BoxTerrainAxis& axis)
{
    const auto triangle = precise_triangle(source_triangle);
    const auto raw = box_terrain_contact_points(box, triangle, axis);
    if (!representable(axis.normal) ||
        raw.count == 0U ||
        raw.count > box_contact_manifold_capacity) {
        return core::Result<BoxTerrainContactManifold>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Box terrain manifold exceeded finite range or capacity"));
    }

    BoxTerrainContactManifold manifold{
        .normal = float3(axis.normal),
        .feature = axis.feature,
        .box_axis_index = axis.box_axis_index,
        .triangle_edge_index = axis.triangle_edge_index,
    };
    auto aggregate_separation = std::numeric_limits<double>::max();
    for (std::size_t index = 0; index < raw.count; ++index) {
        const auto& source = raw.points[index];
        const auto barycentric = barycentrics(triangle, source.second);
        if (!barycentric.has_value()) {
            return core::Result<BoxTerrainContactManifold>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Box terrain witnesses exceeded finite float range"));
        }
        auto terrain_point = PreciseVector{};
        for (std::size_t vertex = 0; vertex < 3U; ++vertex) {
            terrain_point = add(
                terrain_point,
                scale(triangle[vertex], (*barycentric)[vertex]));
        }
        const RawContactPoint adjusted{
            .first = source.first,
            .second = terrain_point,
            .position = add(
                scale(source.first, 0.5),
                scale(terrain_point, 0.5)),
            .separation = dot(
                subtract(source.first, terrain_point),
                axis.normal),
        };
        if (!valid_raw_contact(adjusted)) {
            return core::Result<BoxTerrainContactManifold>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Box terrain witnesses exceeded finite float range"));
        }
        const BoxTerrainContactPoint point{
            .box_point = float3(adjusted.first),
            .surface = terrain::HeightTileSurfaceSample{
                .position = float3(adjusted.second),
                .normal = source_triangle.normal,
                .cell_x = source_triangle.cell_x,
                .cell_z = source_triangle.cell_z,
                .triangle = source_triangle.triangle,
                .barycentrics = float3(*barycentric),
            },
            .position = float3(adjusted.position),
            .separation = static_cast<float>(adjusted.separation),
            .penetration_depth = static_cast<float>(
                std::max(0.0, -adjusted.separation)),
        };
        auto duplicate = false;
        for (std::size_t prior = 0;
             prior < manifold.point_count;
             ++prior) {
            if (manifold.points[prior].position == point.position) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            manifold.points[manifold.point_count] = point;
            ++manifold.point_count;
            aggregate_separation = std::min(
                aggregate_separation,
                adjusted.separation);
        }
    }
    if (manifold.point_count == 0U ||
        !representable_float(aggregate_separation) ||
        !representable_float(std::max(0.0, -aggregate_separation))) {
        return core::Result<BoxTerrainContactManifold>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Box terrain aggregate contact exceeded finite range"));
    }
    manifold.separation = static_cast<float>(aggregate_separation);
    manifold.penetration_depth = static_cast<float>(
        std::max(0.0, -aggregate_separation));
    return core::Result<BoxTerrainContactManifold>::success(manifold);
}

[[nodiscard]] float outward_minimum(
    const float value) noexcept
{
    const auto desired = std::max(
        -static_cast<double>(std::numeric_limits<float>::max()),
        static_cast<double>(value) - contact_tolerance_meters);
    auto result = static_cast<float>(desired);
    if (static_cast<double>(result) > desired) {
        result = std::nextafter(
            result,
            -std::numeric_limits<float>::infinity());
    }
    return result;
}

[[nodiscard]] float outward_maximum(
    const float value) noexcept
{
    const auto desired = std::min(
        static_cast<double>(std::numeric_limits<float>::max()),
        static_cast<double>(value) + contact_tolerance_meters);
    auto result = static_cast<float>(desired);
    if (static_cast<double>(result) < desired) {
        result = std::nextafter(
            result,
            std::numeric_limits<float>::infinity());
    }
    return result;
}

} // namespace

core::Result<BoxWorldGeometry> make_box_world_geometry(
    const RigidBodyState& state,
    const BoxCollider& collider)
{
    if (!is_valid(state) || !is_valid(collider)) {
        return core::Result<BoxWorldGeometry>::failure(
            contact_error(
                core::ErrorCode::invalid_argument,
                "Box geometry requires a valid rigid state and finite "
                "strictly positive half extents"));
    }

    const auto quaternion = std::array<double, 4>{
        static_cast<double>(state.orientation.x),
        static_cast<double>(state.orientation.y),
        static_cast<double>(state.orientation.z),
        static_cast<double>(state.orientation.w),
    };
    const auto quaternion_length_squared =
        quaternion[0] * quaternion[0] +
        quaternion[1] * quaternion[1] +
        quaternion[2] * quaternion[2] +
        quaternion[3] * quaternion[3];
    if (!std::isfinite(quaternion_length_squared) ||
        quaternion_length_squared <= 0.0) {
        return core::Result<BoxWorldGeometry>::failure(
            contact_error(
                core::ErrorCode::unavailable,
                "Box orientation normalization exceeded finite range"));
    }
    const auto inverse_length = 1.0 / std::sqrt(quaternion_length_squared);
    const auto x = quaternion[0] * inverse_length;
    const auto y = quaternion[1] * inverse_length;
    const auto z = quaternion[2] * inverse_length;
    const auto w = quaternion[3] * inverse_length;
    const std::array<PreciseVector, 3> axes{
        PreciseVector{
            1.0 - 2.0 * (y * y + z * z),
            2.0 * (x * y + w * z),
            2.0 * (x * z - w * y),
        },
        PreciseVector{
            2.0 * (x * y - w * z),
            1.0 - 2.0 * (x * x + z * z),
            2.0 * (y * z + w * x),
        },
        PreciseVector{
            2.0 * (x * z + w * y),
            2.0 * (y * z - w * x),
            1.0 - 2.0 * (x * x + y * y),
        },
    };
    for (const auto axis : axes) {
        if (!representable(axis)) {
            return core::Result<BoxWorldGeometry>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Box world axes exceeded finite float range"));
        }
    }

    const auto center = precise(state.position);
    const std::array<double, 3> half_extents{
        static_cast<double>(collider.local_half_extents.x),
        static_cast<double>(collider.local_half_extents.y),
        static_cast<double>(collider.local_half_extents.z),
    };
    std::array<PreciseVector, 8> vertices{};
    auto minimum = PreciseVector{
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
    };
    auto maximum = PreciseVector{
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
    };
    for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex) {
        auto position = center;
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            const auto sign =
                (vertex & (std::size_t{1} << axis)) != 0U
                ? 1.0
                : -1.0;
            position = add(
                position,
                scale(axes[axis], sign * half_extents[axis]));
        }
        if (!representable(position)) {
            return core::Result<BoxWorldGeometry>::failure(
                contact_error(
                    core::ErrorCode::unavailable,
                    "Box world vertices exceeded finite float range"));
        }
        vertices[vertex] = position;
        for (std::size_t component = 0; component < 3U; ++component) {
            minimum[component] = std::min(
                minimum[component],
                position[component]);
            maximum[component] = std::max(
                maximum[component],
                position[component]);
        }
    }

    std::array<math::Float3, 8> stored_vertices{};
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        stored_vertices[index] = float3(vertices[index]);
    }
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        for (std::size_t vertex = 0;
             vertex < stored_vertices.size();
             ++vertex) {
            if ((vertex & (std::size_t{1} << axis)) != 0U) {
                continue;
            }
            const auto opposite = vertex | (std::size_t{1} << axis);
            if (stored_vertices[vertex] == stored_vertices[opposite]) {
                return core::Result<BoxWorldGeometry>::failure(
                    contact_error(
                        core::ErrorCode::unavailable,
                        "Box world extent collapsed at float precision"));
            }
        }
    }

    BoxWorldGeometry result{
        .center = state.position,
        .axes = {
            float3(axes[0]),
            float3(axes[1]),
            float3(axes[2]),
        },
        .half_extents = collider.local_half_extents,
        .bounds = terrain::Bounds3{
            .minimum = float3(minimum),
            .maximum = float3(maximum),
        },
    };
    result.vertices = stored_vertices;
    return core::Result<BoxWorldGeometry>::success(result);
}

core::Result<std::optional<BoxBoxContactManifold>>
query_box_box_contact(
    const RigidBodyState& first_state,
    const BoxCollider& first,
    const RigidBodyState& second_state,
    const BoxCollider& second)
{
    auto first_geometry_result = make_box_world_geometry(first_state, first);
    if (!first_geometry_result) {
        return core::Result<
            std::optional<BoxBoxContactManifold>>::failure(
                std::move(first_geometry_result).error());
    }
    auto second_geometry_result = make_box_world_geometry(second_state, second);
    if (!second_geometry_result) {
        return core::Result<
            std::optional<BoxBoxContactManifold>>::failure(
                std::move(second_geometry_result).error());
    }

    const auto first_box = precise_box(first_geometry_result.value());
    const auto second_box = precise_box(second_geometry_result.value());
    const auto sat = select_box_box_axis(first_box, second_box);
    if (sat.separated) {
        return core::Result<
            std::optional<BoxBoxContactManifold>>::success(std::nullopt);
    }
    if (!sat.axis.has_value()) {
        return core::Result<
            std::optional<BoxBoxContactManifold>>::failure(
                contact_error(
                    core::ErrorCode::invalid_state,
                    "Box pair SAT produced no usable axis"));
    }
    auto manifold_result = make_box_box_manifold(
        first_box,
        second_box,
        *sat.axis);
    if (!manifold_result) {
        return core::Result<
            std::optional<BoxBoxContactManifold>>::failure(
                std::move(manifold_result).error());
    }
    return core::Result<
        std::optional<BoxBoxContactManifold>>::success(
            std::move(manifold_result).value());
}

core::Result<std::optional<BoxTerrainContactManifold>>
query_box_terrain_contact(
    const RigidBodyState& box_state,
    const BoxCollider& box,
    const terrain::HeightTileSurface& terrain_surface)
{
    auto geometry_result = make_box_world_geometry(box_state, box);
    if (!geometry_result) {
        return core::Result<
            std::optional<BoxTerrainContactManifold>>::failure(
                std::move(geometry_result).error());
    }
    const auto& geometry = geometry_result.value();
    const terrain::Bounds3 expanded_bounds{
        .minimum = {
            outward_minimum(geometry.bounds.minimum.x),
            outward_minimum(geometry.bounds.minimum.y),
            outward_minimum(geometry.bounds.minimum.z),
        },
        .maximum = {
            outward_maximum(geometry.bounds.maximum.x),
            outward_maximum(geometry.bounds.maximum.y),
            outward_maximum(geometry.bounds.maximum.z),
        },
    };
    auto triangles_result =
        terrain_surface.lod0_triangles_overlapping_bounds(expanded_bounds);
    if (!triangles_result) {
        return core::Result<
            std::optional<BoxTerrainContactManifold>>::failure(
                std::move(triangles_result).error());
    }

    const auto precise_geometry = precise_box(geometry);
    std::optional<BoxTerrainContactManifold> selected;
    for (const auto& triangle : triangles_result.value()) {
        if (!math::is_finite(triangle.normal) ||
            triangle.normal.y <= 0.0F) {
            return core::Result<
                std::optional<BoxTerrainContactManifold>>::failure(
                    contact_error(
                        core::ErrorCode::invalid_state,
                        "Canonical terrain returned an invalid triangle"));
        }
        const auto precise_positions = precise_triangle(triangle);
        const auto triangle_normal = normalize(precise(triangle.normal));
        const auto sat = select_box_terrain_axis(
            precise_geometry,
            precise_positions,
            triangle_normal);
        if (sat.separated) {
            continue;
        }
        if (!sat.axis.has_value()) {
            return core::Result<
                std::optional<BoxTerrainContactManifold>>::failure(
                    contact_error(
                        core::ErrorCode::invalid_state,
                        "Box terrain SAT produced no usable axis"));
        }
        auto candidate_result = make_box_terrain_manifold(
            precise_geometry,
            triangle,
            *sat.axis);
        if (!candidate_result) {
            return core::Result<
                std::optional<BoxTerrainContactManifold>>::failure(
                    std::move(candidate_result).error());
        }
        auto candidate = std::move(candidate_result).value();
        const auto replace = !selected.has_value() ||
            static_cast<double>(candidate.penetration_depth) >
                static_cast<double>(selected->penetration_depth) +
                    axis_tie_tolerance ||
            (std::abs(
                static_cast<double>(candidate.penetration_depth) -
                static_cast<double>(selected->penetration_depth)) <=
                    axis_tie_tolerance &&
             static_cast<double>(candidate.separation) <
                static_cast<double>(selected->separation) -
                    axis_tie_tolerance);
        if (replace) {
            selected = std::move(candidate);
        }
    }
    return core::Result<
        std::optional<BoxTerrainContactManifold>>::success(
            std::move(selected));
}

} // namespace shark::physics
