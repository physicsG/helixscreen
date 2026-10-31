// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// helix::Callback<R(Args...)>: a move-only std::function for hot choke-point
// APIs, built to cost less flash per lambda type.
//
// std::function emits a manager per stored type (clone, destroy, address,
// type query), and on a 32-bit target it stores only 8 bytes inline, so
// almost every capture is heap-stored behind a manager of 80 B or more. Here:
//   - a trivially copyable callable that fits the inline buffer has NO
//     per-type manager: moving it copies the buffer, destroying it is nothing;
//   - any other callable that fits gets a per-type move+destroy manager;
//     moving it runs its move constructor, which UpdateQueue does under its
//     mutex, so a callable's move constructor must not queue work;
//   - an oversized callable lives on the heap behind a destroy-only manager,
//     since moving it moves only the pointer.
// The invoker is per type in every case; it is the callable's own body.
//
// Move-only, so a callable may capture a unique_ptr. No RTTI, no exceptions:
// calling an empty Callback goes through throw_or_abort().

#include "exception_policy.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace helix {

namespace callback_detail {

/// Inline capacity in pointer-sized words. Eight holds `this` plus a
/// shared_ptr token, a generation and a tag (AsyncLifetimeGuard::defer's
/// wrapper) with a few words to spare on a 32-bit target.
inline constexpr std::size_t kInlineWords = 8;
inline constexpr std::size_t kInlineSize = kInlineWords * sizeof(void*);
inline constexpr std::size_t kInlineAlign = alignof(std::max_align_t);

enum class Op { Relocate, Destroy };

/// Out of line so every signature shares one copy of the abort path.
[[noreturn]] inline __attribute__((noinline)) void call_empty() {
    throw_or_abort(std::bad_function_call());
}

template <typename D>
inline constexpr bool kStoredInline = sizeof(D) <= kInlineSize && alignof(D) <= kInlineAlign &&
                                      std::is_nothrow_move_constructible_v<D>;

template <typename D>
inline constexpr bool kTrivialInline = kStoredInline<D> && std::is_trivially_copyable_v<D>;

} // namespace callback_detail

/// Where a Callback keeps a callable of type F, and what that costs in code.
enum class CallbackStorage {
    Trivial, ///< inline, no per-type manager
    Inline,  ///< inline, per-type move+destroy manager
    Heap,    ///< heap-allocated, per-type destroy manager
};

template <typename F> constexpr CallbackStorage callback_storage() {
    using D = std::decay_t<F>;
    if constexpr (callback_detail::kTrivialInline<D>) {
        return CallbackStorage::Trivial;
    } else if constexpr (callback_detail::kStoredInline<D>) {
        return CallbackStorage::Inline;
    } else {
        return CallbackStorage::Heap;
    }
}

template <typename Sig> class Callback;
template <typename Sig> class SharedCallback;

