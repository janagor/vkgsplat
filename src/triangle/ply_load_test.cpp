#include "io/ply/miniply.hpp"

#include <cstdlib>
#include <print>
#include <span>
#include <string>
#include <string_view>

#ifndef VKGSPLAT_SOURCE_DIR
#define VKGSPLAT_SOURCE_DIR "."
#endif

namespace {

auto property_type_name(miniply::PLYPropertyType type) -> std::string_view
{
  switch (type) {
  case miniply::PLYPropertyType::Char:
    return std::string_view{ "char" };
  case miniply::PLYPropertyType::UChar:
    return std::string_view{ "uchar" };
  case miniply::PLYPropertyType::Short:
    return std::string_view{ "short" };
  case miniply::PLYPropertyType::UShort:
    return std::string_view{ "ushort" };
  case miniply::PLYPropertyType::Int:
    return std::string_view{ "int" };
  case miniply::PLYPropertyType::UInt:
    return std::string_view{ "uint" };
  case miniply::PLYPropertyType::Float:
    return std::string_view{ "float" };
  case miniply::PLYPropertyType::Double:
    return std::string_view{ "double" };
  case miniply::PLYPropertyType::None:
    return std::string_view{ "none" };
  }
  return std::string_view{ "unknown" };
}

void print_property(miniply::PLYProperty const &prop)
{
  if (prop.countType != miniply::PLYPropertyType::None) {
    std::println(
      "    property '{}' list<{}> {}", prop.name, property_type_name(prop.countType), property_type_name(prop.type));
    return;
  }

  std::println("    property '{}' {}", prop.name, property_type_name(prop.type));
}

}// namespace

auto main(int argc, char *argv[]) -> int
{
  try {
    auto const args = std::span{ argv, static_cast<size_t>(argc) };
    std::string const path = (args.size() > 1) ? std::string{ std::string_view{ args.subspan(1).front() } }
                                               : std::string{ VKGSPLAT_SOURCE_DIR } + "/resources/scene.ply";

    miniply::PLYReader reader(path.c_str());
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

      for (auto const &prop : elem->properties) { print_property(prop); }

      reader.next_element();
    }

    return EXIT_SUCCESS;
  } catch (...) {
    return EXIT_FAILURE;
  }
}
