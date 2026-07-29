#include "application.hpp"

#include <cstddef>
#include <span>

auto main(int argc, char **argv) -> int { return vkgsplat::app::run(std::span{ argv, static_cast<size_t>(argc) }); }
