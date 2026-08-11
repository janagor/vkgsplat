#ifndef VKGSPLAT_INPUT_HANDLER_KEY_BINDINGS_HPP
#define VKGSPLAT_INPUT_HANDLER_KEY_BINDINGS_HPP

#include <expected>
#include <string>
#include <string_view>

#include <GLFW/glfw3.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

// GLFW key codes for every discrete action the viewer binds.
struct KeyBindings
{
  int forward = GLFW_KEY_W;
  int backward = GLFW_KEY_S;
  int left = GLFW_KEY_A;
  int right = GLFW_KEY_D;
  int up = GLFW_KEY_SPACE;
  int down = GLFW_KEY_LEFT_CONTROL;
  int roll_left = GLFW_KEY_Q;
  int roll_right = GLFW_KEY_E;
  int close = GLFW_KEY_ESCAPE;
  int screenshot = GLFW_KEY_F2;
  int emulate_toggle = GLFW_KEY_M;
  int cell_left = GLFW_KEY_LEFT;
  int cell_right = GLFW_KEY_RIGHT;
  int cell_up = GLFW_KEY_UP;
  int cell_down = GLFW_KEY_DOWN;
};

// Human-readable names used in config / CLI (defaults match KeyBindings{}).
struct KeyBindingNames
{
  std::string forward{ "W" };
  std::string backward{ "S" };
  std::string left{ "A" };
  std::string right{ "D" };
  std::string up{ "SPACE" };
  std::string down{ "LEFT_CONTROL" };
  std::string roll_left{ "Q" };
  std::string roll_right{ "E" };
  std::string close{ "ESCAPE" };
  std::string screenshot{ "F2" };
  std::string emulate_toggle{ "M" };
  std::string cell_left{ "LEFT" };
  std::string cell_right{ "RIGHT" };
  std::string cell_up{ "UP" };
  std::string cell_down{ "DOWN" };
};

[[nodiscard]] auto ParseGlfwKeyName(std::string_view name) -> std::expected<int, Error>;

[[nodiscard]] auto ResolveKeyBindings(KeyBindingNames const &names) -> std::expected<KeyBindings, Error>;

}// namespace vkgsplat

#endif// VKGSPLAT_INPUT_HANDLER_KEY_BINDINGS_HPP
