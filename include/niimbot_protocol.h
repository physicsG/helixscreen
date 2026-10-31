// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <cstdint>
#include <vector>

namespace helix::label {

/// Niimbot BLE packet command IDs (client -> printer)
enum class NiimbotCmd : uint8_t {
    PrintStart = 0x01,
    PageStart = 0x03,
    SetPageSize = 0x13,
    SetQuantity = 0x15,
    RfidInfo = 0x1A,
    PrintClear = 0x20,
    SetDensity = 0x21,
    SetLabelType = 0x23,
    PrinterInfo = 0x40,
    PrintBitmapRowIndexed = 0x83,
    PrintEmptyRow = 0x84,
    PrintBitmapRow = 0x85,
    PrinterCheckLine = 0x86,
    PrintStatus = 0xA3,
    Connect = 0xC1,
    Heartbeat = 0xDC,
    PageEnd = 0xE3,
    PrintEnd = 0xF3,
};

/// Niimbot BLE packet response IDs (printer -> client)
enum class NiimbotResp : uint8_t {
    In_PrintStart = 0x02,
    In_PageStart = 0x04,
    In_SetPageSize = 0x14,
    In_SetQuantity = 0x16,
    In_RfidInfo = 0x1B,
    In_PrintClear = 0x30,
    In_SetDensity = 0x31,
    In_SetLabelType = 0x33,
    In_PrintBitmapRow = 0x85,
    In_PrintStatus = 0xB3,
    In_Connect = 0xC2,
    In_HeartbeatAdvanced1 = 0xDD,
    In_HeartbeatBasic = 0xDE,
    In_PrintError = 0xDB,
    In_PageEnd = 0xE4,
    In_PrintEnd = 0xF4,
};

/// Niimbot label type values
enum class NiimbotLabelType : uint8_t {
    WithGaps = 1,
    BlackMark = 2,
    Continuous = 3,
    Transparent = 5,
};

/// Niimbot BLE service UUID (Transparent UART / Microchip ISSC)
inline constexpr const char* kNiimbotServiceUuid = "e7810a71-73ae-499d-8c15-faa9aef0c3f2";

/// Niimbot BLE write characteristic UUID (within the transparent UART service)
inline constexpr const char* kNiimbotWriteCharUuid = "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f";

/// Build a single Niimbot protocol packet: [0x55 0x55 CMD LEN DATA... CHECKSUM 0xAA 0xAA]
std::vector<uint8_t> niimbot_build_packet(NiimbotCmd cmd, const uint8_t* data, size_t len);

/// Convenience overload for vector data
inline std::vector<uint8_t> niimbot_build_packet(NiimbotCmd cmd, const std::vector<uint8_t>& data) {
    return niimbot_build_packet(cmd, data.data(), data.size());
}

/// Convenience overload for single-byte payload
inline std::vector<uint8_t> niimbot_build_packet(NiimbotCmd cmd, uint8_t byte) {
    return niimbot_build_packet(cmd, &byte, 1);
}

/// Build the complete sequence of BLE packets for a Niimbot print job.
/// Returns a vector of individual packets to be sent sequentially over BLE.
/// The caller is responsible for sending each packet and handling inter-packet delays.
struct NiimbotPrintJob {
    std::vector<std::vector<uint8_t>> packets;
    int total_rows = 0;
};

/// Build a print job from a bitmap for Niimbot printers.
/// @param bitmap     1-bit monochrome bitmap
/// @param size       Label dimensions
/// @param density    Print density (1-5, default 3)
/// @param label_type Label type (default: WithGaps)
NiimbotPrintJob niimbot_build_print_job(const LabelBitmap& bitmap, const LabelSize& size,
                                        uint8_t density = 3,
                                        NiimbotLabelType label_type = NiimbotLabelType::WithGaps);

/// Supported label sizes for Niimbot B21 (384px wide, 203 DPI)
std::vector<LabelSize> niimbot_b21_sizes();

/// Supported label sizes for Niimbot D11/D110 (96px wide, 203 DPI)
std::vector<LabelSize> niimbot_d11_sizes();

/// Select sizes based on device name (D11/D110 → narrow, else B21 wide)
std::vector<LabelSize> niimbot_sizes_for_model(const std::string& device_name);

} // namespace helix::label
