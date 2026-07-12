#include <shark/core/engine_info.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the engine exposes its build identity", "[core]")
{
    REQUIRE(shark::core::engine_name() == "Shark");
}

