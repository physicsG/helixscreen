// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "label_printer.h"

#include <string>

namespace helix::label {

/// Phomemo label printer backend over Bluetooth.
/// Supports both SPP (RFCOMM) and BLE (GATT) transports.
/// Uses BluetoothLoader for connectivity and phomemo_build_raster()
/// for protocol byte generation. Per-connection BT context (init on print,
/// deinit after). Async via detached thread + callback via queue_update().
class PhomemoBluetoothPrinter : public ILabelPrinter {
  public:
    /// Set the Bluetooth device and transport to print to
    void set_device(const std::string& mac, const std::string& transport = "ble");

    [[nodiscard]] std::string name() const override;
    void print(const LabelBitmap& bitmap, const LabelSize& size, PrintCallback callback) override;
    [[nodiscard]] std::vector<LabelSize> supported_sizes() const override;

  private:
    std::string mac_;
    std::string transport_ = "ble";
    static constexpr const char* kPhomemoWriteUuid = "0000ff02-0000-1000-8000-00805f9b34fb";
};

} // namespace helix::label
