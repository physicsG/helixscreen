// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file standard_macros.h
 * @brief Unified registry for mapping semantic operations to printer macros
 *
 * The StandardMacros system provides:
 * - Semantic macro slots (Load Filament, Pause, Clean Nozzle, etc.)
 * - Auto-detection from printer via naming patterns
 * - Fallback to HELIX_* helper macros when printer doesn't have its own
 * - User configuration via Settings overlay
 * - Graceful handling of empty slots
 *
 * @pattern Singleton with priority-based resolution
 * @threading Main thread only (not thread-safe)
 */

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Forward declarations
class IMoonrakerAPI;
struct MoonrakerError;

namespace helix {
class PrinterDiscovery;
}

/**
 * @brief Standard macro slot identifiers
 *
 * These represent semantic operations that can be mapped to printer-specific macros.
 */
enum class StandardMacroSlot {
    LoadFilament,   ///< Load filament into toolhead
    UnloadFilament, ///< Unload filament from toolhead
    Purge,          ///< Purge/prime nozzle
    Pause,          ///< Pause current print
    Resume,         ///< Resume paused print
    Cancel,         ///< Cancel current print
    BedMesh,        ///< Bed mesh calibration (BED_MESH_CALIBRATE/G29)
    BedLevel,       ///< Physical bed leveling (QGL/Z-Tilt)
    CleanNozzle,    ///< Nozzle cleaning/wiping
    HeatSoak,       ///< Chamber/bed heat soak

    COUNT ///< Number of slots (for iteration)
};

/**
 * @brief Source of the macro assignment for a slot
 */
enum class MacroSource {
    NONE,       ///< No macro assigned
    CONFIGURED, ///< User explicitly configured in Settings
    DETECTED,   ///< Auto-detected from printer
    FALLBACK    ///< Using HELIX_* fallback macro
};

/**
 * @brief Information about a standard macro slot
 *
 * Contains the slot's identity, current assignment, and resolution details.
 */
struct StandardMacroInfo {
    StandardMacroSlot slot; ///< The slot enum value

    std::string slot_name;    ///< Machine name: "load_filament"
    std::string display_name; ///< English key for translation: "Load Filament"

    /// @brief Get translated display name for current locale
    [[nodiscard]] const char* translated_name() const;

    std::string configured_macro; ///< User override (or empty)
    std::string detected_macro;   ///< Auto-detected (or empty)
    std::string fallback_macro;   ///< HELIX_* fallback (or empty)

    /**
     * @brief Check if this slot has no usable macro
     * @return true if all three sources are empty
     */
    [[nodiscard]] bool is_empty() const {
        return configured_macro.empty() && detected_macro.empty() && fallback_macro.empty();
    }

    /**
     * @brief Get the resolved macro name
     *
     * Priority: configured > detected > fallback
     *
     * @return First non-empty macro name, or empty string if none
     */
    [[nodiscard]] std::string get_macro() const {
        if (!configured_macro.empty())
            return configured_macro;
        if (!detected_macro.empty())
            return detected_macro;
        return fallback_macro;
    }

    /**
     * @brief Get the source of the current macro assignment
     * @return The MacroSource indicating where the macro came from
     */
    [[nodiscard]] MacroSource get_source() const {
        if (!configured_macro.empty())
            return MacroSource::CONFIGURED;
        if (!detected_macro.empty())
            return MacroSource::DETECTED;
        if (!fallback_macro.empty())
            return MacroSource::FALLBACK;
        return MacroSource::NONE;
    }
};

/**
 * @brief Unified registry for standard macro operations (singleton)
 *
 * Maps semantic operations (Load Filament, Pause, etc.) to printer-specific
 * G-code macros using a priority-based resolution system:
 *
 * 1. User configured - Explicit selection in Settings
 * 2. Auto-detected - Found on printer via pattern matching
 * 3. HELIX fallback - HelixScreen's helper macro (if available)
 * 4. Empty - No macro; functionality disabled
 *
 * @code
 * // Initialize after printer discovery
 * StandardMacros::instance().init(capabilities);
 *
 * // Execute a macro
 * StandardMacros::instance().execute(
 *     StandardMacroSlot::LoadFilament, api,
 *     []() { spdlog::info("Loading..."); },
 *     [](const auto& err) { spdlog::error("Failed: {}", err.message); }
 * );
 *
 * // Check if slot is available
 * if (!StandardMacros::instance().get(StandardMacroSlot::CleanNozzle).is_empty()) {
 *     // Show clean nozzle button
 * }
 * @endcode
 */
