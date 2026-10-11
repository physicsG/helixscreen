// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace helix {

/**
 * @brief Brother QL label printer backend
 *
 * Implements the Brother QL raster protocol over TCP (port 9100).
 * Supports QL-800, QL-810W, QL-820NWB and similar models.
 *
 * Thread safety: print_label() runs async on a detached thread. Callbacks
 * are dispatched to the UI thread via queue_update().
 */
class BrotherQLPrinter : public ILabelPrinter {
  public:
    BrotherQLPrinter();
    BrotherQLPrinter(std::string host, int port);
    ~BrotherQLPrinter() override;

    BrotherQLPrinter(const BrotherQLPrinter&) = delete;
    BrotherQLPrinter& operator=(const BrotherQLPrinter&) = delete;

    // === ILabelPrinter interface ===

    [[nodiscard]] std::string name() const override;
    void print(const LabelBitmap& bitmap, const LabelSize& size, PrintCallback callback) override;
    [[nodiscard]] std::vector<LabelSize> supported_sizes() const override;

    // === Brother QL-specific API ===

    /// Print a label bitmap to a specific host:port. Connects, sends, disconnects.
    /// Callback fires on UI thread via queue_update().
    void print_label(const std::string& host, int port, const LabelBitmap& bitmap,
                     const LabelSize& size, PrintCallback callback);

    /// Get supported label sizes for Brother QL printers (static access)
    static std::vector<LabelSize> supported_sizes_static();

    /// Build the raw raster command buffer (public for testing)
    static std::vector<uint8_t> build_raster_commands(const LabelBitmap& bitmap,
                                                      const LabelSize& size);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string host_;
    int port_ = 9100;
};

} // namespace helix
