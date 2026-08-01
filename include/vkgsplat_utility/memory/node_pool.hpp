#pragma once

#include <concepts>
#include <memory>
#include <utility>

namespace vkgsplat {

template<typename ValueTp, typename AllocatorTp = std::allocator<ValueTp>>
class NodePool
{
public:
  struct Node
  {
    template<typename... Args>
      requires std::constructible_from<ValueTp, Args...>
    explicit constexpr Node(Args &&...args) : value(std::forward<Args>(args)...)
    {}

    ValueTp value;
    Node *next = nullptr;
  };

private:
  using ValueTraits = std::allocator_traits<AllocatorTp>;
  using NodeTraits = typename ValueTraits::template rebind_traits<Node>;
  using NodeAllocatorTp = typename ValueTraits::template rebind_alloc<Node>;

public:
  using value_type = ValueTp;
  using allocator_type = AllocatorTp;
  using node_type = Node;

  constexpr explicit NodePool(AllocatorTp const &allocator = {}) : allocator_(allocator) {}

  constexpr NodePool(NodePool &&other) noexcept
    : allocator_(std::move(other.allocator_)), handle_(std::exchange(other.handle_, nullptr))
  {}

  NodePool(NodePool const &) = delete;

  constexpr auto operator=(NodePool &&other) noexcept -> NodePool &
  {
    if (this != &other) {
      destroy();
      allocator_ = std::move(other.allocator_);
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  auto operator=(NodePool const &) -> NodePool & = delete;

  constexpr ~NodePool() { destroy(); }

  constexpr void return_node(Node *handle)
  {
    handle->next = handle_;
    handle_ = handle;
    std::destroy_at(&handle_->value);
  }

  template<typename... Args>
    requires std::constructible_from<ValueTp, Args...>
  constexpr auto get_node(Args &&...args) -> Node *
  {
    if (handle_ == nullptr) {
      Node *node = NodeTraits::allocate(allocator_, 1);
      NodeTraits::construct(allocator_, node, std::forward<Args>(args)...);
      return node;
    }

    Node *node = handle_;
    handle_ = node->next;
    NodeTraits::construct(allocator_, node, std::forward<Args>(args)...);
    return node;
  }

  [[nodiscard]] constexpr auto get_allocator() const -> AllocatorTp { return allocator_; }

  [[nodiscard]] constexpr auto get_node_allocator() noexcept -> NodeAllocatorTp & { return allocator_; }

private:
  constexpr void destroy()
  {
    for (Node *node = handle_; node != nullptr;) {
      Node *const old_node = node;
      node = node->next;
      NodeTraits::deallocate(allocator_, old_node, 1);
    }
    handle_ = nullptr;
  }

  [[no_unique_address]] NodeAllocatorTp allocator_{};
  Node *handle_ = nullptr;
};

}// namespace vkgsplat
