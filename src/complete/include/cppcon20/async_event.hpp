#pragma once

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>
#include <asio/associated_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/execution/allocator.hpp>
#include <asio/execution/context.hpp>
#include <asio/execution/execute.hpp>
#include <asio/execution/executor.hpp>
#include <asio/execution/outstanding_work.hpp>
#include <asio/execution_context.hpp>
#include <asio/io_context.hpp>
#include <asio/prefer.hpp>
#include <asio/query.hpp>
#include "pending.hpp"
#include "service.hpp"

namespace cppcon20 {

template<asio::execution::executor Executor>
struct basic_async_event {
  using executor_type = Executor;
  basic_async_event(basic_async_event&& other) noexcept : state_(std::exchange(
    other.state_, nullptr)) {}
private:
  using signature_type = void();
  using pending_type = pending<signature_type>;
  using pendings_type = std::vector<pending_type>;
  struct state_type {
    explicit state_type(executor_type ex) : executor(std::move(ex)) {}
    executor_type executor;
    pendings_type pendings;
  };
  using service_type = service<state_type>;
  static service_type& get_service(executor_type& ex) {
    return asio::use_service<service_type>(asio::query(ex, asio::execution::
      context));
  }
public:
  explicit basic_async_event(executor_type ex) : state_(get_service(ex).create(
    std::move(ex))) {}
  ~basic_async_event() noexcept {
    if (state_) {
      get_service(state_->executor).destroy(state_);
    }
  }
  auto get_executor() const noexcept {
    assert(state_);
    return state_->executor;
  }
  //  TODO: Move ctor? Move assignment operator?
  std::size_t notify_one() {
    assert(state_);
    if (state_->pendings.empty()) {
      return 0;
    }
    auto pending = std::move(state_->pendings.front());
    assert(pending);
    state_->pendings.erase(state_->pendings.begin());
    pending();
    return 1;
  }
  std::size_t notify_all() {
    assert(state_);
    auto iter = state_->pendings.begin();
    struct guard {
      ~guard() noexcept {
        self.state_->pendings.erase(self.state_->pendings.begin(), it);
      }
      decltype(iter)& it;
      basic_async_event& self;
    };
    guard g{iter, *this};
    std::size_t retr(0);
    for (auto end = state_->pendings.end(); iter != end; ++iter, ++retr) {
      assert(*iter);
      (*iter)();
    }
    return retr;
  }
  template<typename CompletionToken>
  decltype(auto) async_wait(CompletionToken&& token) {
    assert(state_);
    return asio::async_initiate<CompletionToken, signature_type>([
      state = state_](auto h)
    {
      assert(state);
      auto io_ex = asio::prefer(state->executor, asio::execution::
        outstanding_work.tracked);
      auto assoc_ex = asio::get_associated_executor(h, state->executor);
      auto alloc_ex = asio::prefer(std::move(assoc_ex), asio::execution::
        allocator(asio::get_associated_allocator(h)));
      auto ex = asio::prefer(std::move(alloc_ex), asio::execution::
        outstanding_work.tracked);
      state->pendings.emplace_back([h = std::move(h), io_ex = std::move(io_ex),
        ex = std::move(ex)]() mutable
      {
        auto local_ex = std::move(ex);
        asio::execution::execute(local_ex, [h = std::move(h), io_ex = std::move
          (io_ex)]() mutable
        {
          auto local_ex = std::move(io_ex);
          std::move(h)();
        });
      });
    }, token);
  }
private:
  state_type* state_;
};

using async_event = basic_async_event<asio::io_context::executor_type>;

}
