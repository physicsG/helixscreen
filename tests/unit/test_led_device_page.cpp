// SPDX-License-Identifier: GPL-3.0-or-later

#include "led/led_color_utils.h"
#include "led/led_device_page.h"
#include "led/led_devices.h"

#include "../catch_amalgamated.hpp"

using namespace helix::led;

namespace {

LedStripInfo native(bool color, bool white) {
    LedStripInfo s;
    s.id = "neopixel x";
    s.backend = LedBackendType::NATIVE;
    s.supports_color = color;
    s.supports_white = white;
    return s;
}

LedStripInfo of(LedBackendType backend, bool pwm = false) {
    LedStripInfo s;
    s.id = "x";
    s.backend = backend;
    s.supports_color = false;
    s.supports_white = false;
    s.is_pwm = pwm;
    return s;
}

constexpr auto ANY = MacroLedType::TOGGLE; // ignored unless the device is a macro

} // namespace

TEST_CASE("page: Klipper LED, RGBW", "[led][page]") {
    CHECK(
        classify_device_page(native(true, true), ANY, true) ==
        DevicePage{LampControl::PowerAndBrightness, WhiteMode::WChannel, true, ListKind::Effects});
    CHECK(classify_device_page(native(true, true), ANY, false) ==
          DevicePage{LampControl::PowerAndBrightness, WhiteMode::WChannel, true, ListKind::None});
}

TEST_CASE("page: Klipper LED, RGB", "[led][page]") {
    CHECK(classify_device_page(native(true, false), ANY, true) ==
          DevicePage{LampControl::PowerAndBrightness, WhiteMode::Mixed, true, ListKind::Effects});
    CHECK(classify_device_page(native(true, false), ANY, false) ==
          DevicePage{LampControl::PowerAndBrightness, WhiteMode::Mixed, true, ListKind::None});
}

TEST_CASE("page: Klipper LED, single channel", "[led][page]") {
    CHECK(
        classify_device_page(native(false, false), ANY, false) ==
        DevicePage{LampControl::PowerAndBrightness, WhiteMode::None, false, ListKind::LevelChips});
    CHECK(classify_device_page(native(false, false), ANY, true) ==
          DevicePage{LampControl::PowerAndBrightness, WhiteMode::None, false, ListKind::Effects});
}

TEST_CASE("page: WLED", "[led][page]") {
    CHECK(classify_device_page(of(LedBackendType::WLED), ANY, true) ==
          DevicePage{LampControl::PowerAndBrightness, WhiteMode::None, false, ListKind::None});
}

TEST_CASE("page: output pin, PWM", "[led][page]") {
    CHECK(
        classify_device_page(of(LedBackendType::OUTPUT_PIN, true), ANY, false) ==
        DevicePage{LampControl::PowerAndBrightness, WhiteMode::None, false, ListKind::LevelChips});
}

TEST_CASE("page: output pin, on/off", "[led][page]") {
    CHECK(classify_device_page(of(LedBackendType::OUTPUT_PIN, false), ANY, false) ==
          DevicePage{LampControl::PowerOnly, WhiteMode::None, false, ListKind::None});
}

TEST_CASE("page: macro ON_OFF", "[led][page]") {
    CHECK(classify_device_page(of(LedBackendType::MACRO), MacroLedType::ON_OFF, false) ==
          DevicePage{LampControl::OnOffButtons, WhiteMode::None, false, ListKind::None});
}

TEST_CASE("page: macro TOGGLE", "[led][page]") {
    CHECK(classify_device_page(of(LedBackendType::MACRO), MacroLedType::TOGGLE, false) ==
          DevicePage{LampControl::ToggleButton, WhiteMode::None, false, ListKind::None});
}

TEST_CASE("page: macro PRESET", "[led][page]") {
    CHECK(classify_device_page(of(LedBackendType::MACRO), MacroLedType::PRESET, false) ==
          DevicePage{LampControl::None, WhiteMode::None, false, ListKind::Presets});
}

TEST_CASE("page: an effect is not a device", "[led][page]") {
    CHECK(classify_device_page(of(LedBackendType::LED_EFFECT), ANY, true) == DevicePage{});
}

TEST_CASE("white_tone: the W channel carries white on RGBW", "[led][page]") {
    const Rgbw n = white_tone(WhiteTone::Neutral, WhiteMode::WChannel);
    CHECK(n.w == Catch::Approx(1.0));
    CHECK(n.r + n.g + n.b == Catch::Approx(0.0));
    for (auto t : {WhiteTone::Cool, WhiteTone::Warm}) {
        CHECK(white_tone(t, WhiteMode::WChannel).w == Catch::Approx(1.0));
    }
}

TEST_CASE("white_tone: RGB strips mix white and never touch W", "[led][page]") {
    for (auto t : {WhiteTone::Cool, WhiteTone::Neutral, WhiteTone::Warm}) {
        CHECK(white_tone(t, WhiteMode::Mixed).w == Catch::Approx(0.0));
    }
}

