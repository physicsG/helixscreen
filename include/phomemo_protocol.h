// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <cstdint>
#include <vector>

namespace helix::label {

/// Default print speed for Phomemo M110
static constexpr uint8_t kPhomemoDefaultSpeed = 0x03;

/// Default print density for Phomemo M110
static constexpr uint8_t kPhomemoDefaultDensity = 0x0A;

/// Build Phomemo ESC/POS raster protocol bytes from a bitmap.
/// Pure function — no I/O. Output can be sent over USB, BLE GATT, or any transport.
std::vector<uint8_t> phomemo_build_raster(const LabelBitmap& bitmap, const LabelSize& size);

} // namespace helix::label
