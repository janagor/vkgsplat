#pragma once

#include "compute/operation.hpp"

#include <vkgsplat_utility/memory/node_pool.hpp>

#include <algorithm>
#include <concepts>
#include <memory>
#include <ranges>
#include <utility>
#include <vector>

namespace vkgsplat {

struct Init;
struct RenderData;

namespace compute {

  template<typename AllocatorTp = std::allocator<std::unique_ptr<Operation>>> class BasicSequence
  {
  public:
    using allocator_type = AllocatorTp;
    using OpPtr = std::unique_ptr<Operation>;
    using Pool = NodePool<OpPtr, AllocatorTp>;
    using Node = typename Pool::node_type;

    constexpr explicit BasicSequence(AllocatorTp const &allocator = {}) : pool_(allocator) {}

    BasicSequence(BasicSequence &&other) noexcept
      : pool_(std::move(other.pool_)), head_(std::exchange(other.head_, nullptr)),
        tail_(std::exchange(other.tail_, nullptr)), size_(std::exchange(other.size_, 0))
    {}

    auto operator=(BasicSequence &&other) noexcept -> BasicSequence &
    {
      if (this != &other) {
        clear();
        pool_ = std::move(other.pool_);
        head_ = std::exchange(other.head_, nullptr);
        tail_ = std::exchange(other.tail_, nullptr);
        size_ = std::exchange(other.size_, 0);
      }
      return *this;
    }

    BasicSequence(BasicSequence const &) = delete;
    auto operator=(BasicSequence const &) -> BasicSequence & = delete;

    ~BasicSequence() { clear(); }

    auto record(OpPtr op) -> BasicSequence &
    {
      Node *const node = pool_.get_node(std::move(op));
      node->next = nullptr;
      if (tail_ == nullptr) {
        head_ = node;
        tail_ = node;
      } else {
        tail_->next = node;
        tail_ = node;
      }
      ++size_;
      return *this;
    }

    template<typename OpTp, typename... Args>
      requires std::derived_from<OpTp, Operation> && std::constructible_from<OpTp, Args...>
    auto emplace(Args &&...args) -> BasicSequence &
    { return record(std::make_unique<OpTp>(std::forward<Args>(args)...)); }

    void eval(Init &init, RenderData const &data, VkCommandBuffer cmd) const
    {
      auto const nodes = as_nodes();
      std::ranges::for_each(nodes, [&](Node const *node) { node->value->pre_eval(init, data, cmd); });
      std::ranges::for_each(nodes, [&](Node const *node) { node->value->record(init, data, cmd); });
      std::ranges::for_each(nodes, [&](Node const *node) { node->value->post_eval(init, data, cmd); });
    }

    void clear() noexcept
    {
      for (Node *node = head_; node != nullptr;) {
        Node *const next = node->next;
        pool_.return_node(node);
        node = next;
      }
      head_ = nullptr;
      tail_ = nullptr;
      size_ = 0;
    }

    [[nodiscard]] constexpr auto size() const noexcept -> size_t { return size_; }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }
    [[nodiscard]] constexpr auto get_allocator() const -> AllocatorTp { return pool_.get_allocator(); }

  private:
    [[nodiscard]] auto as_nodes() const
    {
      std::vector<Node const *> nodes;
      nodes.reserve(size_);
      for (Node const *node = head_; node != nullptr; node = node->next) { nodes.push_back(node); }
      return nodes;
    }

    Pool pool_;
    Node *head_ = nullptr;
    Node *tail_ = nullptr;
    size_t size_ = 0;
  };

  using Sequence = BasicSequence<>;

}// namespace compute

}// namespace vkgsplat
