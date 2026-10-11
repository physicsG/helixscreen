// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "screensaver_registry.h"

#ifdef HELIX_ENABLE_SCREENSAVER

#include "screensaver_cpu_clock.h"
#include "screensaver_gate.h"
#include "screensaver_overlay.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <lvgl.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief Color format of the starfield canvas
 *
 * Opaque, so the full-screen canvas draws as a straight copy instead of an alpha blend.
 * Production 32 bpp displays run ARGB8888 and copy an XRGB8888 image byte for byte, so
 * every canvas pixel's X byte must be 0xFF or that pixel draws transparent ("The alpha
 * trap" in docs/devel/GPU_ACCELERATION.md). The starfield writes every pixel itself with
 * that byte set. A canvas drawn with lv_draw_* cannot rely on it: see PIPES_CANVAS_FORMAT.
 */

/**
 * @brief Color format of the pipes canvas
 *
 * Pipes draws antialiased segments and joints with lv_draw_*. On aarch64, LVGL's NEON
 * blends into an XRGB8888 destination write 0 into each pixel's 4th byte, and an XRGB8888
 * canvas is copied byte for byte into the ARGB8888 display, so those pixels would draw
 * transparent. An ARGB8888 canvas keeps real alpha, and it is filled opaque before anything
 * is drawn on it, so it still covers every pixel under the transparent overlay.
 *
 * RGB565 has no 4th byte for a blend to zero, so a 16 bpp build takes the display's own
 * format and the trap above does not arise. This is why pipes does not use
 * SAVER_BUILD_CANVAS_FORMAT: at 32 bpp it needs ARGB8888 specifically, not XRGB8888.
 */
inline constexpr lv_color_format_t PIPES_CANVAS_FORMAT =
    LV_COLOR_DEPTH == 16 ? LV_COLOR_FORMAT_RGB565 : LV_COLOR_FORMAT_ARGB8888;

/**
 * @brief Row pitch in bytes lv_canvas_set_buffer() uses for a w-wide canvas in format cf
 *
 * lv_canvas_set_buffer() does not take the caller's word for packing: it
 * derives an LV_DRAW_BUF_STRIDE_ALIGN-rounded stride and sizes the canvas
 * extent from it, so a buffer allocated at w * h * bytes-per-pixel under-runs
 * the extent whenever the stride exceeds w * bytes-per-pixel
 * (prestonbrown/helixscreen#1591). The canvas screensavers size their
 * allocations and direct pixel writes from this pitch so they cannot disagree
 * with what LVGL steps rows by.
 */
inline uint32_t screensaver_canvas_stride_bytes(int32_t w, lv_color_format_t cf) {
    return lv_draw_buf_width_to_stride(static_cast<uint32_t>(w), cf);
}

class SaverBase;
struct BoardFacts;

} // namespace helix::ui

namespace helix {
struct PlatformCapabilities;
class ScreensaverManagerTestAccess;
} // namespace helix

/**
 * @brief Interface the manager drives every screensaver through
 */
class Screensaver {
  public:
    virtual ~Screensaver() = default;

    /** @brief Create overlay and begin rendering */
    virtual void start() = 0;

    /** @brief Stop rendering and destroy overlay */
    virtual void stop() = 0;

    /** @brief Check if this screensaver is currently running */
    virtual bool is_active() const = 0;

    /** @brief Return the type identifier for this screensaver */
    virtual ScreensaverType type() const = 0;
};

namespace helix::ui {

/// What the screensaver gate needs from the app.
struct SaverHost {
    /// True while a print job holds the machine; unset reads as not printing.
    std::function<bool()> is_printing;
    /// Running display path for the board fingerprint: "sdl", "fbdev", "drm" or "egl".
    std::string display_backend;
};

} // namespace helix::ui

/**
 * @brief Map a HELIX_SCREENSAVER_NOW value onto a screensaver type
 *
 * A name selects that screensaver. Anything else — "1" included — means the
 * configured one, falling back to flying toasters when nothing is configured.
 *
 * @param value      The environment variable's value
 * @param configured The type from settings, used when @p value names none
 */
namespace helix {
ScreensaverType screensaver_type_from_env(const std::string& value, ScreensaverType configured);
} // namespace helix

/**
 * @brief Owns the screensavers, starts the configured one, and keeps it within its CPU budget
 */
class ScreensaverManager {
  public:
    /** @brief Must not be called before LVGL initialization */
    static ScreensaverManager& instance();

    ScreensaverManager(const ScreensaverManager&) = delete;
    ScreensaverManager& operator=(const ScreensaverManager&) = delete;

