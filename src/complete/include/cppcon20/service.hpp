#pragma once

#include <cassert>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <asio/execution_context.hpp>

namespace cppcon20 {

class service : public ::asio::execution_context::service {
private:
  struct node_base {
    node_base() = default;
    node_base(const node_base&) = delete;
    node_base& operator=(const node_base&) = delete;
    node_base* next;
    node_base* prev{nullptr};
    virtual ~node_base() = default;
  };
  static_assert(!std::is_copy_constructible_v<node_base>);
  static_assert(!std::is_move_constructible_v<node_base>);
  static_assert(!std::is_copy_assignable_v<node_base>);
  static_assert(!std::is_move_assignable_v<node_base>);
  template<typename T>
  struct node final : public T, public node_base {
    template<typename... Args>
    explicit node(Args&&... args) noexcept(std::is_nothrow_constructible_v<T,
      Args&&...>) : T(std::forward<Args>(args)...) {}
  };
public:
  using key_type = service;
  static_assert(std::is_base_of_v<key_type, service>);
  static inline ::asio::execution_context::id id;
  explicit service(::asio::execution_context& ctx) : ::asio::execution_context::
    service(ctx) {}
  service() = delete;
  service(const service&) = delete;
  service& operator=(const service&) = delete;
  ~service() noexcept {
    assert(!begin_);
  }
  virtual void shutdown() override {
    while (begin_) {
      auto&& tmp = *begin_;
      begin_ = tmp.next;
      delete &tmp;
    }
  }
  template<typename T, typename... Args>
  T* create(Args&&... args) {
    auto ptr = new node<T>(std::forward<Args>(args)...);
    node_base& base = *ptr;
    base.next = begin_;
    if (begin_) {
      begin_->prev = &base;
    }
    begin_ = &base;
    return ptr;
  }
  template<typename T>
  void destroy(T* obj) noexcept {
    if (!begin_) {
      return;
    }
    assert(obj);
    using node_type = node<T>;
    auto&& node = static_cast<node_type&>(*obj);
    const node_base& base = node;
#ifndef NDEBUG
    bool found = false;
    for (auto curr = begin_; curr; curr = curr->next) {
      if (curr == &base) {
        found = true;
        break;
      }
    }
    assert(found);
#endif
    if (base.next) {
      base.next->prev = base.prev;
    }
    if (base.prev) {
      base.prev->next = base.next;
    } else {
      begin_ = base.next;
    }
    delete std::addressof(node);
  }
private:
  node_base* begin_{nullptr};
};

}