template <typename R, typename... Args> class Callback<R(Args...)> {
    template <typename F>
    using EnableIfCallable = std::enable_if_t<!std::is_same_v<std::decay_t<F>, Callback> &&
                                              std::is_invocable_r_v<R, std::decay_t<F>&, Args...>>;

  public:
    Callback() noexcept = default;
    Callback(std::nullptr_t) noexcept {}
    /// Never converts implicitly into a std::function: an empty one would become a
    /// non-empty std::function, and calling that aborts. Under libc++ (the macOS
    /// build) argument passing and copy-initialization are compile errors, so a
    /// Mac build lists every such site; libstdc++ (Linux, ESP32) prefers
    /// std::function's own constructor and refuses nothing, and direct-initialization,
    /// emplace and assignment wrap on both. Hand a std::function a lambda that
    /// tests this first.
    template <typename S> operator std::function<S>() const = delete;

    template <typename F, typename = EnableIfCallable<F>> Callback(F&& f) {
        using D = std::decay_t<F>;
        if constexpr (std::is_pointer_v<D> || std::is_member_pointer_v<D> ||
                      is_callback_like<D>::value) {
            if (!f) {
                return;
            }
        }
        if constexpr (callback_detail::kStoredInline<D>) {
            ::new (static_cast<void*>(storage_)) D(std::forward<F>(f));
            invoke_ = &invoke_inline<D>;
            if constexpr (!callback_detail::kTrivialInline<D>) {
                manage_ = &manage_inline<D>;
            }
        } else {
            D* heap = new D(std::forward<F>(f));
            std::memcpy(storage_, &heap, sizeof(heap));
            invoke_ = &invoke_heap<D>;
            manage_ = &manage_heap<D>;
        }
    }

    Callback(Callback&& other) noexcept {
        take(other);
    }

    /// Takes `other` before destroying the held callable, since that callable
    /// may own `other`. Self-move leaves the callable in place.
    Callback& operator=(Callback&& other) noexcept {
        Callback tmp(std::move(other));
        reset();
        take(tmp);
        return *this;
    }

    Callback& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    template <typename F, typename = EnableIfCallable<F>> Callback& operator=(F&& f) {
        return *this = Callback(std::forward<F>(f));
    }

    Callback(const Callback&) = delete;
    Callback& operator=(const Callback&) = delete;

    ~Callback() {
        reset();
    }

    explicit operator bool() const noexcept {
        return invoke_ != nullptr;
    }

    /// Like std::function, a const call may run a `mutable` callable.
    R operator()(Args... args) const {
        if (invoke_ == nullptr) {
            callback_detail::call_empty();
        }
        return invoke_(storage_, std::forward<Args>(args)...);
    }

  private:
    using Invoker = R (*)(void* storage, Args&&... args);
    using Manager = void (*)(callback_detail::Op op, void* dst, void* src);

    template <typename T> struct is_callback_like : std::false_type {};
    template <typename S> struct is_callback_like<std::function<S>> : std::true_type {};
    template <typename S> struct is_callback_like<Callback<S>> : std::true_type {};
    template <typename S> struct is_callback_like<SharedCallback<S>> : std::true_type {};

    template <typename D> static R invoke_inline(void* storage, Args&&... args) {
        return std::invoke(*std::launder(static_cast<D*>(storage)), std::forward<Args>(args)...);
    }

    template <typename D> static R invoke_heap(void* storage, Args&&... args) {
        D* heap;
        std::memcpy(&heap, storage, sizeof(heap));
        return std::invoke(*heap, std::forward<Args>(args)...);
    }

    template <typename D> static void manage_inline(callback_detail::Op op, void* dst, void* src) {
        D* from = std::launder(static_cast<D*>(src));
        if (op == callback_detail::Op::Relocate) {
            ::new (dst) D(std::move(*from));
        }
        from->~D();
    }

    template <typename D> static void manage_heap(callback_detail::Op op, void* dst, void* src) {
        if (op == callback_detail::Op::Relocate) {
            std::memcpy(dst, src, sizeof(D*));
            return;
        }
        D* heap;
        std::memcpy(&heap, src, sizeof(heap));
        delete heap;
    }

    void take(Callback& other) noexcept {
        if (other.invoke_ == nullptr) {
            return;
        }
        if (other.manage_ == nullptr) {
            std::memcpy(storage_, other.storage_, sizeof(storage_));
        } else {
            other.manage_(callback_detail::Op::Relocate, storage_, other.storage_);
        }
        invoke_ = other.invoke_;
        manage_ = other.manage_;
        other.invoke_ = nullptr;
        other.manage_ = nullptr;
    }

    void reset() noexcept {
        if (manage_ != nullptr) {
            manage_(callback_detail::Op::Destroy, nullptr, storage_);
        }
        invoke_ = nullptr;
        manage_ = nullptr;
    }

    alignas(
        callback_detail::kInlineAlign) mutable unsigned char storage_[callback_detail::kInlineSize];
    Invoker invoke_ = nullptr;
    Manager manage_ = nullptr;
};

} // namespace helix
