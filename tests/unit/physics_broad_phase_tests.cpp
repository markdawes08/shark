#include <shark/physics/broad_phase.hpp>
#include <shark/simulation/fixed_step_clock.hpp>

#include <shark/core/error.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <tuple>
#include <utility>

namespace {

using shark::math::Float3;
using shark::physics::BroadPhaseBounds;
using shark::physics::BroadPhaseCandidate;
using shark::physics::BroadPhaseProxy;
using shark::physics::BroadPhaseStep;

[[nodiscard]] constexpr BroadPhaseProxy make_proxy(
    const std::uint64_t body_id,
    const std::size_t body_index,
    const Float3 minimum,
    const Float3 maximum) noexcept
{
    return {
        .body_id = body_id,
        .body_index = body_index,
        .bounds = {
            .minimum = minimum,
            .maximum = maximum,
        },
    };
}

[[nodiscard]] constexpr bool intervals_overlap(
    const float first_minimum,
    const float first_maximum,
    const float second_minimum,
    const float second_maximum) noexcept
{
    return first_minimum <= second_maximum &&
        second_minimum <= first_maximum;
}

[[nodiscard]] constexpr bool bounds_overlap(
    const BroadPhaseBounds& first,
    const BroadPhaseBounds& second) noexcept
{
    return intervals_overlap(
               first.minimum.x,
               first.maximum.x,
               second.minimum.x,
               second.maximum.x) &&
        intervals_overlap(
               first.minimum.y,
               first.maximum.y,
               second.minimum.y,
               second.maximum.y) &&
        intervals_overlap(
               first.minimum.z,
               first.maximum.z,
               second.minimum.z,
               second.maximum.z);
}

[[nodiscard]] constexpr BroadPhaseCandidate canonical_candidate(
    const BroadPhaseProxy& first,
    const BroadPhaseProxy& second) noexcept
{
    if (first.body_id < second.body_id) {
        return {
            .first_body_id = first.body_id,
            .second_body_id = second.body_id,
            .first_body_index = first.body_index,
            .second_body_index = second.body_index,
        };
    }
    return {
        .first_body_id = second.body_id,
        .second_body_id = first.body_id,
        .first_body_index = second.body_index,
        .second_body_index = first.body_index,
    };
}

[[nodiscard]] constexpr bool candidate_less(
    const BroadPhaseCandidate& first,
    const BroadPhaseCandidate& second) noexcept
{
    return first.first_body_id < second.first_body_id ||
        (first.first_body_id == second.first_body_id &&
         first.second_body_id < second.second_body_id);
}

[[nodiscard]] BroadPhaseStep brute_force_oracle(
    const std::span<const BroadPhaseProxy> proxies)
{
    BroadPhaseStep step{
        .proxy_count = proxies.size(),
        .possible_pair_count =
            proxies.size() * (proxies.size() -
                (proxies.empty() ? 0U : 1U)) / 2U,
    };
    for (std::size_t first_index = 0;
         first_index < proxies.size();
         ++first_index) {
        for (std::size_t second_index = first_index + 1U;
             second_index < proxies.size();
             ++second_index) {
            const auto& first = proxies[first_index];
            const auto& second = proxies[second_index];
            if (intervals_overlap(
                    first.bounds.minimum.x,
                    first.bounds.maximum.x,
                    second.bounds.minimum.x,
                    second.bounds.maximum.x)) {
                ++step.x_overlap_pair_count;
            }
            if (!bounds_overlap(first.bounds, second.bounds)) {
                continue;
            }
            REQUIRE(
                step.candidate_count <
                shark::physics::broad_phase_candidate_capacity);
            step.candidates[step.candidate_count] =
                canonical_candidate(first, second);
            ++step.candidate_count;
        }
    }
    std::sort(
        step.candidates.begin(),
        step.candidates.begin() +
            static_cast<std::ptrdiff_t>(step.candidate_count),
        candidate_less);
    return step;
}

[[nodiscard]] BroadPhaseStep require_matches_oracle(
    const std::span<const BroadPhaseProxy> proxies)
{
    const auto expected = brute_force_oracle(proxies);
    const auto result =
        shark::physics::generate_broad_phase_candidates(proxies);
    REQUIRE(result);
    const auto actual = result.value();
    REQUIRE(actual == expected);
    for (std::size_t pair_index = 0;
         pair_index < actual.candidate_count;
         ++pair_index) {
        const auto& candidate = actual.candidates[pair_index];
        REQUIRE(candidate.first_body_id < candidate.second_body_id);
        REQUIRE(
            candidate.first_body_index !=
            candidate.second_body_index);
        if (pair_index > 0U) {
            REQUIRE(candidate_less(
                actual.candidates[pair_index - 1U],
                candidate));
        }
    }
    return actual;
}

template<std::size_t proxy_count>
void require_invalid_transaction(
    const std::array<BroadPhaseProxy, proxy_count>& proxies)
{
    const auto before = proxies;
    const auto result =
        shark::physics::generate_broad_phase_candidates(proxies);
    REQUIRE_FALSE(result);
    REQUIRE(
        result.error().code() ==
        shark::core::ErrorCode::invalid_argument);
    REQUIRE(
        std::memcmp(
            proxies.data(),
            before.data(),
            sizeof(proxies)) == 0);

    constexpr std::array valid{
        make_proxy(1U, 10U, {}, {1.0F, 1.0F, 1.0F}),
        make_proxy(
            2U,
            20U,
            {1.0F, 0.0F, 0.0F},
            {2.0F, 1.0F, 1.0F}),
    };
    const auto baseline =
        shark::physics::generate_broad_phase_candidates(valid);
    REQUIRE(baseline);
    const auto after_failure =
        shark::physics::generate_broad_phase_candidates(valid);
    REQUIRE(after_failure);
    REQUIRE(after_failure.value() == baseline.value());
}

[[nodiscard]] std::uint32_t xorshift32(
    std::uint32_t& state) noexcept
{
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

[[nodiscard]] float lattice_coordinate(
    std::uint32_t& random) noexcept
{
    const auto coordinate =
        static_cast<std::int32_t>(xorshift32(random) % 65U) - 32;
    return static_cast<float>(coordinate) * 0.25F;
}

[[nodiscard]] std::array<
    BroadPhaseProxy,
    shark::physics::broad_phase_proxy_capacity>
make_seeded_proxies(
    const std::uint32_t seed,
    const std::size_t proxy_count)
{
    std::array<
        BroadPhaseProxy,
        shark::physics::broad_phase_proxy_capacity>
        proxies{};
    auto random = seed;
    for (std::size_t index = 0;
         index < proxy_count;
         ++index) {
        const Float3 minimum{
            lattice_coordinate(random),
            lattice_coordinate(random),
            lattice_coordinate(random),
        };
        const Float3 extent{
            static_cast<float>(xorshift32(random) % 9U) * 0.25F,
            static_cast<float>(xorshift32(random) % 9U) * 0.25F,
            static_cast<float>(xorshift32(random) % 9U) * 0.25F,
        };
        proxies[index] = make_proxy(
            (static_cast<std::uint64_t>(seed) << 32U) |
                static_cast<std::uint64_t>(index + 1U),
            1'000U + index * 17U,
            minimum,
            {
                minimum.x + extent.x,
                minimum.y + extent.y,
                minimum.z + extent.z,
            });
    }
    for (std::size_t index = proxy_count;
         index > 1U;
         --index) {
        const auto other =
            static_cast<std::size_t>(xorshift32(random)) % index;
        std::swap(proxies[index - 1U], proxies[other]);
    }
    return proxies;
}

struct MotionProxy final {
    std::uint64_t body_id{};
    std::size_t body_index{};
    std::array<std::int32_t, 3> center{};
    std::array<std::int32_t, 3> velocity{};
    std::array<std::int32_t, 3> half_extent{};

    [[nodiscard]] friend bool operator==(
        const MotionProxy&,
        const MotionProxy&) noexcept = default;
};

inline constexpr std::size_t moving_proxy_count = 32U;
inline constexpr std::int32_t moving_boundary = 64;

[[nodiscard]] std::array<MotionProxy, moving_proxy_count>
make_motion_proxies()
{
    std::array<MotionProxy, moving_proxy_count> motion{};
    auto random = std::uint32_t{0xA17E'91D3U};
    for (std::size_t index = 0;
         index < motion.size();
         ++index) {
        auto& proxy = motion[index];
        proxy.body_id =
            (std::uint64_t{1} << 32U) |
            static_cast<std::uint64_t>(index + 1U);
        proxy.body_index = 2'000U + index * 31U;
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            proxy.center[axis] =
                static_cast<std::int32_t>(
                    xorshift32(random) % 97U) - 48;
            const auto speed =
                static_cast<std::int32_t>(
                    xorshift32(random) % 5U) + 1;
            proxy.velocity[axis] =
                (xorshift32(random) & 1U) != 0U
                ? speed
                : -speed;
            proxy.half_extent[axis] =
                static_cast<std::int32_t>(
                    xorshift32(random) % 5U);
        }
    }
    return motion;
}

void advance_motion(
    std::array<MotionProxy, moving_proxy_count>& motion,
    const std::uint64_t fixed_tick)
{
    for (auto& proxy : motion) {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            proxy.center[axis] += proxy.velocity[axis];
            if (proxy.center[axis] > moving_boundary) {
                proxy.center[axis] =
                    moving_boundary -
                    (proxy.center[axis] - moving_boundary);
                proxy.velocity[axis] = -proxy.velocity[axis];
            }
            else if (proxy.center[axis] < -moving_boundary) {
                proxy.center[axis] =
                    -moving_boundary +
                    (-moving_boundary - proxy.center[axis]);
                proxy.velocity[axis] = -proxy.velocity[axis];
            }
        }
    }

    // Force one visible identity handoff in an overlapping pair while the
    // caller-owned storage index remains unchanged.
    if (fixed_tick == 79U || fixed_tick == 80U) {
        motion[7].center = motion[0].center;
        motion[7].half_extent = motion[0].half_extent;
    }
    if (fixed_tick == 80U) {
        motion[7].body_id =
            (std::uint64_t{2} << 32U) | 8U;
    }
}

[[nodiscard]] std::array<BroadPhaseProxy, moving_proxy_count>
motion_broad_phase_proxies(
    const std::array<MotionProxy, moving_proxy_count>& motion,
    const std::uint64_t fixed_tick)
{
    std::array<BroadPhaseProxy, moving_proxy_count> proxies{};
    for (std::size_t index = 0;
         index < motion.size();
         ++index) {
        const auto& source = motion[index];
        const auto minimum = Float3{
            static_cast<float>(
                source.center[0] - source.half_extent[0]) * 0.25F,
            static_cast<float>(
                source.center[1] - source.half_extent[1]) * 0.25F,
            static_cast<float>(
                source.center[2] - source.half_extent[2]) * 0.25F,
        };
        const auto maximum = Float3{
            static_cast<float>(
                source.center[0] + source.half_extent[0]) * 0.25F,
            static_cast<float>(
                source.center[1] + source.half_extent[1]) * 0.25F,
            static_cast<float>(
                source.center[2] + source.half_extent[2]) * 0.25F,
        };
        proxies[index] = make_proxy(
            source.body_id,
            source.body_index,
            minimum,
            maximum);
    }

    const auto rotation =
        static_cast<std::size_t>(fixed_tick % proxies.size());
    std::rotate(
        proxies.begin(),
        proxies.begin() + static_cast<std::ptrdiff_t>(rotation),
        proxies.end());
    if ((fixed_tick & 1U) != 0U) {
        std::reverse(proxies.begin(), proxies.end());
    }
    return proxies;
}

[[nodiscard]] constexpr std::uint64_t hash_value(
    std::uint64_t hash,
    const std::uint64_t value) noexcept
{
    for (std::size_t byte = 0; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 1'099'511'628'211ULL;
    }
    return hash;
}

struct MovingScheduleRun final {
    std::array<MotionProxy, moving_proxy_count> motion{};
    std::uint64_t fixed_step_count{};
    std::uint64_t proxy_count{};
    std::uint64_t possible_pair_count{};
    std::uint64_t x_overlap_pair_count{};
    std::uint64_t candidate_count{};
    std::uint64_t pair_hash{1'469'598'103'934'665'603ULL};

