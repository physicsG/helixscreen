// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "screensaver_frame.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * @file screensaver_sim_contract.h
 * @brief How a saver, or a test, drives a simulation: the one place the calling shape lives
 *
 * A sim draws a frame at a time into a FrameTarget:
 *
 *     void init(FrameTarget& frame, std::minstd_rand& rng, size_t level, Extra...);
 *     void step(uint32_t dt_ms, FrameTarget& frame, std::minstd_rand& rng, Extra...,
 *               std::vector<DirtyRect>& dirty);
 *     void request_level(size_t level);
 *     void clear();                 // releases every buffer, capacity included
 *     size_t heap_bytes() const;    // optional
 *     static constexpr const auto& LEVELS = X_LEVELS;  // each row has a period_ms
 *
 * A sim that draws no random numbers leaves the rng out of init() and step(). `Extra` is
 * what a sim reads each frame besides time, such as a clock reading. No LVGL.
 */

namespace helix::ui {

namespace sim_contract_detail {

template <typename Sim, typename Extras, typename = void>
struct init_takes_rng : std::false_type {};
template <typename Sim, typename... E>
struct init_takes_rng<Sim, std::tuple<E...>,
                      std::void_t<decltype(std::declval<Sim&>().init(
                          std::declval<FrameTarget&>(), std::declval<std::minstd_rand&>(),
                          std::declval<size_t>(), std::declval<E>()...))>> : std::true_type {};

template <typename Sim, typename Extras, typename = void> struct init_no_rng : std::false_type {};
template <typename Sim, typename... E>
struct init_no_rng<
    Sim, std::tuple<E...>,
    std::void_t<decltype(std::declval<Sim&>().init(std::declval<FrameTarget&>(),
                                                   std::declval<size_t>(), std::declval<E>()...))>>
    : std::true_type {};

template <typename Sim, typename Extras, typename = void>
struct step_takes_rng : std::false_type {};
template <typename Sim, typename... E>
struct step_takes_rng<
    Sim, std::tuple<E...>,
    std::void_t<decltype(std::declval<Sim&>().step(
        std::declval<uint32_t>(), std::declval<FrameTarget&>(), std::declval<std::minstd_rand&>(),
        std::declval<E>()..., std::declval<std::vector<DirtyRect>&>()))>> : std::true_type {};

template <typename Sim, typename Extras, typename = void> struct step_no_rng : std::false_type {};
template <typename Sim, typename... E>
struct step_no_rng<Sim, std::tuple<E...>,
                   std::void_t<decltype(std::declval<Sim&>().step(
                       std::declval<uint32_t>(), std::declval<FrameTarget&>(), std::declval<E>()...,
                       std::declval<std::vector<DirtyRect>&>()))>> : std::true_type {};

template <typename Sim, typename = void> struct has_heap_bytes : std::false_type {};
template <typename Sim>
struct has_heap_bytes<Sim, std::void_t<decltype(std::declval<const Sim&>().heap_bytes())>>
    : std::true_type {};

} // namespace sim_contract_detail

/// True when `Sim` initializes from the frame, (the rng,) the level and `Extra`.
template <typename Sim, typename... Extra>
inline constexpr bool sim_inits_with_v =
    sim_contract_detail::init_takes_rng<Sim, std::tuple<Extra...>>::value ||
    sim_contract_detail::init_no_rng<Sim, std::tuple<Extra...>>::value;

/// True when `Sim` steps on the frame, (the rng,) `Extra` and the dirty list.
template <typename Sim, typename... Extra>
inline constexpr bool sim_steps_with_v =
    sim_contract_detail::step_takes_rng<Sim, std::tuple<Extra...>>::value ||
    sim_contract_detail::step_no_rng<Sim, std::tuple<Extra...>>::value;

/// True when `Sim` reports the bytes it holds.
template <typename Sim>
inline constexpr bool sim_has_heap_bytes_v = sim_contract_detail::has_heap_bytes<Sim>::value;

/// `sim.init()` on `frame` at `level`, passing `rng` when the sim draws random numbers.
template <typename Sim, typename... Extra>
void init_sim(Sim& sim, FrameTarget& frame, std::minstd_rand& rng, size_t level, Extra&&... extra) {
    if constexpr (sim_contract_detail::init_takes_rng<Sim, std::tuple<Extra&&...>>::value) {
        sim.init(frame, rng, level, std::forward<Extra>(extra)...);
    } else {
        static_assert(sim_contract_detail::init_no_rng<Sim, std::tuple<Extra&&...>>::value,
                      "the sim's init() does not take (frame, [rng,] level, extra...)");
        sim.init(frame, level, std::forward<Extra>(extra)...);
    }
}

/// `sim.step()` by `dt_ms` into `frame`, passing `rng` when the sim draws random numbers.
template <typename Sim, typename... Extra>
void step_sim(Sim& sim, uint32_t dt_ms, FrameTarget& frame, std::minstd_rand& rng,
              std::vector<DirtyRect>& dirty, Extra&&... extra) {
    if constexpr (sim_contract_detail::step_takes_rng<Sim, std::tuple<Extra&&...>>::value) {
        sim.step(dt_ms, frame, rng, std::forward<Extra>(extra)..., dirty);
    } else {
        static_assert(sim_contract_detail::step_no_rng<Sim, std::tuple<Extra&&...>>::value,
                      "the sim's step() does not take (dt, frame, [rng,] extra..., dirty)");
        sim.step(dt_ms, frame, std::forward<Extra>(extra)..., dirty);
    }
}

/// Frame period, in ms, of rung `level` of a ladder table whose rows have a `period_ms`,
/// clamped to its last rung.
template <typename Levels> constexpr uint32_t ladder_period(const Levels& levels, size_t level) {
    const size_t last = std::size(levels) - 1;
    return levels[level < last ? level : last].period_ms;
}

} // namespace helix::ui
