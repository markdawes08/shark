#include "camera_controller.hpp"

#include <shark/core/math.hpp>
#include <shark/platform/events.hpp>
#include <shark/world/camera.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

#include <cmath>
#include <cstdint>

namespace {

[[nodiscard]] shark::platform::KeyEvent key_event(
    const std::uint32_t virtual_key,
    const shark::platform::KeyAction action,
    const bool repeated = false) noexcept
{
    return shark::platform::KeyEvent{
        .virtual_key = virtual_key,
        .repeat_count = 1,
        .scan_code = 0,
        .action = action,
        .extended = false,
        .repeated = repeated,
        .system_key = false,
    };
}

void send_key(
    shark::sandbox::CameraController& controller,
    const std::uint32_t virtual_key,
    const shark::platform::KeyAction action,
    const bool repeated = false) noexcept
{
    controller.handle_event(
        key_event(virtual_key, action, repeated));
}

[[nodiscard]] float displacement_length(
    const shark::world::Camera& camera) noexcept
{
    const auto x = camera.transform.position.x;
    const auto y = camera.transform.position.y;
    const auto z = camera.transform.position.z - 4.0F;
    return std::sqrt(x * x + y * y + z * z);
}

} // namespace

TEST_CASE(
    "camera controller maps movement keys and normalizes diagonals",
    "[sandbox][camera-controller]")
{
    using namespace shark;

    const sandbox::CameraControllerConfig config{
        .movement_speed = 4.0F,
        .sprint_multiplier = 3.0F,
        .mouse_sensitivity = 0.01F,
        .maximum_delta_seconds = 1.0F,
    };

    SECTION("W moves forward and release stops movement")
    {
        sandbox::CameraController controller{config};
        world::Camera camera;
        send_key(controller, 'W', platform::KeyAction::pressed);
        send_key(
            controller,
            'W',
            platform::KeyAction::pressed,
            true);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(0.0F));

        send_key(controller, 'W', platform::KeyAction::released);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(0.0F));
    }

    SECTION("W and D preserve the configured speed")
    {
        sandbox::CameraController controller{config};
        world::Camera camera;
        send_key(controller, 'W', platform::KeyAction::pressed);
        send_key(controller, 'D', platform::KeyAction::pressed);
        controller.update(camera, 1.0F);
        REQUIRE(displacement_length(camera) ==
            Catch::Approx(4.0F).margin(0.00001F));
        REQUIRE(camera.transform.position.x > 0.0F);
        REQUIRE(camera.transform.position.z < 4.0F);
    }

    SECTION("Q E Space Control and Shift map to vertical and sprint")
    {
        sandbox::CameraController controller{config};
        world::Camera camera;
        send_key(controller, 'E', platform::KeyAction::pressed);
        send_key(controller, VK_SHIFT, platform::KeyAction::pressed);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.y ==
            Catch::Approx(12.0F));

        send_key(controller, 'E', platform::KeyAction::released);
        send_key(controller, VK_SHIFT, platform::KeyAction::released);
        send_key(controller, VK_CONTROL, platform::KeyAction::pressed);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.y ==
            Catch::Approx(8.0F));

        send_key(
            controller,
            VK_CONTROL,
            platform::KeyAction::released);
        send_key(controller, VK_SPACE, platform::KeyAction::pressed);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.y ==
            Catch::Approx(12.0F));

        send_key(controller, VK_SPACE, platform::KeyAction::released);
        send_key(controller, 'Q', platform::KeyAction::pressed);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.y ==
            Catch::Approx(8.0F));
    }
}

TEST_CASE(
    "camera controller clamps frame delta before moving",
    "[sandbox][camera-controller]")
{
    using namespace shark;

    sandbox::CameraController controller;
    world::Camera camera;
    send_key(controller, 'W', platform::KeyAction::pressed);
    controller.update(camera, 100.0F);
    REQUIRE(camera.transform.position.z ==
        Catch::Approx(3.6F).margin(0.00001F));
}

TEST_CASE(
    "right mouse drag produces deterministic yaw and pitch",
    "[sandbox][camera-controller][mouse]")
{
    using namespace shark;

    sandbox::CameraController controller{
        sandbox::CameraControllerConfig{
            .mouse_sensitivity = 0.01F,
        }};
    world::Camera camera;

    controller.handle_event(platform::MouseMovedEvent{500, 500});
    controller.update(camera, 0.0F);
    REQUIRE(camera.transform.yaw_radians == 0.0F);
    REQUIRE(camera.transform.pitch_radians == 0.0F);

    controller.handle_event(platform::MouseButtonEvent{
        .x = 100,
        .y = 100,
        .button = platform::MouseButton::right,
        .action = platform::ButtonAction::pressed,
    });
    controller.update(camera, 0.0F);
    REQUIRE(camera.transform.yaw_radians == 0.0F);
    REQUIRE(camera.transform.pitch_radians == 0.0F);

    controller.handle_event(platform::MouseMovedEvent{110, 90});
    controller.handle_event(platform::MouseButtonEvent{
        .x = 110,
        .y = 90,
        .button = platform::MouseButton::right,
        .action = platform::ButtonAction::released,
    });
    controller.update(camera, 0.0F);
    REQUIRE(camera.transform.yaw_radians ==
        Catch::Approx(0.1F));
    REQUIRE(camera.transform.pitch_radians ==
        Catch::Approx(0.1F));

    controller.handle_event(platform::MouseMovedEvent{210, 190});
    controller.update(camera, 0.0F);
    REQUIRE(camera.transform.yaw_radians ==
        Catch::Approx(0.1F));
    REQUIRE(camera.transform.pitch_radians ==
        Catch::Approx(0.1F));
}

TEST_CASE(
    "focus minimize and close transitions clear controller state",
    "[sandbox][camera-controller][lifecycle]")
{
    using namespace shark;

    const sandbox::CameraControllerConfig config{
        .movement_speed = 4.0F,
        .maximum_delta_seconds = 1.0F,
    };

    SECTION("focus loss")
    {
        sandbox::CameraController controller{config};
        world::Camera camera;
        send_key(controller, 'W', platform::KeyAction::pressed);
        controller.set_focused(false);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(4.0F));

        controller.set_focused(true);
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(4.0F));
    }

    SECTION("minimize and restore")
    {
        sandbox::CameraController controller{config};
        world::Camera camera;
        send_key(controller, 'W', platform::KeyAction::pressed);
        controller.handle_event(platform::WindowMinimizedEvent{});
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(4.0F));

        controller.handle_event(platform::WindowRestoredEvent{
            platform::WindowExtent{800, 600},
        });
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(4.0F));
    }

    SECTION("close request")
    {
        sandbox::CameraController controller{config};
        world::Camera camera;
        send_key(controller, 'W', platform::KeyAction::pressed);
        controller.handle_event(
            platform::WindowCloseRequestedEvent{});
        controller.update(camera, 1.0F);
        REQUIRE(camera.transform.position.z ==
            Catch::Approx(4.0F));
    }
}
