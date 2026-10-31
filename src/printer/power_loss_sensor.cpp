// SPDX-License-Identifier: GPL-3.0-or-later

#include "power_loss_sensor.h"

#include "hardware_object_table.h"
#include "printer_discovery.h"

namespace helix::power_loss {

std::vector<std::string> required_status_objects(const PrinterDiscovery& hw) {
    return hw.objects_of({helix::hw::ObjectKind::PowerLossMonitor});
}

} // namespace helix::power_loss
