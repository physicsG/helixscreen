// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// helix::SharedCallback<R(Args...)>: the copyable sibling of helix::Callback, for
// APIs whose callbacks are copied into wrappers, pending-request tables and
// retry paths (the Moonraker network layer).
//
// A small trivially copyable callable is stored inline exactly as in Callback,
// with no per-type manager. Any other callable lives in one heap block with an
// atomic reference count, so copying bumps the count and the per-type code is a
// destroy function only. Copies share that one callable: a `mutable` callable's
// state is shared between them rather than cloned, and its captures are
// destroyed with the last copy, on whichever thread releases it.

#include "helix_callback.h"

#include <atomic>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace helix {

template <typename Sig> class SharedCallback;

namespace callback_detail {

/// Callables with an empty state, which a SharedCallback adopts as its own.
template <typename T>
struct IsNullableForShared
    : std::bool_constant<std::is_pointer_v<T> || std::is_member_pointer_v<T>> {};
template <typename S> struct IsNullableForShared<std::function<S>> : std::true_type {};
template <typename S> struct IsNullableForShared<SharedCallback<S>> : std::true_type {};
template <typename S> struct IsNullableForShared<Callback<S>> : std::true_type {};

/// Reference-count header of a SharedCallback heap block.
struct SharedBlock {
    std::atomic<unsigned> refs{1};
};

template <typename D> struct SharedBox : SharedBlock {
    template <typename F> explicit SharedBox(F&& f) : value(std::forward<F>(f)) {}
    D value;
};

} // namespace callback_detail

namespace callback_detail {

/// Signature-independent half of SharedCallback. Copy, move and release are
/// the same bytes for every signature, so they live here out of line: each
/// call site that copies or drops a SharedCallback emits one call.
class SharedCallbackBase {
  public:
    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

  protected:
    using ErasedInvoker = void (*)();
    using Destroyer = void (*)(SharedBlock* block);

    SharedCallbackBase() noexcept = default;
    SharedCallbackBase(const SharedCallbackBase& other) noexcept {
        copy_from(other);
    }
    SharedCallbackBase(SharedCallbackBase&& other) noexcept {
        steal(other);
    }
    ~SharedCallbackBase() {
        reset();
    }

    SharedCallbackBase& operator=(const SharedCallbackBase&) = delete;
    SharedCallbackBase& operator=(SharedCallbackBase&&) = delete;

    __attribute__((noinline)) void copy_from(const SharedCallbackBase& other) noexcept {
        std::memcpy(storage_, other.storage_, sizeof(storage_));
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        if (destroy_ != nullptr) {
            block()->refs.fetch_add(1, std::memory_order_relaxed);
        }
    }

    __attribute__((noinline)) void steal(SharedCallbackBase& other) noexcept {
        std::memcpy(storage_, other.storage_, sizeof(storage_));
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        other.invoke_ = nullptr;
        other.destroy_ = nullptr;
    }

    __attribute__((noinline)) void reset() noexcept {
        if (destroy_ != nullptr) {
            SharedBlock* b = block();
            if (b->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                destroy_(b);
            }
        }
        invoke_ = nullptr;
        destroy_ = nullptr;
    }

    void assign_copy(const SharedCallbackBase& other) noexcept {
        if (this != &other) {
            reset();
            copy_from(other);
        }
    }

    void assign_move(SharedCallbackBase& other) noexcept {
        if (this != &other) {
            reset();
            steal(other);
        }
    }

    SharedBlock* block() const noexcept {
        SharedBlock* b;
        std::memcpy(&b, storage_, sizeof(b));
        return b;
    }

    alignas(kInlineAlign) mutable unsigned char storage_[kInlineSize];
    ErasedInvoker invoke_ = nullptr;
    Destroyer destroy_ = nullptr;
};

} // namespace callback_detail

template <typename R, typename... Args>
class SharedCallback<R(Args...)> : public callback_detail::SharedCallbackBase {
    template <typename F>
    using EnableIfCallable = std::enable_if_t<!std::is_same_v<std::decay_t<F>, SharedCallback> &&
                                              std::is_invocable_r_v<R, std::decay_t<F>&, Args...>>;
    using Invoker = R (*)(void* storage, Args&&... args);

  public:
    SharedCallback() noexcept = default;
    /// Never converts implicitly into a std::function: an empty one would become a
    /// non-empty std::function, and calling that aborts. Under libc++ (the macOS
    /// build) argument passing and copy-initialization are compile errors, so a
    /// Mac build lists every such site; libstdc++ (Linux, ESP32) prefers
    /// std::function's own constructor and refuses nothing, and direct-initialization,
    /// emplace and assignment wrap on both. Hand a std::function a lambda that
    /// tests this first.
    template <typename S> operator std::function<S>() const = delete;
    SharedCallback(std::nullptr_t) noexcept {}
    SharedCallback(const SharedCallback& other) noexcept = default;
    SharedCallback(SharedCallback&& other) noexcept = default;

    template <typename F, typename = EnableIfCallable<F>> SharedCallback(F&& f) {
        using D = std::decay_t<F>;
        if constexpr (callback_detail::IsNullableForShared<D>::value) {
            if (!f) {
                return;
            }
        }
        if constexpr (callback_detail::kTrivialInline<D>) {
            ::new (static_cast<void*>(storage_)) D(std::forward<F>(f));
            set_invoker(&invoke_inline<D>);
        } else {
            callback_detail::SharedBlock* b = new callback_detail::SharedBox<D>(std::forward<F>(f));
            std::memcpy(storage_, &b, sizeof(b));
            set_invoker(&invoke_shared<D>);
            destroy_ = &destroy_shared<D>;
        }
    }

    SharedCallback& operator=(const SharedCallback& other) noexcept {
        assign_copy(other);
        return *this;
    }

    SharedCallback& operator=(SharedCallback&& other) noexcept {
        assign_move(other);
        return *this;
    }

    SharedCallback& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    template <typename F, typename = EnableIfCallable<F>> SharedCallback& operator=(F&& f) {
        SharedCallback tmp(std::forward<F>(f));
        assign_move(tmp);
        return *this;
    }

    friend bool operator==(const SharedCallback& cb, std::nullptr_t) noexcept {
        return !cb;
    }
    friend bool operator!=(const SharedCallback& cb, std::nullptr_t) noexcept {
        return static_cast<bool>(cb);
    }

    /// Like std::function, a const call may run a `mutable` callable.
    R operator()(Args... args) const {
        if (invoke_ == nullptr) {
            callback_detail::call_empty();
        }
        return reinterpret_cast<Invoker>(invoke_)(storage_, std::forward<Args>(args)...);
    }

  private:
    void set_invoker(Invoker fn) noexcept {
        invoke_ = reinterpret_cast<ErasedInvoker>(fn);
    }

    template <typename D> static R invoke_inline(void* storage, Args&&... args) {
        return std::invoke(*std::launder(static_cast<D*>(storage)), std::forward<Args>(args)...);
    }

    template <typename D> static R invoke_shared(void* storage, Args&&... args) {
        callback_detail::SharedBlock* b;
        std::memcpy(&b, storage, sizeof(b));
        return std::invoke(static_cast<callback_detail::SharedBox<D>*>(b)->value,
                           std::forward<Args>(args)...);
    }

    template <typename D> static void destroy_shared(callback_detail::SharedBlock* block) {
        delete static_cast<callback_detail::SharedBox<D>*>(block);
    }
};

} // namespace helix
