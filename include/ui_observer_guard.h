// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file ui_observer_guard.h
 * @brief RAII wrapper for LVGL observer cleanup with subject lifetime tracking
 *
 * @pattern Guard that removes observer on destruction; release() for pre-destroyed subjects.
 *          For dynamic subjects (per-fan, per-sensor, per-extruder), use SubjectLifetime
 *          tokens to prevent use-after-free when subjects are deinited before observers.
 * @threading Main thread only (invalidate_all uses an atomic epoch for safety)
 * @gotchas Checks lv_is_initialized() - safe during LVGL shutdown.
 *          Dynamic subjects MUST provide a SubjectLifetime token — see printer_fan_state.h,
 *          temperature_sensor_manager.h, printer_temperature_state.h.
 */

#pragma once

#include "lvgl/lvgl.h"

#include <atomic>
#include <functional>
#include <memory>
#include <unordered_set>
#include <utility>

/**
 * @brief Shared token that tracks whether a dynamic subject is still alive.
 *
 * Dynamic subject owners (PrinterFanState, TemperatureSensorManager,
 * PrinterTemperatureState) create a SubjectLifetime per dynamic subject.
 * The bool value signals subject liveness: true = alive, false = dead.
 *
 * Before destroying a subject, owners MUST set *lifetime = false, then
 * reset(). Multiple services may hold shared_ptr copies of the same
 * lifetime — setting the bool ensures ALL ObserverGuards detect subject
 * death, even when the shared_ptr refcount hasn't reached zero. (#816)
 *
 * Static subjects (singleton lifetime) don't need this — pass empty token.
 */
using SubjectLifetime = std::shared_ptr<bool>;

/**
 * @brief Lifetime token claiming the subject is never freed before process exit.
 *
 * A claim, not a default. Legitimate only where the observer provably cannot
 * outlive the subject: theme globals (file-static, reseeded in place by
 * theme_manager_init()), or a subject that is a member of the very object
 * doing the observing and dies with it. For any subject owned elsewhere,
 * pass that owner's get_subjects_lifetime() instead — claiming never-freed
 * about a subject that does get freed disarms ObserverGuard::reset()'s
 * liveness check and defends nothing.
 */
// NAMESPACE_OK: sits with SubjectLifetime and ObserverGuard at file scope
inline SubjectLifetime subject_never_freed() {
    // Leaked: ObserverGuard::reset() compares against it from static destructors.
    static const auto* token = new SubjectLifetime(std::make_shared<bool>(true));
    return *token;
}

/**
 * @brief RAII wrapper for LVGL observers - auto-removes on destruction
 *
 * For observers on dynamic subjects, set an alive token via set_alive_token()
 * or the factory functions. When the token expires (subject deinited), reset()
 * skips lv_observer_remove() because the observer was already freed by
 * lv_subject_deinit().
 */
class ObserverGuard {
  public:
    ObserverGuard() = default;

    ObserverGuard(lv_subject_t* subject, lv_observer_cb_t cb, void* user_data)
        : observer_(lv_subject_add_observer(subject, cb, user_data)),
          created_epoch_(s_invalidation_epoch.load(std::memory_order_acquire)), subject_(subject) {}

    /// Construct with a cleanup callback for the user_data context.
    /// The cleanup runs in reset() to free the context and expire weak tokens.
    ObserverGuard(lv_subject_t* subject, lv_observer_cb_t cb, void* user_data,
                  std::function<void()> cleanup)
        : observer_(lv_subject_add_observer(subject, cb, user_data)),
          created_epoch_(s_invalidation_epoch.load(std::memory_order_acquire)), subject_(subject),
          cleanup_(std::move(cleanup)) {}

    ~ObserverGuard() {
        reset();
    }

    ObserverGuard(ObserverGuard&& other) noexcept
        : observer_(std::exchange(other.observer_, nullptr)),
          alive_token_(std::move(other.alive_token_)),
          has_alive_token_(std::exchange(other.has_alive_token_, false)),
          created_epoch_(other.created_epoch_), subject_(std::exchange(other.subject_, nullptr)),
          // std::exchange, not std::move: a moved-from std::function is left in
          // a valid but UNSPECIFIED state, and libc++ keeps the target when the
          // callable fits its small-object buffer - which `[ctx]{ delete ctx; }`
          // does. A source left holding the cleanup deletes the observer's
          // context from its own destructor while this guard keeps the observer
          // attached, so the next notify reads freed memory.
          cleanup_(std::exchange(other.cleanup_, nullptr)) {}

    ObserverGuard& operator=(ObserverGuard&& other) noexcept {
        if (this != &other) {
            reset();
            observer_ = std::exchange(other.observer_, nullptr);
            alive_token_ = std::move(other.alive_token_);
            has_alive_token_ = std::exchange(other.has_alive_token_, false);
            created_epoch_ = other.created_epoch_;
            subject_ = std::exchange(other.subject_, nullptr);
            cleanup_ = std::exchange(other.cleanup_, nullptr);
        }
        return *this;
    }

