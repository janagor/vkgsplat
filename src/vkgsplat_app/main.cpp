#include "application.hpp"

#include <cstddef>
#include <span>

int main(int argc, char **argv)
{
  return vkgsplat::app::run(std::span{ argv, static_cast<size_t>(argc) });
}
