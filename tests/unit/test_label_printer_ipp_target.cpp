// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_label_printer_ipp_target.cpp
 * @brief The IPP resource path from the mDNS TXT "rp" field through the label-printer
 *        settings to the IppPrinter target (prestonbrown/helixscreen#1822).
 *
 * A CUPS share serves its queue at e.g. /printers/Canon_TS8350, not ipp/print, and
 * advertises that path in "rp". A printer typed in by address has no "rp" and uses
 * the ipp/print fallback.
 */

#include "ui_settings_label_printer.h"

#include "../lvgl_test_fixture.h"
#include "config.h"
#include "ipp_printer.h"
#include "label_printer_settings.h"
#include "label_printer_utils.h"
#include "mdns_discovery.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

DiscoveredPrinter discovered(const std::string& ip, uint16_t port, const std::string& rp) {
    DiscoveredPrinter p;
    p.name = "Canon TS8300 series";
    p.hostname = "nas.local";
    p.ip_address = ip;
    p.port = port;
    p.resource_path = rp;
    return p;
}

/// Leaves the shared Config the way the other label tests expect to find it.
struct LabelSettingsScope {
    LabelSettingsScope() {
        Config::get_instance();
        LabelPrinterSettingsManager::instance().init_subjects();
    }
    ~LabelSettingsScope() {
        auto& s = LabelPrinterSettingsManager::instance();
        s.set_printer_address("");
        s.set_ipp_resource_path("");
        s.set_printer_port(9100);
        s.set_printer_protocol("raw");
        s.deinit_subjects();
    }
};

} // namespace

namespace helix::settings {

class LabelPrinterSettingsOverlayTestAccess {
  public:
    static void add_discovered(LabelPrinterSettingsOverlay& overlay,
                               const DiscoveredPrinter& printer, const std::string& protocol) {
        overlay.discovered_network_printers_.push_back({printer, protocol, 100});
    }
};

} // namespace helix::settings

TEST_CASE("A complete mDNS record keeps its TXT rp on the discovered printer", "[label][ipp_rp]") {
    MdnsTxtFields txt;
    txt.model_name = "Canon TS8300 series";
    txt.resource_path = "printers/Canon_TS8350";

    auto printer =
        make_discovered_printer("Canon TS8300 series", "nas.local", "192.168.2.3", 631, txt);

    REQUIRE(printer.name == "Canon TS8300 series");
    REQUIRE(printer.hostname == "nas.local");
    REQUIRE(printer.ip_address == "192.168.2.3");
    REQUIRE(printer.port == 631);
    REQUIRE(printer.resource_path == "printers/Canon_TS8350");
}

TEST_CASE_METHOD(LVGLTestFixture, "Selecting a discovered IPP printer in settings stores its rp",
                 "[label][ipp_rp]") {
    LabelSettingsScope scope;
    settings::LabelPrinterSettingsOverlay overlay;
    settings::LabelPrinterSettingsOverlayTestAccess::add_discovered(
        overlay, discovered("192.168.2.3", 631, "printers/Canon_TS8350"), "ipp");

    overlay.handle_printer_selected(0);

    auto& settings = LabelPrinterSettingsManager::instance();
    REQUIRE(settings.get_printer_address() == "192.168.2.3");
    REQUIRE(settings.get_printer_protocol() == "ipp");
    REQUIRE(settings.get_ipp_resource_path() == "printers/Canon_TS8350");
}

TEST_CASE("mDNS TXT rp is kept as the resource path", "[label][ipp_rp]") {
    MdnsTxtFields fields;
    apply_mdns_txt_entry(fields, "rp", "printers/Canon_TS8350");
    apply_mdns_txt_entry(fields, "ty", "Canon TS8300 series");
    apply_mdns_txt_entry(fields, "product", "(Canon TS8300)");

    REQUIRE(fields.resource_path == "printers/Canon_TS8350");
    REQUIRE(fields.model_name == "Canon TS8300 series");
    REQUIRE(fields.product == "(Canon TS8300)");

    SECTION("an empty value does not overwrite a known one") {
        apply_mdns_txt_entry(fields, "rp", "");
        REQUIRE(fields.resource_path == "printers/Canon_TS8350");
    }
}

TEST_CASE_METHOD(LVGLTestFixture, "Discovered rp flows into the settings and the IPP target",
                 "[label][ipp_rp]") {
    LabelSettingsScope scope;
    auto& settings = LabelPrinterSettingsManager::instance();

    save_discovered_network_printer(discovered("192.168.2.3", 631, "printers/Canon_TS8350"), "ipp");

    REQUIRE(settings.get_printer_address() == "192.168.2.3");
    REQUIRE(settings.get_printer_port() == 631);
    REQUIRE(settings.get_printer_protocol() == "ipp");
    REQUIRE(settings.get_ipp_resource_path() == "printers/Canon_TS8350");

    IppPrinter printer;
    apply_ipp_target(printer);
    REQUIRE(printer.resource_path() == "printers/Canon_TS8350");
}

TEST_CASE_METHOD(LVGLTestFixture, "IPP target falls back to ipp/print without an rp",
                 "[label][ipp_rp]") {
    LabelSettingsScope scope;
    auto& settings = LabelPrinterSettingsManager::instance();

    SECTION("a discovered printer that advertises no rp, on a zero SRV port") {
        save_discovered_network_printer(discovered("192.168.2.15", 0, ""), "ipp");
        REQUIRE(settings.get_printer_port() == 631);
        REQUIRE(settings.get_ipp_resource_path().empty());
    }

    SECTION("a printer typed in by address after a CUPS share was selected") {
        save_discovered_network_printer(discovered("192.168.2.3", 631, "printers/Canon_TS8350"),
                                        "ipp");
        settings.set_printer_address("192.168.2.15");
        REQUIRE(settings.get_ipp_resource_path().empty());
    }

    IppPrinter printer;
    apply_ipp_target(printer);
    REQUIRE(printer.resource_path() == "ipp/print");
}

TEST_CASE_METHOD(LVGLTestFixture, "Re-saving the same address keeps its rp", "[label][ipp_rp]") {
    LabelSettingsScope scope;
    auto& settings = LabelPrinterSettingsManager::instance();

    save_discovered_network_printer(discovered("192.168.2.3", 631, "printers/Canon_TS8350"), "ipp");
    settings.set_printer_address("192.168.2.3");

    REQUIRE(settings.get_ipp_resource_path() == "printers/Canon_TS8350");
}

TEST_CASE_METHOD(LVGLTestFixture, "A leading slash in rp is normalized at the IPP target",
                 "[label][ipp_rp]") {
    LabelSettingsScope scope;

    save_discovered_network_printer(discovered("192.168.2.3", 631, "/printers/Canon_TS8350"),
                                    "ipp");

    IppPrinter printer;
    apply_ipp_target(printer);
    REQUIRE(printer.resource_path() == "printers/Canon_TS8350");
}
