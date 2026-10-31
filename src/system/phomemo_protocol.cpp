// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_LABEL_PRINTER

#include "phomemo_protocol.h"

namespace helix::label {

std::vector<uint8_t> phomemo_build_raster(const LabelBitmap& bitmap, const LabelSize& size) {
    int bytes_per_line = (bitmap.width() + 7) / 8;
    int num_lines = bitmap.height();

    std::vector<uint8_t> cmd;
    cmd.reserve(19 + static_cast<size_t>(bytes_per_line) * num_lines + 8);

    // === Header (11 bytes) ===

    // Print speed: ESC N 0D <speed>
    cmd.push_back(0x1B);
    cmd.push_back(0x4E);
    cmd.push_back(0x0D);
    cmd.push_back(kPhomemoDefaultSpeed);

    // Print density: ESC N 04 <density>
    cmd.push_back(0x1B);
    cmd.push_back(0x4E);
    cmd.push_back(0x04);
    cmd.push_back(kPhomemoDefaultDensity);

    // Media type: 1F 11 <type>
    cmd.push_back(0x1F);
    cmd.push_back(0x11);
    cmd.push_back(size.media_type);

    // === Image: GS v 0 raster block ===

    // GS v 0 command
    cmd.push_back(0x1D);
    cmd.push_back(0x76);
    cmd.push_back(0x30);
    cmd.push_back(0x00); // normal mode

    // bytes_per_line (16-bit LE)
    cmd.push_back(static_cast<uint8_t>(bytes_per_line & 0xFF));
    cmd.push_back(static_cast<uint8_t>((bytes_per_line >> 8) & 0xFF));

    // num_lines (16-bit LE)
    cmd.push_back(static_cast<uint8_t>(num_lines & 0xFF));
    cmd.push_back(static_cast<uint8_t>((num_lines >> 8) & 0xFF));

    // Raster data
    for (int y = 0; y < num_lines; y++) {
        const uint8_t* row = bitmap.row_data(y);
        cmd.insert(cmd.end(), row, row + bytes_per_line);
    }

    // === Footer (8 bytes) ===

    // Finalize: 1F F0 05 00
    cmd.push_back(0x1F);
    cmd.push_back(0xF0);
    cmd.push_back(0x05);
    cmd.push_back(0x00);

    // Feed to gap: 1F F0 03 00
    cmd.push_back(0x1F);
    cmd.push_back(0xF0);
    cmd.push_back(0x03);
    cmd.push_back(0x00);

    return cmd;
}

} // namespace helix::label

#endif // HELIX_HAS_LABEL_PRINTER
