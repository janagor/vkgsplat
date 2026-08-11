#include "ply/miniply.hpp"

#include <cstdlib>
#include <print>
#include <span>
#include <string>
#include <string_view>

#ifndef VKGSPLAT_SOURCE_DIR
#define VKGSPLAT_SOURCE_DIR "."
#endif

namespace {

auto PropertyTypeName(vkgsplat::miniply::PLYPropertyType type) -> std::string_view
{
  switch (type) {
  case vkgsplat::miniply::PLYPropertyType::kChar:
    return std::string_view{ "char" };
  case vkgsplat::miniply::PLYPropertyType::kUChar:
    return std::string_view{ "uchar" };
  case vkgsplat::miniply::PLYPropertyType::kShort:
    return std::string_view{ "short" };
  case vkgsplat::miniply::PLYPropertyType::kUShort:
    return std::string_view{ "ushort" };
  case vkgsplat::miniply::PLYPropertyType::kInt:
    return std::string_view{ "int" };
  case vkgsplat::miniply::PLYPropertyType::kUInt:
    return std::string_view{ "uint" };
  case vkgsplat::miniply::PLYPropertyType::kFloat:
    return std::string_view{ "float" };
  case vkgsplat::miniply::PLYPropertyType::kDouble:
    return std::string_view{ "double" };
  case vkgsplat::miniply::PLYPropertyType::kNone:
    return std::string_view{ "none" };
  }
  return std::string_view{ "unknown" };
}

void PrintProperty(vkgsplat::miniply::PLYProperty const &prop)
{
  if (prop.count_type != vkgsplat::miniply::PLYPropertyType::kNone) {
    std::println(
      "    property '{}' list<{}> {}", prop.name, PropertyTypeName(prop.count_type), PropertyTypeName(prop.type));
    return;
  }

  std::println("    property '{}' {}", prop.name, PropertyTypeName(prop.type));
}

}// namespace

auto main(int argc, char *argv[]) -> int
{
  try {
    auto const args = std::span{ argv, static_cast<size_t>(argc) };
    std::string const path = (args.size() > 1) ? std::string{ std::string_view{ args.subspan(1).front() } }
                                               : std::string{ VKGSPLAT_SOURCE_DIR } + "/resources/scene.ply";

    vkgsplat::miniply::PLYReader reader(path.c_str());
    if (!reader.valid()) {
      std::println(stderr, "Failed to open or parse PLY header: {}", path);
      return EXIT_FAILURE;
    }

    std::println("PLY file: {}", path);
    std::println("Elements: {}", reader.num_elements());

    while (reader.has_element()) {
      auto const *elem = reader.element();
      std::println("  element '{}' rows={}", elem->name, elem->count);
      std::println("  properties: {}", elem->properties.size());

      for (auto const &prop : elem->properties) { PrintProperty(prop); }

      reader.next_element();
    }

    return EXIT_SUCCESS;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
