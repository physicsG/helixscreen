// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/**
 * @file firmware_error_envelope.h
 * @brief Firmware error text that wraps its message in a JSON object.
 *
 * Some firmwares report an error as a JSON object carrying a code and the
 * human message, e.g. `{"code":"key849","msg":"..."}`, either as the whole
 * text or spliced into Klipper prose (`Internal error during connect:
 * !{...}`, or a shutdown reason followed by Klipper's "Once the underlying
 * issue is corrected..." footer). Shown verbatim, the user reads raw JSON.
 *
 * This is the one decoder for that shape. Which key names a firmware uses is
 * a row in the envelope table in the .cpp; callers never name a firmware.
 * Pure, exception-free and free of LVGL, so the ESP32 network component can
 * compile it too.
 */
namespace helix {

/// What an envelope's code can be used for beyond showing it.
enum class FirmwareCodeScheme : std::uint8_t {
    /// A `keyNNN` code indexed by KlipperErrorTable: curated wording and
    /// severity rules key off it.
    ErrorTable,
    /// An identifier for the user and the logs only; nothing here keys off it.
    Opaque,
};

/// One decoded envelope and the text around it.
struct FirmwareErrorEnvelope {
    /// Text before the object, with trailing whitespace and Klipper's `!`
    /// emit marker trimmed. Empty when the object opens the text.
    std::string prefix;
    /// The firmware's human message. Empty only for an error-table code,
    /// whose wording the table supplies.
    std::string msg;
    std::string code;
    FirmwareCodeScheme scheme = FirmwareCodeScheme::Opaque;
    /// The envelope's `values` member as JSON text, empty when it has none.
    /// Kept serialized so this header stays light enough for moonraker_error.h.
    std::string values_json;
    /// Text after the object, verbatim. Empty when it is only whitespace.
    std::string suffix;

    /// The original text with the object replaced by `body`.
    [[nodiscard]] std::string splice(const std::string& body) const;
    /// The original text with the object replaced by its message.
    [[nodiscard]] std::string text() const {
        return splice(msg);
    }
};

/// The first envelope in `text`: a JSON object with a string code under one
/// of the known shapes, and a string message unless the shape's code is an
/// error-table one. nullopt when there is none, including for a malformed or
/// unbalanced object and for a JSON object of any other shape.
[[nodiscard]] std::optional<FirmwareErrorEnvelope>
find_firmware_error_envelope(std::string_view text);

/// `text` with its first envelope replaced by the envelope's message, or
/// `text` unchanged when it carries none or the envelope has no message.
[[nodiscard]] std::string decode_firmware_error_text(const std::string& text);

} // namespace helix