    ObserverGuard(const ObserverGuard&) = delete;
    ObserverGuard& operator=(const ObserverGuard&) = delete;

    /**
     * @brief Signal that the registry's subjects have been torn down (soft restart).
     *
     * Bumps a monotonic invalidation epoch. A guard carrying its subject owner's
     * lifetime token ignores it: the owner flips the token before freeing the subject.
     * Any other ObserverGuard (no token, or subject_never_freed()) created BEFORE
     * this call whose subject was freed by StaticSubjectRegistry::deinit_all()
     * (LVGL already removed+freed the observer) will skip
     * lv_observer_remove() to avoid touching freed memory — reset() consults
     * the teardown exemption (mark_subject_teardown_exempt()), so guards on
     * subjects the registry never frees (the reseeded theme globals) still
     * remove theirs.
     * Observers created AFTER this call — e.g. widgets built during
     * init_printer_state()'s finalize_setup() — are
     * attached to live subjects and are removed normally on reset().
     *
     * Call invalidate_all() AFTER StaticSubjectRegistry::deinit_all() and
     * BEFORE any re-initialization.
     *
     * The per-creation epoch is why the old global boolean was insufficient:
     * a boolean suppressed removal for window-created observers too, orphaning
     * live observers on live subjects → use-after-free when later notified
     * (debug bundles 449TVQ82 / X3RA4252, LedWidget on the static LED subject).
     */
    /// @p process_exit: every subject is about to be freed whatever its owner says, so
    /// owner tokens stop being trusted and every older guard skips removal.
    static void invalidate_all(bool process_exit = false) {
        if (process_exit) {
            s_process_exit.store(true, std::memory_order_release);
        }
        s_invalidation_epoch.fetch_add(1, std::memory_order_release);
    }
    /// Epoch accessors for the test fixture, which rolls back a simulated teardown so it
    /// cannot make a later test's long-lived guards skip removal from live subjects.
    static uint64_t invalidation_epoch() {
        return s_invalidation_epoch.load(std::memory_order_acquire);
    }
    static void restore_invalidation_epoch(uint64_t epoch) {
        s_invalidation_epoch.store(epoch, std::memory_order_release);
    }

    /**
     * @brief Declare that a subject is never freed by a registry teardown.
     *
     * The invalidation epoch is process-global: it says a
     * StaticSubjectRegistry::deinit_all() ran, not that any given subject was
     * one of the freed ones. Subjects that deinit_all() never touches — the
     * file-static theme globals, reseeded in place by theme_manager_init()
     * rather than torn down — keep every observer allocated across the bump,
     * so suppressing their removal orphans a live observer node whose context
     * reset() is about to free. Owners register such subjects here, at the
     * same place they initialize them.
     *
     * Registering a subject that IS in fact torn down while LVGL is
     * initialized converts its guards' skip into a use-after-free, so the
     * claim "no deinit path frees this subject mid-process" must hold.
     *
     * @threading Main thread only, like the rest of this class's statics.
     */
    static void mark_subject_teardown_exempt(lv_subject_t* subject) {
        if (subject != nullptr) {
            teardown_exempt_subjects().insert(subject);
        }
    }

    /// Whether @p subject was registered by mark_subject_teardown_exempt().
    static bool subject_is_teardown_exempt(const lv_subject_t* subject) {
        return subject != nullptr && teardown_exempt_subjects().count(subject) != 0;
    }

