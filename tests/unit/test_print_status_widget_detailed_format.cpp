// SPDX-License-Identifier: GPL-3.0-or-later

#include "../helix_test_fixture.h"
#include "../test_helpers/printer_state_test_access.h"
#include "../test_helpers/update_queue_test_access.h"
#include "app_globals.h"
#include "printer_state.h"
#include "src/ui/panel_widgets/print_status_widget.h"
#include "tool_state.h"

#include "../catch_amalgamated.hpp"

using namespace helix;
using namespace helix::ui;

// RAII helper that creates the DetailedFormatter singleton without a real attach().
// Destroys any pre-existing formatter first so the new one's observers bind to
// the current PrinterState subjects (tests in this file reset PrinterState in
// their setup; a formatter from a prior test would hold dangling observer
// pointers to the freed subjects).
struct FormatterScope {
    FormatterScope() {
        PrintStatusWidget::destroy_formatter_for_test();
        PrintStatusWidget::ensure_formatter_for_test();
    }
    ~FormatterScope() {
        PrintStatusWidget::release_formatter_for_test();
    }
};

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter writes layer and time",
                 "[print_status][formatter]") {
    // Tear down any inherited formatter BEFORE resetting PrinterState — otherwise
    // its observers point to subjects that are about to be deinit'd/reinit'd, and
    // FormatterScope's later destroy walks a recycled lv_subject_t (macOS SIGSEGV).
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    // Real slicer layer fields — no "~" estimate marker.
    PrinterPrintStateTestAccess::set_has_real_layer_data(
        PrinterStateTestAccess::get_print_state(ps), true);

    FormatterScope fs;

    lv_subject_set_int(ps.get_print_layer_current_subject(), 42);
    lv_subject_set_int(ps.get_print_layer_total_subject(), 213);
    lv_subject_set_int(ps.get_print_elapsed_subject(), 42 * 60);              // 0h 42m
    lv_subject_set_int(ps.get_print_time_left_subject(), 2 * 3600 + 14 * 60); // 2h 14m

    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_layer_text"))) == "Layer 42 / 213");
    // elapsed=42m, total=42m+2h14m=2h56m. Sub-hour durations carry no "0h" —
    // helix::format::duration_padded drops the hours field below one hour.
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_time_text"))) == "42m / 2h 56m");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter time text omits hours under an hour",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    FormatterScope fs;

    lv_subject_set_int(ps.get_print_elapsed_subject(), 45 * 60);
    lv_subject_set_int(ps.get_print_time_left_subject(), 0);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    // A hand-rolled "%dh %02dm" renders this as "0h 45m / 0h 45m".
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_time_text"))) == "45m / 45m");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter filament text switches unit at 1000mm",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    FormatterScope fs;

    // Below 1000mm the canonical formatter stays in millimetres. Dividing by
    // 1000 unconditionally renders this as "0.9m".
    lv_subject_set_int(ps.get_print_filament_used_subject(), 850);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_filament_text"))) == "Filament: 850mm");

    // Above 1000000mm it switches to kilometres rather than "1200.0m".
    lv_subject_set_int(ps.get_print_filament_used_subject(), 1200000);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_filament_text"))) == "Filament: 1.20km");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter seeds initial values on construction",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    PrinterPrintStateTestAccess::set_has_real_layer_data(
        PrinterStateTestAccess::get_print_state(ps), true);

    // Set subjects BEFORE creating formatter — seed calls in constructor pick them up
    lv_subject_set_int(ps.get_print_layer_current_subject(), 100);
    lv_subject_set_int(ps.get_print_layer_total_subject(), 200);

    FormatterScope fs;

    // No drain needed — seed calls are synchronous in the constructor
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_layer_text"))) == "Layer 100 / 200");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter layer text omits total when zero",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    PrinterPrintStateTestAccess::set_has_real_layer_data(
        PrinterStateTestAccess::get_print_state(ps), true);

    FormatterScope fs;

    lv_subject_set_int(ps.get_print_layer_current_subject(), 7);
    lv_subject_set_int(ps.get_print_layer_total_subject(), 0);

    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_layer_text"))) == "Layer 7");
}