class StandardMacros {
  public:
    using SuccessCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const MoonrakerError&)>;

    /**
     * @brief Get singleton instance
     * @return Reference to global StandardMacros instance
     */
    static StandardMacros& instance();

    // Non-copyable
    StandardMacros(const StandardMacros&) = delete;
    StandardMacros& operator=(const StandardMacros&) = delete;

    /**
     * @brief Initialize with hardware discovery
     *
     * Call this after printer discovery to enable auto-detection.
     * Loads user config and runs pattern matching on available macros.
     *
     * @param hardware Hardware discovery with discovered macros
     */
    void init(const helix::PrinterDiscovery& hardware);

    /**
     * @brief Reset to uninitialized state
     *
     * Clears all detected macros. User config is preserved.
     * Call init() again after reconnecting to printer.
     */
    void reset();

    /**
     * @brief Check if initialized
     * @return true if init() has been called
     */
    [[nodiscard]] bool is_initialized() const {
        return initialized_;
    }

    // ========================================================================
    // Slot Access
    // ========================================================================

    /**
     * @brief Get info for a specific slot
     * @param slot The slot to query
     * @return Reference to slot info (valid until next init/reset)
     */
    [[nodiscard]] const StandardMacroInfo& get(StandardMacroSlot slot) const;

    /**
     * @brief Get all slot infos
     *
     * Returns slots in enum order. Useful for UI listing.
     *
     * @return Vector of all slot infos
     */
    [[nodiscard]] const std::vector<StandardMacroInfo>& all() const {
        return slots_;
    }

    /**
     * @brief Get slot enum from slot name
     * @param name Slot name (e.g., "load_filament")
     * @return Slot enum, or nullopt if not found
     */
    [[nodiscard]] static std::optional<StandardMacroSlot> slot_from_name(const std::string& name);

    /**
     * @brief Get slot name from enum
     * @param slot The slot enum
     * @return Slot name string
     */
    [[nodiscard]] static std::string slot_to_name(StandardMacroSlot slot);

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set user-configured macro for a slot
     *
     * Pass empty string to clear configuration and use auto-detection.
     * Automatically saves to config file.
     *
     * @param slot The slot to configure
     * @param macro Macro name, or empty to clear
     */
    void set_macro(StandardMacroSlot slot, const std::string& macro);

    /**
     * @brief Load slot configurations from config file
     */
    void load_from_config();

    /**
     * @brief Save current configurations to config file
     */
    void save_to_config();

    // ========================================================================
    // Execution
    // ========================================================================

    /**
     * @brief Execute the macro for a slot
     *
     * Resolves the macro using priority chain, then executes via API.
     * If slot is empty, returns false immediately without calling callbacks.
     *
     * @param slot The slot to execute
     * @param api IMoonrakerAPI instance for execution
     * @param on_success Called when macro execution starts
     * @param on_error Called on execution failure
     * @param suppress_auto_toast Maps to helix::rpc_error_policy::CallerIntent::silent
     *        — see the parameterized overload below for the full contract.
     * @return true if macro was found and execution attempted,
     *         false if slot is empty (no callbacks called)
     */
    bool execute(StandardMacroSlot slot, IMoonrakerAPI* api, SuccessCallback on_success,
                 ErrorCallback on_error, uint32_t timeout_ms = 0, bool suppress_auto_toast = false);

    /**
     * @brief Execute macro with parameters
     *
     * @param slot The slot to execute
     * @param api IMoonrakerAPI instance for execution
     * @param params Parameters to pass to macro
     * @param on_success Called when macro execution starts
     * @param on_error Called on execution failure
     * @param timeout_ms Timeout override (0 = default macro timeout)
     * @param suppress_auto_toast Maps to helix::rpc_error_policy::CallerIntent::silent.
     *        If true, the Request Tracker will NOT emit the generic "Printer
     *        command '...' failed" RPC_ERROR toast on failure; the caller's
     *        on_error callback is expected to surface the error to the user with
     *        action-specific context. The cross-source dedup that suppresses
     *        Klipper's `!!` broadcast for the same root cause follows from
     *        on_error being a real user-facing report, not from this flag (see
     *        rpc_error_policy.h and rpc_error_correlation.h).
     * @return true if macro was found and execution attempted
     */
    bool execute(StandardMacroSlot slot, IMoonrakerAPI* api,
                 const std::map<std::string, std::string>& params, SuccessCallback on_success,
                 ErrorCallback on_error, uint32_t timeout_ms = 0, bool suppress_auto_toast = false);

  private:
    StandardMacros();
    ~StandardMacros() = default;

    /**
     * @brief Initialize slot definitions (names, display names, fallbacks)
     */
    void init_slot_definitions();

    /**
     * @brief Run auto-detection for all slots
     * @param hardware Hardware discovery with macro list
     */
    void auto_detect(const helix::PrinterDiscovery& hardware);

    /**
     * @brief Try to detect a macro for a slot using patterns
     * @param hardware Hardware discovery
     * @param slot Slot to detect for
     * @param patterns Patterns to match (uppercase)
     * @return Detected macro name, or empty if none found
     */
    std::string try_detect(const helix::PrinterDiscovery& hardware, StandardMacroSlot slot,
                           const std::vector<std::string>& patterns);

    std::vector<StandardMacroInfo> slots_;
    bool initialized_ = false;
};