    /// Samples closer together than this are skipped by on_idle_check_tick().
    static constexpr uint64_t SAMPLE_INTERVAL_NS = 250000000ULL;

    /**
     * @brief Start the specified screensaver type
     *
     * Stops any currently active screensaver first. OFF stops and starts nothing.
     *
     * The saver starts at the level stored for it on this board and app version, or at level 0
     * (HELIX_SCREENSAVER_LEVEL forces a level and turns the gate off for the run). A saver
     * stored as too heavy shows a static black screen instead.
     *
     * While a saver runs, the active screen is hidden beneath its overlay
     * (helix::ScreenHideHold), and it stays hidden across a switch between types.
     * For the same span the display refreshes at the running saver's frame period,
     * following its level; level 0 runs at HELIX_SCREENSAVER_REFR_PERIOD_MS when one is
     * configured (helix::RefreshPeriodHold). The black screen does not take that hold.
     * A saver that fails to start leaves the manager inactive, and the screen and
     * refresh period as they were before any saver ran.
     */
    void start(ScreensaverType type);

    /**
     * @brief Registry savers this build can draw but has no instance for
     *
     * Which savers a colour depth can draw is stated in four places that agree only by
     * convention: SCREENSAVERS::depths, the Makefile's source filter, and this file's include
     * and registration guards. When they disagree the type is selectable and starts nothing,
     * which looks to a user like the screen simply never blanking. Empty in a correct build.
     */
    std::vector<const char*> savers_missing_for_build_depth() const;

    /** @brief Stop whatever screensaver or black screen is active */
    void stop();

    /** @brief True while a screensaver or its black screen is up */
    bool is_active() const;

    /** @brief Read configured screensaver type from DisplaySettingsManager, clamped */
    static ScreensaverType configured_type();

    /// Gives the gate the app's print state and display path.
    void set_host(helix::ui::SaverHost host);

    /**
     * @brief Samples the process CPU clock for the gate
     *
     * Call on the display manager's idle-check tick. While no saver runs, samples feed the idle
     * baseline. While one runs they close the gate's windows: over budget steps the saver down
     * one level and stores that level; over budget at the lowest level stores the board as too
     * heavy and replaces the saver with a static black screen.
     */
    void on_idle_check_tick();

  private:
    friend class helix::ScreensaverManagerTestAccess;

    struct StartPlan {
        size_t level = 0;
        bool too_heavy = false;
        bool gated = true;
    };

    ScreensaverManager();
    ~ScreensaverManager();

    /** @brief Find screensaver instance by type, or nullptr */
    helix::ui::SaverBase* find(ScreensaverType type) const;

    /// Level, too-heavy mark and gating a run of `saver` starts with. Records the board and
    /// version.
    StartPlan plan_start(const helix::ui::SaverBase& saver, const helix::ui::ScreensaverInfo& info);

    helix::ui::BoardFacts board_facts(const helix::PlatformCapabilities& caps) const;

    /// Starts measuring the saver that just started.
    void begin_gate(const StartPlan& plan);

    /// Stops the running saver or black screen without giving back the holds.
    void end_current();

    /// Shows the static black overlay in place of `type`.
    void show_black_screen(ScreensaverType type);

    /// Stores `level` and `too_heavy` for the running saver on this board and version.
    void record_level(size_t level, bool too_heavy);

    /** @brief Take the active-screen hide hold, once however many savers run in turn */
    void hold_screen();

    /** @brief Give back the hold taken by hold_screen(), if one is out */
    void release_screen();

    /** @brief Take the refresh period hold, once however many savers run in turn */
    void hold_refresh_period();

    /** @brief Give back the hold taken by hold_refresh_period(), if one is out */
    void release_refresh_period();

    std::vector<std::unique_ptr<helix::ui::SaverBase>> screensavers_;
    helix::ui::SaverBase* active_ = nullptr;
    const helix::ui::ScreensaverInfo* active_info_ = nullptr;
    helix::ui::SaverOverlay black_screen_;
    ScreensaverType black_screen_type_ = ScreensaverType::OFF;
    bool holds_screen_ = false;
    bool holds_refresh_period_ = false;

    helix::ui::SaverHost host_;
    helix::ui::CpuClockFn cpu_clock_;
    helix::ui::IdleBaseline baseline_;
    helix::ui::SaverGateSession session_;
    bool gate_enabled_ = false;
    std::optional<double> budget_override_;
    int cores_ = 0;
    size_t gated_level_ = 0;
    size_t requested_level_ = 0;
    uint64_t last_sample_ns_ = 0;
    bool sampled_ = false;
    std::string version_;
    std::string board_;
};

#endif // HELIX_ENABLE_SCREENSAVER
