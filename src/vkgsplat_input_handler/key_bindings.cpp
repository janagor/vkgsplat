#include <vkgsplat_input_handler/key_bindings.hpp>

#include <array>
#include <cctype>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

#include <GLFW/glfw3.h>

#include <vkgsplat_utility/error.hpp>

namespace vkgsplat {

namespace {

  [[nodiscard]] auto NormalizeKeyName(std::string_view name) -> std::string
  {
    std::string out;
    out.reserve(name.size());
    for (char const raw : name) {
      auto const byte = static_cast<unsigned char>(raw);
      if (byte == '-' || byte == ' ') {
        out.push_back('_');
        continue;
      }
      out.push_back(static_cast<char>(std::toupper(byte)));
    }
    constexpr std::string_view kPrefix = "GLFW_KEY_";
    if (out.starts_with(kPrefix)) { out.erase(0, kPrefix.size()); }
    return out;
  }

  [[nodiscard]] auto NamedKeyMap() -> std::unordered_map<std::string_view, int> const &
  {
    static std::unordered_map<std::string_view, int> const kMap{
      { "SPACE", GLFW_KEY_SPACE },
      { "APOSTROPHE", GLFW_KEY_APOSTROPHE },
      { "COMMA", GLFW_KEY_COMMA },
      { "MINUS", GLFW_KEY_MINUS },
      { "PERIOD", GLFW_KEY_PERIOD },
      { "SLASH", GLFW_KEY_SLASH },
      { "SEMICOLON", GLFW_KEY_SEMICOLON },
      { "EQUAL", GLFW_KEY_EQUAL },
      { "LEFT_BRACKET", GLFW_KEY_LEFT_BRACKET },
      { "BACKSLASH", GLFW_KEY_BACKSLASH },
      { "RIGHT_BRACKET", GLFW_KEY_RIGHT_BRACKET },
      { "GRAVE_ACCENT", GLFW_KEY_GRAVE_ACCENT },
      { "ESCAPE", GLFW_KEY_ESCAPE },
      { "ESC", GLFW_KEY_ESCAPE },
      { "ENTER", GLFW_KEY_ENTER },
      { "TAB", GLFW_KEY_TAB },
      { "BACKSPACE", GLFW_KEY_BACKSPACE },
      { "INSERT", GLFW_KEY_INSERT },
      { "DELETE", GLFW_KEY_DELETE },
      { "RIGHT", GLFW_KEY_RIGHT },
      { "LEFT", GLFW_KEY_LEFT },
      { "DOWN", GLFW_KEY_DOWN },
      { "UP", GLFW_KEY_UP },
      { "PAGE_UP", GLFW_KEY_PAGE_UP },
      { "PAGE_DOWN", GLFW_KEY_PAGE_DOWN },
      { "HOME", GLFW_KEY_HOME },
      { "END", GLFW_KEY_END },
      { "CAPS_LOCK", GLFW_KEY_CAPS_LOCK },
      { "SCROLL_LOCK", GLFW_KEY_SCROLL_LOCK },
      { "NUM_LOCK", GLFW_KEY_NUM_LOCK },
      { "PRINT_SCREEN", GLFW_KEY_PRINT_SCREEN },
      { "PAUSE", GLFW_KEY_PAUSE },
      { "F1", GLFW_KEY_F1 },
      { "F2", GLFW_KEY_F2 },
      { "F3", GLFW_KEY_F3 },
      { "F4", GLFW_KEY_F4 },
      { "F5", GLFW_KEY_F5 },
      { "F6", GLFW_KEY_F6 },
      { "F7", GLFW_KEY_F7 },
      { "F8", GLFW_KEY_F8 },
      { "F9", GLFW_KEY_F9 },
      { "F10", GLFW_KEY_F10 },
      { "F11", GLFW_KEY_F11 },
      { "F12", GLFW_KEY_F12 },
      { "KP_0", GLFW_KEY_KP_0 },
      { "KP_1", GLFW_KEY_KP_1 },
      { "KP_2", GLFW_KEY_KP_2 },
      { "KP_3", GLFW_KEY_KP_3 },
      { "KP_4", GLFW_KEY_KP_4 },
      { "KP_5", GLFW_KEY_KP_5 },
      { "KP_6", GLFW_KEY_KP_6 },
      { "KP_7", GLFW_KEY_KP_7 },
      { "KP_8", GLFW_KEY_KP_8 },
      { "KP_9", GLFW_KEY_KP_9 },
      { "KP_DECIMAL", GLFW_KEY_KP_DECIMAL },
      { "KP_DIVIDE", GLFW_KEY_KP_DIVIDE },
      { "KP_MULTIPLY", GLFW_KEY_KP_MULTIPLY },
      { "KP_SUBTRACT", GLFW_KEY_KP_SUBTRACT },
      { "KP_ADD", GLFW_KEY_KP_ADD },
      { "KP_ENTER", GLFW_KEY_KP_ENTER },
      { "KP_EQUAL", GLFW_KEY_KP_EQUAL },
      { "LEFT_SHIFT", GLFW_KEY_LEFT_SHIFT },
      { "LEFT_CONTROL", GLFW_KEY_LEFT_CONTROL },
      { "LCTRL", GLFW_KEY_LEFT_CONTROL },
      { "LEFT_CTRL", GLFW_KEY_LEFT_CONTROL },
      { "LEFT_ALT", GLFW_KEY_LEFT_ALT },
      { "LEFT_SUPER", GLFW_KEY_LEFT_SUPER },
      { "RIGHT_SHIFT", GLFW_KEY_RIGHT_SHIFT },
      { "RIGHT_CONTROL", GLFW_KEY_RIGHT_CONTROL },
      { "RCTRL", GLFW_KEY_RIGHT_CONTROL },
      { "RIGHT_CTRL", GLFW_KEY_RIGHT_CONTROL },
      { "RIGHT_ALT", GLFW_KEY_RIGHT_ALT },
      { "RIGHT_SUPER", GLFW_KEY_RIGHT_SUPER },
      { "MENU", GLFW_KEY_MENU },
    };
    return kMap;
  }

}// namespace

auto ParseGlfwKeyName(std::string_view name) -> std::expected<int, Error>
{
  auto const normalized = NormalizeKeyName(name);
  if (normalized.empty()) {
    return std::unexpected{ MakeError(std::errc::invalid_argument, "empty keybind name") };
  }

  if (normalized.size() == 1U) {
    auto const code = static_cast<unsigned char>(normalized.front());
    if (code >= 'A' && code <= 'Z') { return GLFW_KEY_A + (code - 'A'); }
    if (code >= '0' && code <= '9') { return GLFW_KEY_0 + (code - '0'); }
  }

  if (auto const found = NamedKeyMap().find(normalized); found != NamedKeyMap().end()) { return found->second; }

  return std::unexpected{ MakeError(std::errc::invalid_argument,
    "unknown keybind name '" + std::string{ name }
      + "' (use A-Z, 0-9, F1-F12, SPACE, ESCAPE, LEFT/RIGHT/UP/DOWN, LEFT_CONTROL, ...)") };
}

auto ResolveKeyBindings(KeyBindingNames const &names) -> std::expected<KeyBindings, Error>
{
  KeyBindings bindings{};

  auto const fields = std::to_array<std::pair<std::string const *, int *>>({
    { &names.forward, &bindings.forward },
    { &names.backward, &bindings.backward },
    { &names.left, &bindings.left },
    { &names.right, &bindings.right },
    { &names.up, &bindings.up },
    { &names.down, &bindings.down },
    { &names.roll_left, &bindings.roll_left },
    { &names.roll_right, &bindings.roll_right },
    { &names.close, &bindings.close },
    { &names.screenshot, &bindings.screenshot },
    { &names.emulate_toggle, &bindings.emulate_toggle },
    { &names.cell_left, &bindings.cell_left },
    { &names.cell_right, &bindings.cell_right },
    { &names.cell_up, &bindings.cell_up },
    { &names.cell_down, &bindings.cell_down },
  });

  for (auto const &[name_ptr, binding_ptr] : fields) {
    auto parsed = ParseGlfwKeyName(*name_ptr);
    if (!parsed) { return std::unexpected{ parsed.error() }; }
    *binding_ptr = *parsed;
  }

  return bindings;
}

}// namespace vkgsplat