// The panel's richest layer path (on_print_layer_changed) marks progress-derived
// layers with "~" and appends the commanded Z height. The widget renders the same
// subjects and must produce the same string.
TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter layer text marks estimates and shows Z",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    FormatterScope fs;

    // A freshly reset PrinterState has neither real layer data nor a Z-derived
    // layer, so layer_is_accurate() is false and the count is an estimate.
    REQUIRE(ps.layer_is_accurate() == false);

    lv_subject_set_int(ps.get_gcode_position_z_subject(), 2400); // 24.00mm
    lv_subject_set_int(ps.get_print_layer_current_subject(), 42);
    lv_subject_set_int(ps.get_print_layer_total_subject(), 213);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    REQUIRE(std::string(lv_subject_get_string(lv_xml_get_subject(
                nullptr, "print_status_layer_text"))) == "Layer ~42 / 213 (24.0mm)");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter filament text empty when zero",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    FormatterScope fs;

    lv_subject_set_int(ps.get_print_filament_used_subject(), 0);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_filament_text"))) == "");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter filament text formatted in meters",
                 "[print_status][formatter]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    FormatterScope fs;

    lv_subject_set_int(ps.get_print_filament_used_subject(), 2500); // 2.5m
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_filament_text"))) == "Filament: 2.5m");
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter nozzle text (decidegree rounding)",
                 "[print_status][formatter][temps]") {
    PrintStatusWidget::destroy_formatter_for_test();

    PrinterState& ps = get_printer_state();
    PrinterStateTestAccess::reset(ps);
    ps.init_subjects(false);

    FormatterScope fs;
    // Temp subjects store decidegrees (1 unit = 0.1°C; see L021 +
    // helix::units::to_decidegrees which multiplies by 10, not 100).
    lv_subject_set_int(ps.get_active_extruder_temp_subject(), 2157);   // 215.7°C → 216
    lv_subject_set_int(ps.get_active_extruder_target_subject(), 2200); // 220°C
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());

    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_nozzle_text"))) == "216 / 220°C");
    // bed_text / chamber_text are no longer formatted by the widget — the
    // XML's temp_display widgets bind directly to bed_temp / chamber_temp.
}

TEST_CASE_METHOD(HelixTestFixture, "DetailedFormatter multi-tool label and gate",
                 "[print_status][formatter][multi_tool]") {
    // Same hazard as the other tests, but for ToolState's tool_count subject:
    // tear down any inherited formatter before re-initing the subject it observes.
    PrintStatusWidget::destroy_formatter_for_test();

    ToolState::instance().init_subjects(false);

    FormatterScope fs;
    auto& ts = ToolState::instance();

    // Single tool — no label, gate=0
    lv_subject_set_int(ts.get_tool_count_subject(), 1);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());
    REQUIRE(lv_subject_get_int(lv_xml_get_subject(nullptr, "print_status_multi_tool")) == 0);
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_nozzle_tool_label"))) == "");

    // Two tools — gate=1, label tracks active (default index = 0)
    lv_subject_set_int(ts.get_tool_count_subject(), 2);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());
    REQUIRE(lv_subject_get_int(lv_xml_get_subject(nullptr, "print_status_multi_tool")) == 1);
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_nozzle_tool_label"))) == "T0");

    // Back to single — gate=0, label cleared
    lv_subject_set_int(ts.get_tool_count_subject(), 1);
    UpdateQueueTestAccess::drain_all(UpdateQueue::instance());
    REQUIRE(lv_subject_get_int(lv_xml_get_subject(nullptr, "print_status_multi_tool")) == 0);
    REQUIRE(std::string(lv_subject_get_string(
                lv_xml_get_subject(nullptr, "print_status_nozzle_tool_label"))) == "");
}
