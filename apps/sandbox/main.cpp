#include <shark/core/engine_info.hpp>

#include <cstdlib>

int main()
{
    return shark::core::engine_name() == "Shark" ? EXIT_SUCCESS : EXIT_FAILURE;
}