    void reset() {
        if (observer_) {
            // If we have a lifetime token and either (a) it expired (all shared_ptrs
            // gone), or (b) the pointed-to bool was set to false (source signaled
            // subject death), the subject's observers were already freed by
            // lv_subject_deinit(). Calling lv_observer_remove() would crash.
            //
            // Check (b) is critical: multiple services may hold shared_ptr copies of
            // the same SubjectLifetime. When the source resets its copy and deinits
            // the subject, other holders keep the shared_ptr alive (refcount > 0),
            // so expired() returns false. But the bool value was set to false by the
            // source before destruction, allowing us to detect the dead subject.
            // (#816, #673)
            bool subject_dead = false;
            // A token from the subject's owner is authoritative: the owner flips it before
            // freeing the subject, so the epoch below has nothing to add. The shared
            // never-freed claim names no owner and falls through to the epoch.
            bool owner_token = false;
            if (has_alive_token_) {
                auto locked = alive_token_.lock();
                subject_dead = !locked || !*locked;
                owner_token = locked && locked != subject_never_freed();
            }
            // An observer created before the most recent invalidate_all() MAY
            // have had its subject freed by StaticSubjectRegistry::deinit_all()
            // — then LVGL already removed+freed the observer and
            // lv_observer_remove() would touch freed memory. The epoch is
            // process-global though: it says a registry teardown happened, not
            // that THIS subject was one of the freed ones. A subject declared
            // teardown-exempt by its owner (the reseeded theme globals) keeps
            // its observers allocated across the bump, and skipping removal
            // there orphans a live observer node whose context we are about to
            // free → UAF on the next notify (#449TVQ82, #X3RA4252). An
            // observer created during/after the window (e.g. a widget built
            // mid-reinit) is on a LIVE subject and is removed normally —
            // created_epoch_ distinguishes the two cases; the old global
            // boolean could not.
            bool freed_by_deinit =
                (!owner_token || s_process_exit.load(std::memory_order_acquire)) &&
                created_epoch_ < s_invalidation_epoch.load(std::memory_order_acquire) &&
                !subject_is_teardown_exempt(subject_);
            if (!subject_dead && !freed_by_deinit && lv_is_initialized()) {
                lv_observer_remove(observer_);
            }
            // If LVGL is already torn down or the subject was freed, just release
            observer_ = nullptr;
            alive_token_.reset();
            has_alive_token_ = false;
        }
        // Free the observer context (expires weak_alive tokens in deferred lambdas).
        //
        // OUTSIDE the `if (observer_)` on purpose. The factories allocate the
        // context BEFORE constructing the guard, so an attach that comes back
        // null still leaves us owning it -- and lv_subject_add_observer_obj()
        // returns null for a subject whose type is still LV_SUBJECT_TYPE_INVALID,
        // i.e. any observer installed before its subject's init_subjects() ran.
        // Skipping the cleanup there stranded the context for the process
        // lifetime and showed up as observe_int_*<Owner> origins in the ASan
        // leak ratchet (#1279).
        //
        // Safe for every other path: a normal reset() has just removed the
        // observer above, a second reset() finds cleanup_ already null, and
        // release() deliberately nulls cleanup_ to keep its documented
        // leave-the-context-alive contract.
        if (cleanup_) {
            cleanup_();
            cleanup_ = nullptr;
        }
    }

    /**
     * @brief Attach a subject lifetime token for dynamic subject safety.
     *
     * Call this after construction when observing a dynamic subject.
     * The observer factory functions handle this automatically.
     */
    void set_alive_token(const SubjectLifetime& token) {
        alive_token_ = token;
        has_alive_token_ = true;
    }

    /**
     * @brief Release ownership without calling lv_observer_remove()
     *
     * Use during shutdown when subjects may already be destroyed.
     * The observer will not be removed from the subject (it may already be gone).
     */
    void release() {
        observer_ = nullptr;
        alive_token_.reset();
        has_alive_token_ = false;
        // NOTE: Do NOT call cleanup_ here. release() leaves the observer
        // registered in LVGL, so the ctx (user_data) must remain valid.
        // The ctx becomes a permanent leak, but that's acceptable for the
        // shutdown/pre-deinit paths where release() is used.
        cleanup_ = nullptr;
    }

    explicit operator bool() const {
        return observer_ != nullptr;
    }
    lv_observer_t* get() const {
        return observer_;
    }

  private:
    /// Backing for mark_subject_teardown_exempt(). Deliberately never
    /// destroyed: reset() can run from a static destructor, after any
    /// destructor-ordered container would be gone.
    static std::unordered_set<const lv_subject_t*>& teardown_exempt_subjects() {
        static auto* subjects = new std::unordered_set<const lv_subject_t*>();
        return *subjects;
    }

    /// Monotonic counter bumped by invalidate_all() each teardown. Observers
    /// compare their created_epoch_ against it to decide whether their subject
    /// MAY have been freed by deinit_all() (skip removal unless the subject is
    /// teardown-exempt) or is untouched (remove normally). Replaces the old
    /// global s_subjects_valid boolean, which could not tell window-created
    /// observers from pre-teardown ones.
    static inline std::atomic<uint64_t> s_invalidation_epoch{0};
    static inline std::atomic<bool> s_process_exit{false};

    lv_observer_t* observer_ = nullptr;
    std::weak_ptr<bool> alive_token_; ///< Tracks dynamic subject lifetime
    /// Distinguishes "token never set" from "token expired". Required because a
    /// default-constructed weak_ptr reports expired() == true, which would cause
    /// static-subject guards to falsely skip lv_observer_remove() and leak observers.
    bool has_alive_token_ = false;
    /// Invalidation epoch captured when this guard's observer was registered.
    /// If < s_invalidation_epoch at reset() time, a registry teardown may have
    /// freed the observer — see subject_is_teardown_exempt().
    uint64_t created_epoch_ = 0;
    /// The subject this guard's observer was registered on, consulted for the
    /// teardown exemption during that decision.
    lv_subject_t* subject_ = nullptr;
    /// Cleanup callback to free the observer context (LambdaObserverContext).
    /// When called, destroys the shared_ptr<bool> alive token, expiring weak_ptr
    /// copies held by deferred lambdas so they skip execution on destroyed widgets.
    std::function<void()> cleanup_;
};