    [[nodiscard]] friend bool operator==(
        const MovingScheduleRun&,
        const MovingScheduleRun&) noexcept = default;
};

void accumulate_step(
    MovingScheduleRun& run,
    const BroadPhaseStep& step,
    const std::uint64_t fixed_tick)
{
    run.proxy_count += step.proxy_count;
    run.possible_pair_count += step.possible_pair_count;
    run.x_overlap_pair_count += step.x_overlap_pair_count;
    run.candidate_count += step.candidate_count;
    run.pair_hash = hash_value(run.pair_hash, fixed_tick);
    run.pair_hash = hash_value(
        run.pair_hash,
        static_cast<std::uint64_t>(step.candidate_count));
    for (std::size_t pair_index = 0;
         pair_index < step.candidate_count;
         ++pair_index) {
        run.pair_hash = hash_value(
            run.pair_hash,
            step.candidates[pair_index].first_body_id);
        run.pair_hash = hash_value(
            run.pair_hash,
            step.candidates[pair_index].second_body_id);
    }
}

[[nodiscard]] MovingScheduleRun run_moving_schedule(
    const std::uint32_t render_rate_hz)
{
    using namespace shark;

    auto clock_result = simulation::FixedStepClock::create(
        simulation::FixedStepClockConfig{
            .initially_paused = false,
        });
    REQUIRE(clock_result);
    auto clock = std::move(clock_result).value();
    MovingScheduleRun run{
        .motion = make_motion_proxies(),
    };
    auto previous_timestamp = std::chrono::nanoseconds{0};
    constexpr std::uint32_t duration_seconds = 4U;
    const auto frame_count =
        static_cast<std::uint64_t>(render_rate_hz) *
        duration_seconds;

    for (std::uint64_t frame = 1U;
         frame <= frame_count;
         ++frame) {
        const auto timestamp = std::chrono::nanoseconds{
            static_cast<std::chrono::nanoseconds::rep>(
                frame * 1'000'000'000ULL / render_rate_hz)};
        const auto frame_result =
            clock.advance(timestamp - previous_timestamp);
        REQUIRE(frame_result);
        previous_timestamp = timestamp;
        for (std::uint32_t step_index = 0;
             step_index < frame_result.value().step_count;
             ++step_index) {
            const auto fixed_tick = clock.total_step_count() -
                frame_result.value().step_count + step_index + 1U;
            advance_motion(run.motion, fixed_tick);
            const auto proxies = motion_broad_phase_proxies(
                run.motion,
                fixed_tick);
            const auto step = require_matches_oracle(proxies);
            accumulate_step(run, step, fixed_tick);
        }
    }
    run.fixed_step_count = clock.total_step_count();
    return run;
}

} // namespace

TEST_CASE(
    "broad phase fixed capacities cover every proxy pair",
    "[physics][broad-phase][capacity]")
{
    using namespace shark::physics;

    STATIC_REQUIRE(broad_phase_proxy_capacity == 64U);
    STATIC_REQUIRE(broad_phase_candidate_capacity == 2'016U);
    STATIC_REQUIRE(
        broad_phase_candidate_capacity ==
        broad_phase_proxy_capacity *
            (broad_phase_proxy_capacity - 1U) / 2U);
    STATIC_REQUIRE(
        std::tuple_size_v<decltype(BroadPhaseStep{}.candidates)> ==
        broad_phase_candidate_capacity);

    const auto empty = require_matches_oracle({});
    REQUIRE(empty.proxy_count == 0U);
    REQUIRE(empty.possible_pair_count == 0U);
    REQUIRE(empty.x_overlap_pair_count == 0U);
    REQUIRE(empty.candidate_count == 0U);

    constexpr std::array one_proxy{
        make_proxy(
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::size_t>::max() - 1U,
            {},
            {}),
    };
    const auto one = require_matches_oracle(one_proxy);
    REQUIRE(one.proxy_count == 1U);
    REQUIRE(one.possible_pair_count == 0U);
    REQUIRE(one.x_overlap_pair_count == 0U);
    REQUIRE(one.candidate_count == 0U);
}

TEST_CASE(
    "broad phase dense and sparse capacity fixtures expose exact work",
    "[physics][broad-phase][capacity][metrics][oracle]")
{
    using namespace shark::physics;

    std::array<BroadPhaseProxy, broad_phase_proxy_capacity> proxies{};

    SECTION("all point bounds overlap")
    {
        for (std::size_t index = 0;
             index < proxies.size();
             ++index) {
            proxies[index] = make_proxy(
                proxies.size() - index,
                10'000U + (proxies.size() - index) * 7U,
                {},
                {});
        }
        const auto step = require_matches_oracle(proxies);
        REQUIRE(step.proxy_count == broad_phase_proxy_capacity);
        REQUIRE(
            step.possible_pair_count ==
            broad_phase_candidate_capacity);
        REQUIRE(
            step.x_overlap_pair_count ==
            broad_phase_candidate_capacity);
        REQUIRE(
            step.candidate_count ==
            broad_phase_candidate_capacity);
    }

    SECTION("X-separated bounds prune before pair tests")
    {
        for (std::size_t index = 0;
             index < proxies.size();
             ++index) {
            const auto minimum_x =
                static_cast<float>(index) * 3.0F;
            proxies[index] = make_proxy(
                index + 1U,
                20'000U + index,
                {minimum_x, 0.0F, 0.0F},
                {minimum_x + 1.0F, 1.0F, 1.0F});
        }
        const auto step = require_matches_oracle(proxies);
        REQUIRE(
            step.possible_pair_count ==
            broad_phase_candidate_capacity);
        REQUIRE(step.x_overlap_pair_count == 0U);
        REQUIRE(step.candidate_count == 0U);
    }

    SECTION("same-X bounds require every YZ prune")
    {
        for (std::size_t index = 0;
             index < proxies.size();
             ++index) {
            const auto minimum_y =
                static_cast<float>(index) * 3.0F;
            proxies[index] = make_proxy(
                index + 1U,
                30'000U + index,
                {0.0F, minimum_y, 0.0F},
                {1.0F, minimum_y + 1.0F, 1.0F});
        }
        const auto step = require_matches_oracle(proxies);
        REQUIRE(
            step.possible_pair_count ==
            broad_phase_candidate_capacity);
        REQUIRE(
            step.x_overlap_pair_count ==
            broad_phase_candidate_capacity);
        REQUIRE(step.candidate_count == 0U);
    }
}

TEST_CASE(
    "broad phase retains exact face edge corner and point touching",
    "[physics][broad-phase][touching][inclusive]")
{
    constexpr auto first =
        make_proxy(1U, 10U, {}, {1.0F, 1.0F, 1.0F});

    const auto require_pair = [first](
                                  const Float3 minimum,
                                  const Float3 maximum,
                                  const std::size_t expected_count) {
        const std::array proxies{
            first,
            make_proxy(2U, 20U, minimum, maximum),
        };
        const auto step = require_matches_oracle(proxies);
        REQUIRE(
            step.x_overlap_pair_count ==
            expected_count);
        REQUIRE(step.candidate_count == 1U);
    };

    SECTION("X face")
    {
        require_pair(
            {1.0F, 0.25F, 0.25F},
            {2.0F, 0.75F, 0.75F},
            1U);
    }
    SECTION("Y face")
    {
        require_pair(
            {0.25F, 1.0F, 0.25F},
            {0.75F, 2.0F, 0.75F},
            1U);
    }
    SECTION("Z face")
    {
        require_pair(
            {0.25F, 0.25F, 1.0F},
            {0.75F, 0.75F, 2.0F},
            1U);
    }
    SECTION("edge")
    {
        require_pair(
            {1.0F, 1.0F, 0.25F},
            {2.0F, 2.0F, 0.75F},
            1U);
    }
    SECTION("corner")
    {
        require_pair(
            {1.0F, 1.0F, 1.0F},
            {2.0F, 2.0F, 2.0F},
            1U);
    }
    SECTION("degenerate point")
    {
        require_pair(
            {1.0F, 1.0F, 1.0F},
            {1.0F, 1.0F, 1.0F},
            1U);
    }
}

TEST_CASE(
    "broad phase excludes one-float-step separation on every axis",
    "[physics][broad-phase][separated][nextafter]")
{
    constexpr auto first =
        make_proxy(1U, 10U, {}, {1.0F, 1.0F, 1.0F});
    const auto next = std::nextafter(
        1.0F,
        std::numeric_limits<float>::infinity());

    const auto require_no_pair = [first](
                                      const Float3 minimum,
                                      const Float3 maximum,
                                      const std::size_t expected_count) {
        const std::array proxies{
            first,
            make_proxy(2U, 20U, minimum, maximum),
        };
        const auto step = require_matches_oracle(proxies);
        REQUIRE(
            step.x_overlap_pair_count ==
            expected_count);
        REQUIRE(step.candidate_count == 0U);
    };

    SECTION("X separation")
    {
        require_no_pair(
            {next, 0.25F, 0.25F},
            {2.0F, 0.75F, 0.75F},
            0U);
    }
    SECTION("Y separation")
    {
        require_no_pair(
            {0.25F, next, 0.25F},
            {0.75F, 2.0F, 0.75F},
            1U);
    }
    SECTION("Z separation")
    {
        require_no_pair(
            {0.25F, 0.25F, next},
            {0.75F, 0.75F, 2.0F},
            1U);
    }
}

TEST_CASE(
    "broad phase ties and input permutations publish one stable order",
    "[physics][broad-phase][order][ties][determinism]")
{
    constexpr std::array<std::uint64_t, 6> input_ids{
        std::numeric_limits<std::uint64_t>::max(),
        4U,
        42U,
        1U,
        9U,
        2U,
    };
    std::array<BroadPhaseProxy, input_ids.size()> proxies{};
    for (std::size_t index = 0;
         index < proxies.size();
         ++index) {
        proxies[index] = make_proxy(
            input_ids[index],
            50'000U + index * 19U,
            {},
            {1.0F, 1.0F, 1.0F});
    }

    const auto baseline = require_matches_oracle(proxies);
    REQUIRE(baseline.candidate_count == 15U);
    std::reverse(proxies.begin(), proxies.end());
    const auto reversed = require_matches_oracle(proxies);
    REQUIRE(reversed == baseline);
    std::rotate(proxies.begin(), proxies.begin() + 2, proxies.end());
    const auto rotated = require_matches_oracle(proxies);
    REQUIRE(rotated == baseline);
}

TEST_CASE(
    "broad phase fixed-X ties still apply full three-axis overlap",
    "[physics][broad-phase][ties][axis][oracle]")
{
    constexpr std::array proxies{
        make_proxy(50U, 500U, {0.0F, 0.0F, 2.25F},
            {1.0F, 1.0F, 3.0F}),
        make_proxy(10U, 100U, {0.0F, 0.0F, 0.0F},
            {1.0F, 1.0F, 1.0F}),
        make_proxy(40U, 400U, {0.0F, 0.0F, 1.0F},
            {1.0F, 1.0F, 2.0F}),
        make_proxy(20U, 200U, {0.0F, 1.0F, 0.0F},
            {1.0F, 2.0F, 1.0F}),
        make_proxy(30U, 300U, {0.0F, 2.25F, 0.0F},
            {1.0F, 3.0F, 1.0F}),
    };
    const auto step = require_matches_oracle(proxies);
    REQUIRE(step.possible_pair_count == 10U);
    REQUIRE(step.x_overlap_pair_count == 10U);
    REQUIRE(step.candidate_count == 3U);
    REQUIRE(step.candidates[0].first_body_id == 10U);
    REQUIRE(step.candidates[0].second_body_id == 20U);
    REQUIRE(step.candidates[1].first_body_id == 10U);
    REQUIRE(step.candidates[1].second_body_id == 40U);
    REQUIRE(step.candidates[2].first_body_id == 20U);
    REQUIRE(step.candidates[2].second_body_id == 40U);
}

TEST_CASE(
    "broad phase seeded fixtures exactly match brute force",
    "[physics][broad-phase][seeded][oracle][determinism]")
{
    constexpr std::array<std::uint32_t, 4> seeds{
        1U,
        0x4FFB'0830U,
        0xC0FF'EE11U,
        0xFFFF'FFFFU,
    };
    constexpr std::array<std::size_t, 6> proxy_counts{
        2U,
        3U,
        7U,
        16U,
        31U,
        shark::physics::broad_phase_proxy_capacity,
    };

    for (const auto seed : seeds) {
        for (const auto proxy_count : proxy_counts) {
            CAPTURE(seed, proxy_count);
            const auto proxies =
                make_seeded_proxies(seed, proxy_count);
            const auto active = std::span<const BroadPhaseProxy>{
                proxies.data(),
                proxy_count,
            };
            const auto baseline = require_matches_oracle(active);
            const auto repeated = require_matches_oracle(active);
            REQUIRE(repeated == baseline);
        }
    }
}

TEST_CASE(
    "moving broad-phase proxies match brute force and change generation",
    "[physics][broad-phase][moving][generation][oracle]")
{
    constexpr auto old_id = (std::uint64_t{1} << 32U) | 8U;
    constexpr auto new_id = (std::uint64_t{2} << 32U) | 8U;
    auto motion = make_motion_proxies();
    bool saw_old_identity = false;
    bool saw_new_identity = false;

    for (std::uint64_t tick = 1U; tick <= 240U; ++tick) {
        advance_motion(motion, tick);
        const auto proxies = motion_broad_phase_proxies(motion, tick);
        const auto step = require_matches_oracle(proxies);
        for (std::size_t pair_index = 0;
             pair_index < step.candidate_count;
             ++pair_index) {
            const auto& pair = step.candidates[pair_index];
            const auto contains_old =
                pair.first_body_id == old_id ||
                pair.second_body_id == old_id;
            const auto contains_new =
                pair.first_body_id == new_id ||
                pair.second_body_id == new_id;
            if (tick == 79U && contains_old) {
                saw_old_identity = true;
            }
            if (tick >= 80U) {
                REQUIRE_FALSE(contains_old);
                saw_new_identity = saw_new_identity || contains_new;
            }
        }
    }

    REQUIRE(saw_old_identity);
    REQUIRE(saw_new_identity);
}

TEST_CASE(
    "moving broad phase is invariant across render partitions",
    "[physics][broad-phase][moving][fixed-step][invariance]")
{
    constexpr std::array<std::uint32_t, 4> render_rates{
        30U,
        60U,
        120U,
        144U,
    };
    const auto baseline = run_moving_schedule(render_rates[0]);
    REQUIRE(baseline.fixed_step_count == 240U);
    REQUIRE(
        baseline.proxy_count ==
        moving_proxy_count * baseline.fixed_step_count);
    REQUIRE(
        baseline.possible_pair_count ==
        moving_proxy_count * (moving_proxy_count - 1U) / 2U *
            baseline.fixed_step_count);

    for (const auto render_rate : render_rates) {
        CAPTURE(render_rate);
        REQUIRE(run_moving_schedule(render_rate) == baseline);
    }
}

TEST_CASE(
    "broad phase rejects invalid proxies without persistent state",
    "[physics][broad-phase][validation][transaction]")
{
    using namespace shark::physics;

    constexpr auto valid_first =
        make_proxy(1U, 10U, {}, {1.0F, 1.0F, 1.0F});
    constexpr auto valid_second =
        make_proxy(2U, 20U, {}, {1.0F, 1.0F, 1.0F});

    SECTION("body ID is zero")
    {
        const std::array proxies{
            make_proxy(0U, 10U, {}, {1.0F, 1.0F, 1.0F}),
        };
        require_invalid_transaction(proxies);
    }
    SECTION("body IDs are duplicated even when bounds are disjoint")
    {
        const std::array proxies{
            valid_first,
            make_proxy(1U, 20U, {10.0F, 10.0F, 10.0F},
                {11.0F, 11.0F, 11.0F}),
        };
        require_invalid_transaction(proxies);
    }
    SECTION("body indices are duplicated")
    {
        const std::array proxies{
            valid_first,
            make_proxy(2U, 10U, {10.0F, 10.0F, 10.0F},
                {11.0F, 11.0F, 11.0F}),
        };
        require_invalid_transaction(proxies);
    }
    SECTION("static contact sentinel is not a dynamic body index")
    {
        const std::array proxies{
            make_proxy(
                1U,
                std::numeric_limits<std::size_t>::max(),
                {},
                {1.0F, 1.0F, 1.0F}),
        };
        require_invalid_transaction(proxies);
    }
    SECTION("minimum exceeds maximum on one axis")
    {
        for (std::size_t axis = 0; axis < 3U; ++axis) {
            CAPTURE(axis);
            auto invalid = valid_second;
            if (axis == 0U) {
                invalid.bounds.minimum.x = 2.0F;
            }
            else if (axis == 1U) {
                invalid.bounds.minimum.y = 2.0F;
            }
            else {
                invalid.bounds.minimum.z = 2.0F;
            }
            const std::array proxies{valid_first, invalid};
            require_invalid_transaction(proxies);
        }
    }
    SECTION("nonfinite bound components are rejected")
    {
        constexpr std::array invalid_values{
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(),
        };
        for (const auto value : invalid_values) {
            for (std::size_t component = 0;
                 component < 6U;
                 ++component) {
                CAPTURE(value, component);
                auto invalid = valid_second;
                float* target = nullptr;
                switch (component) {
                case 0U:
                    target = &invalid.bounds.minimum.x;
                    break;
                case 1U:
                    target = &invalid.bounds.minimum.y;
                    break;
                case 2U:
                    target = &invalid.bounds.minimum.z;
                    break;
                case 3U:
                    target = &invalid.bounds.maximum.x;
                    break;
                case 4U:
                    target = &invalid.bounds.maximum.y;
                    break;
                default:
                    target = &invalid.bounds.maximum.z;
                    break;
                }
                *target = value;
                const std::array proxies{valid_first, invalid};
                require_invalid_transaction(proxies);
            }
        }
    }
    SECTION("proxy count exceeds fixed capacity")
    {
        std::array<
            BroadPhaseProxy,
            broad_phase_proxy_capacity + 1U>
            proxies{};
        for (std::size_t index = 0;
             index < proxies.size();
             ++index) {
            proxies[index] = make_proxy(
                index + 1U,
                index,
                {},
                {});
        }
        require_invalid_transaction(proxies);
    }
}

TEST_CASE(
    "broad phase accepts finite extremes and ignores inactive storage",
    "[physics][broad-phase][bounds][capacity]")
{
    constexpr auto maximum = std::numeric_limits<float>::max();
    constexpr std::array extreme_proxies{
        make_proxy(
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::size_t>::max() - 1U,
            {-maximum, -maximum, -maximum},
            {maximum, maximum, maximum}),
        make_proxy(1U, 0U, {}, {}),
    };
    const auto step = require_matches_oracle(extreme_proxies);
    REQUIRE(step.x_overlap_pair_count == 1U);
    REQUIRE(step.candidate_count == 1U);

    auto storage = extreme_proxies;
    storage[1].body_id = 0U;
    storage[1].bounds.minimum.x =
        std::numeric_limits<float>::quiet_NaN();
    const auto active = std::span<const BroadPhaseProxy>{
        storage.data(),
        1U,
    };
    const auto active_step = require_matches_oracle(active);
    REQUIRE(active_step.proxy_count == 1U);
    REQUIRE(active_step.candidate_count == 0U);
}