TEST_CASE("white_tone: cool is bluer than warm", "[led][page]") {
    for (auto mode : {WhiteMode::WChannel, WhiteMode::Mixed}) {
        const Rgbw cool = white_tone(WhiteTone::Cool, mode);
        const Rgbw warm = white_tone(WhiteTone::Warm, mode);
        CHECK(cool.b > warm.b);
        CHECK(warm.r > cool.r);
    }
    CHECK(white_tone(WhiteTone::Cool, WhiteMode::None) == Rgbw{});
}

TEST_CASE("fit_look: a no-color strip gets brightness only, never an RGB fraction", "[led][page]") {
    // White-only (W channel): all of it on W.
    CHECK(fit_look(0xFF4444, 0.0, native(false, true)) == Look{0, 1.0});
    CHECK(fit_look(0, 1.0, native(false, true)) == Look{0, 1.0});
    // Single channel, no W: full on every RGB pin, so whichever one exists lights.
    CHECK(fit_look(0xFF4444, 0.0, native(false, false)) == Look{0xFFFFFF, 0.0});
    CHECK(fit_look(0, 1.0, native(false, false)) == Look{0xFFFFFF, 0.0});
}

TEST_CASE("fit_look: an RGB-only strip shows W as RGB white", "[led][page]") {
    CHECK(fit_look(0, 1.0, native(true, false)) == Look{0xFFFFFF, 0.0});
    // A tinted W folds into its tint.
    const Look warm = fit_look(0x593100, 1.0, native(true, false));
    CHECK(warm.w == 0.0);
    CHECK(warm.rgb == output_rgb(0x59 / 255.0, 0x31 / 255.0, 0.0, 1.0));
    CHECK(fit_look(0xFF4444, 0.0, native(true, false)) == Look{0xFF4444, 0.0});
}

TEST_CASE("fit_look: an RGBW strip keeps the look; black becomes white", "[led][page]") {
    CHECK(fit_look(0xFF4444, 0.0, native(true, true)) == Look{0xFF4444, 0.0});
    CHECK(fit_look(0x000040, 1.0, native(true, true)) == Look{0x000040, 1.0});
    CHECK(fit_look(0, 0.0, native(true, true)) == Look{0xFFFFFF, 0.0});
}

TEST_CASE("output_rgb: W adds to every channel, scaled to full", "[led][page]") {
    CHECK(output_rgb(0.0, 0.0, 0.0, 1.0) == 0xFFFFFFu);
    CHECK(output_rgb(1.0, 0.0, 0.0, 0.0) == 0xFF0000u);
    CHECK(output_rgb(0.5, 0.0, 0.0, 0.0) == 0xFF0000u);
    CHECK(output_rgb(0.0, 0.0, 0.0, 0.0) == 0xFFFFFFu);
    CHECK(output_rgb(0.0, 0.0, 0.25, 1.0) == 0xCCCCFFu);
}

namespace {
const std::vector<uint32_t> PRESETS(std::begin(helix::led::DEFAULT_COLOR_PRESETS),
                                    std::end(helix::led::DEFAULT_COLOR_PRESETS));
}

TEST_CASE("ring_for_look: every preset rings itself, whatever its brightest channel",
          "[led][page]") {
    for (size_t i = 0; i < PRESETS.size(); ++i) {
        INFO("preset " << i);
        const LookRing ring = ring_for_look(PRESETS[i], 0.0, WhiteMode::WChannel, PRESETS);
        CHECK(ring.swatch == static_cast<int>(i));
        CHECK(ring.white == -1);
    }
}

TEST_CASE("ring_for_look: any white rings a White tone, never Custom", "[led][page]") {
    // RGB strip: pure white is Neutral, warm and cool mixes their tones.
    CHECK(ring_for_look(0xFFFFFF, 0.0, WhiteMode::Mixed, PRESETS).white ==
          static_cast<int>(WhiteTone::Neutral));
    CHECK(ring_for_look(0xFFC080, 0.0, WhiteMode::Mixed, PRESETS).white ==
          static_cast<int>(WhiteTone::Warm));
    CHECK(ring_for_look(0xC8DCFF, 0.0, WhiteMode::Mixed, PRESETS).white ==
          static_cast<int>(WhiteTone::Cool));
    // RGBW: W alone is Neutral; a tint under W leans the way of its tint.
    CHECK(ring_for_look(0, 1.0, WhiteMode::WChannel, PRESETS).white ==
          static_cast<int>(WhiteTone::Neutral));
    CHECK(ring_for_look(0xFFFFFF, 0.0, WhiteMode::WChannel, PRESETS).white ==
          static_cast<int>(WhiteTone::Neutral));
    for (int t = 0; t < 3; ++t) {
        for (WhiteMode mode : {WhiteMode::WChannel, WhiteMode::Mixed}) {
            INFO("tone " << t << " mode " << static_cast<int>(mode));
            const Rgbw c = white_tone(static_cast<WhiteTone>(t), mode);
            const LookRing ring =
                ring_for_look(pack_rgb(c.r, c.g, c.b), c.w, WhiteMode::WChannel, PRESETS);
            CHECK(ring.white == t);
            CHECK(ring.swatch == -1);
        }
    }
}

TEST_CASE("ring_for_look: a color that is neither rings Custom", "[led][page]") {
    const LookRing ring = ring_for_look(0x804000, 0.0, WhiteMode::WChannel, PRESETS);
    CHECK(ring.white == -1);
    CHECK(ring.swatch == -2);
}
