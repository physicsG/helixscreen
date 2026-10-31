// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <cstdint>
#include <vector>

namespace helix::label {

// BLE UUIDs
inline constexpr const char* kMakeidServiceUuid = "0000abf0-0000-1000-8000-00805f9b34fb";
inline constexpr const char* kMakeidWriteCharUuid = "0000abf1-0000-1000-8000-00805f9b34fb";
inline constexpr const char* kMakeidNotifyCharUuid = "0000abf2-0000-1000-8000-00805f9b34fb";

// Command IDs
enum class MakeIdCmd : uint8_t {
    Handshake = 0x10,
    RfidWrite = 0x1A,
    PrintData = 0x1B,
};

// Handshake states
enum class MakeIdHandshakeState : uint8_t {
    Search = 0x00,
    Pause = 0x01,
    Restore = 0x02,
    Cancel = 0x03,
};

// Label type (bits 5-7 of print frame byte 4)
enum class MakeIdLabelType : uint8_t {
    Translucent = 0x00,
    BlackMark = 0x20,
    Transparent = 0x40,
    None = 0xE0,
};

// Response status
enum class MakeIdResponseStatus {
    Success,
    Wait,
    Resend,
    Error,
    Null,
    Pause,
    Exit,
};

// Parsed response
struct MakeIdResponse {
    MakeIdResponseStatus status = MakeIdResponseStatus::Resend;
    uint8_t error_code = 0;
    bool is_printing = false;
};

// Print frame parameters
struct MakeIdPrintParams {
    uint8_t darkness = 20; // 0-31 (bits 0-4), snoop: 0x14=20
    MakeIdLabelType label_type = MakeIdLabelType::BlackMark;
    uint8_t cut_type = 3;  // bits 0-2, snoop: 0x03
    uint8_t save_type = 0; // bits 3-4
    uint16_t total_copies = 1;
    uint16_t current_copy = 1;
    uint16_t width_px = 96;
    uint16_t chunk_height = 56;
    uint8_t remaining_chunks = 0;
};

// Bitmap chunk (encoded, possibly LZO-compressed)
struct MakeIdBitmapChunk {
    std::vector<uint8_t> data; // LZO-compressed 1bpp data
    int height = 0;            // rows in this chunk
};

// Print job config
struct MakeIdPrintJobConfig {
    uint8_t darkness = 20;
    MakeIdLabelType label_type = MakeIdLabelType::BlackMark;
    uint8_t cut_type = 3;
    int printer_width_bytes = 12;
    int max_rows_per_chunk = 170;
};

// Print job result
struct MakeIdPrintJob {
    std::vector<std::vector<uint8_t>> chunks; // complete 0x66 frames ready to send
    int total_rows = 0;
};

/// Compute MakeID checksum: negated sum of bytes
uint8_t makeid_checksum(const uint8_t* data, size_t len);

/// Build a handshake frame (0x10 command)
std::vector<uint8_t> makeid_build_handshake(MakeIdHandshakeState state);

/// Parse a 36-byte response from the printer
MakeIdResponse makeid_parse_response(const uint8_t* data, size_t len);

/// Build a print data frame (0x1B command) from pre-compressed bitmap data
std::vector<uint8_t> makeid_build_print_frame(const std::vector<uint8_t>& compressed_data,
                                              const MakeIdPrintParams& params);

/// Encode bitmap to raw 1bpp column-major format (no LZO, no chunking)
std::vector<uint8_t> makeid_encode_bitmap_raw(const LabelBitmap& bitmap, int printer_width_bytes);

/// Encode bitmap: column-major 1bpp + literals-only LZO + chunk splitting
/// If LZO is unavailable (BluetoothLoader not loaded), stores raw uncompressed data
std::vector<MakeIdBitmapChunk>
makeid_encode_bitmap(const LabelBitmap& bitmap, int printer_width_bytes, int max_rows_per_chunk);

/// Build complete print job (all 0x1B frames ready to send over BLE/RFCOMM)
MakeIdPrintJob makeid_build_print_job(const LabelBitmap& bitmap, const LabelSize& size,
                                      const MakeIdPrintJobConfig& config = {});

/// Label sizes for MakeID E1 (9/12/16mm tapes, 203 DPI)
std::vector<LabelSize> makeid_e1_sizes();

/// Default sizes (alias for E1 until we support more models)
std::vector<LabelSize> makeid_default_sizes();

} // namespace helix::label
