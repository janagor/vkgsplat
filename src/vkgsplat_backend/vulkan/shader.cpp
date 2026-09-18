#include "shader.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace vkgsplat {

auto ReadFile(const std::string &filename) -> std::vector<char>
{
  std::ifstream file(filename, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    std::cout << "failed to open file!\n";
    return {};
  }

  auto const file_size = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(file_size);

  file.seekg(0);
  file.read(buffer.data(), static_cast<std::streamsize>(file_size));

  file.close();

  return buffer;
}

}// namespace vkgsplat
