// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_pointer_frame_hook.cpp
 * @brief Every pointer sample reaches LVGL in the frame its rotation step expects
 *
 * A touch panel reports where on the panel it was touched. A relative pointer's
 * driver accumulates motion into a position on the picture the user moves it
 * across. These read each kind through lv_indev_read(), so LVGL's own rotation
 * step runs on the sample and the point asserted is the one widgets receive.
 */

#include "../lvgl_test_fixture.h"
#include "../mock_input_tree.h"
#include "indev_delete_watch.h"
#include "pointer_frame_hook.h"
#include "touch_calibration_wrapper.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::pointer_transform_for;
using helix::PointerFrameHook;
using helix::PointerTransform;
using helix::input::PointerKind;
using helix::test::caps_string;
using helix::test::MockInputTree;
using helix::test::mouse_key_caps;

namespace {

/// Where the touch driver says the panel was touched, in the panel's own frame.
constexpr lv_point_t kTouchOnPanel{100, 50};
/// Where the mouse driver has accumulated its motion to.
constexpr lv_point_t kMousePosition{300, 200};

void touch_driver_read(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = kTouchOnPanel;
    data->state = LV_INDEV_STATE_PRESSED;
}

void mouse_driver_read(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = kMousePosition;
    data->state = LV_INDEV_STATE_RELEASED;
}

/// A mouse far down a portrait picture, past the unrotated display's height.
constexpr lv_point_t kMouseDownPortrait{400, 700};

/// The point an evdev mouse's driver reports for kMouseDownPortrait: bounded by
/// the unrotated display.
void portrait_mouse_driver_read(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    data->point = {kMouseDownPortrait.x, kTestDisplayHeight - 1};
    data->state = LV_INDEV_STATE_RELEASED;
}

int g_evdev_raw_reads = 0;

/// Stands in for lv_evdev_get_last_raw(): an evdev mouse's position before its
/// driver's bound.
bool fake_evdev_last_raw(lv_indev_t* /*indev*/, int* x, int* y) {
    ++g_evdev_raw_reads;
    *x = kMouseDownPortrait.x;
    *y = kMouseDownPortrait.y;
    return true;
}

/// The Pi 3B's ft5x06 on event2 and Logitech M705 on event5, as sysfs describes them.
void add_pi3b_pointers(MockInputTree& tree) {
    tree.add_device(
        2, "generic ft5x06 (79)",
        {{"abs", caps_string({0, 1, 47, 53, 54, 57})}, {"rel", "0"}, {"key", caps_string({330})}},
        "0018");
    tree.add_device(5, "Logitech M705", {{"abs", "0"}, {"rel", "1943"}, {"key", mouse_key_caps()}});
}

class PointerFrameHookFixture : public LVGLTestFixture {
  public:
    PointerFrameHookFixture() {
        disp = lv_display_get_default();
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    }
    ~PointerFrameHookFixture() override {
        hook.restore_all();
        for (lv_indev_t* indev : created) {
            lv_indev_delete(indev);
        }
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_0);
    }

    lv_indev_t* make_pointer(lv_indev_read_cb_t driver_read) {
        lv_indev_t* indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, driver_read);
        created.push_back(indev);
        return indev;
    }

    /// Deletes @p indev the way a driver deletes its own device, leaving the hook
    /// installed on it.
    void delete_pointer(lv_indev_t* indev) {
        created.erase(std::remove(created.begin(), created.end(), indev), created.end());
        lv_indev_delete(indev);
    }

    /// One read through LVGL's own pipeline, returning the point widgets see.
    static lv_point_t read(lv_indev_t* indev) {
        lv_indev_read(indev);
        lv_point_t point{};
        lv_indev_get_point(indev, &point);
        return point;
    }

    static constexpr int32_t kPanelW = kTestDisplayWidth;
    static constexpr int32_t kPanelH = kTestDisplayHeight;
    lv_display_t* disp = nullptr;
    PointerFrameHook hook;
    std::vector<lv_indev_t*> created;
};

} // namespace

TEST_CASE("A sample's transform follows the kind of device it came from",
          "[display][indev][rotation]") {
    SECTION("under a scanout plane only a touch panel turns") {
        CHECK(pointer_transform_for(PointerKind::PanelAbsolute, 180, 0) == PointerTransform::Plane);
        CHECK(pointer_transform_for(PointerKind::Relative, 180, 0) == PointerTransform::None);
    }

    SECTION("under LVGL's rotation only a relative pointer is handed back") {
        for (int degrees : {90, 180, 270}) {
            INFO("LVGL at " << degrees);
            CHECK(pointer_transform_for(PointerKind::PanelAbsolute, 0, degrees) ==
                  PointerTransform::None);
            CHECK(pointer_transform_for(PointerKind::Relative, 0, degrees) ==
                  PointerTransform::UndoLvglRotation);
        }
    }

    SECTION("with nothing rotated nothing turns here") {
        CHECK(pointer_transform_for(PointerKind::PanelAbsolute, 0, 0) == PointerTransform::None);
        CHECK(pointer_transform_for(PointerKind::Relative, 0, 0) == PointerTransform::None);
    }
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "Under plane rotation a mouse keeps its position and touch turns with the picture",
                 "[display][indev][rotation]") {
    REQUIRE(disp != nullptr);
    REQUIRE(lv_display_get_horizontal_resolution(disp) == kPanelW);
    REQUIRE(lv_display_get_vertical_resolution(disp) == kPanelH);
    // The plane path leaves LVGL's own rotation at zero.
    REQUIRE(lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_0);
    hook.set_plane_rotation(180, kPanelW, kPanelH);

    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute));
    REQUIRE(hook.install(mouse, PointerKind::Relative));

    const lv_point_t touched = read(touch);
    CHECK(touched.x == kPanelW - 1 - kTouchOnPanel.x);
    CHECK(touched.y == kPanelH - 1 - kTouchOnPanel.y);

    const lv_point_t pointed = read(mouse);
    CHECK(pointed.x == kMousePosition.x);
    CHECK(pointed.y == kMousePosition.y);
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "Under LVGL rotation a mouse keeps its position and touch turns with the picture",
                 "[display][indev][rotation]") {
    REQUIRE(disp != nullptr);
    hook.set_plane_rotation(0, kPanelW, kPanelH);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_180);

    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute));
    REQUIRE(hook.install(mouse, PointerKind::Relative));

    // LVGL's own rotation step turns the touch sample.
    const lv_point_t touched = read(touch);
    CHECK(touched.x == kPanelW - 1 - kTouchOnPanel.x);
    CHECK(touched.y == kPanelH - 1 - kTouchOnPanel.y);

    const lv_point_t pointed = read(mouse);
    CHECK(pointed.x == kMousePosition.x);
    CHECK(pointed.y == kMousePosition.y);
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "Under an LVGL quarter turn a mouse reaches the whole picture",
                 "[display][indev][rotation]") {
    REQUIRE(disp != nullptr);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    // The position is on the picture but past the unrotated display's height,
    // where the driver's own point stops.
    REQUIRE(kMouseDownPortrait.x < lv_display_get_horizontal_resolution(disp));
    REQUIRE(kMouseDownPortrait.y < lv_display_get_vertical_resolution(disp));
    REQUIRE(kMouseDownPortrait.y >= kPanelH);

    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(portrait_mouse_driver_read);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute));
    REQUIRE(hook.install(mouse, PointerKind::Relative, [](int& x, int& y) {
        x = kMouseDownPortrait.x;
        y = kMouseDownPortrait.y;
        return true;
    }));

    const lv_point_t touched = read(touch);
    CHECK(touched.x == kPanelH - 1 - kTouchOnPanel.y);
    CHECK(touched.y == kTouchOnPanel.x);

    const lv_point_t pointed = read(mouse);
    CHECK(pointed.x == kMouseDownPortrait.x);
    CHECK(pointed.y == kMouseDownPortrait.y);
}

TEST_CASE_METHOD(PointerFrameHookFixture, "A mouse pushed past the picture's edge stops at it",
                 "[display][indev][rotation]") {
    REQUIRE(disp != nullptr);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
    const int32_t picture_h = lv_display_get_vertical_resolution(disp);

    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    REQUIRE(hook.install(mouse, PointerKind::Relative, [](int& x, int& y) {
        x = -20;
        y = 5000;
        return true;
    }));

    const lv_point_t pointed = read(mouse);
    CHECK(pointed.x == 0);
    CHECK(pointed.y == picture_h - 1);
}

TEST_CASE_METHOD(PointerFrameHookFixture, "An unrotated display passes every sample through",
                 "[display][indev][rotation]") {
    REQUIRE(disp != nullptr);
    hook.set_plane_rotation(0, kPanelW, kPanelH);

    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute));
    REQUIRE(hook.install(mouse, PointerKind::Relative));

    const lv_point_t touched = read(touch);
    CHECK(touched.x == kTouchOnPanel.x);
    CHECK(touched.y == kTouchOnPanel.y);

    const lv_point_t pointed = read(mouse);
    CHECK(pointed.x == kMousePosition.x);
    CHECK(pointed.y == kMousePosition.y);
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "A device LVGL deletes is forgotten before the hook restores",
                 "[display][indev][rotation]") {
    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    const uint32_t touch_events = lv_indev_get_event_count(touch);
    const uint32_t mouse_events = lv_indev_get_event_count(mouse);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute));
    REQUIRE(hook.install(mouse, PointerKind::Relative));
    REQUIRE(hook.fronts(touch));
    REQUIRE(hook.fronts(mouse));
    // The hook listens for each device's deletion.
    CHECK(lv_indev_get_event_count(touch) == touch_events + 1);
    CHECK(lv_indev_get_event_count(mouse) == mouse_events + 1);

    // lv_evdev deletes its own device when a read fails, as it does on unplug.
    delete_pointer(mouse);
    CHECK_FALSE(hook.fronts(mouse));
    CHECK(hook.fronts(touch));

    // Only the device still alive is read and handed back, and it keeps no
    // listener behind.
    hook.restore_all();
    CHECK(lv_indev_get_read_cb(touch) == touch_driver_read);
    CHECK(lv_indev_get_event_count(touch) == touch_events);
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "A backend's own pointer to a deleted device is cleared, not left dangling",
                 "[display][indev][calibration]") {
    // Stands in for a backend's touch_/pointer_ member: the calibration wrapper
    // sits beneath the frame hook, exactly as DisplayBackendDRM and
    // DisplayBackendFbdev install both on create_input_pointer().
    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* owner = touch;
    // A second device with its own slot, standing in for a backend's mouse_
    // member: proves forget() matches the deleted device, not every slot it
    // knows about.
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    lv_indev_t* mouse_owner = mouse;

    helix::CalibrationContext ctx;
    helix::TouchCalibration cal;
    helix::install_calibration_wrapper(touch, ctx, cal, kPanelW, kPanelH);
    REQUIRE(hook.install(touch, PointerKind::PanelAbsolute, {}, &owner));
    REQUIRE(hook.install(mouse, PointerKind::Relative, {}, &mouse_owner));
    REQUIRE(owner == touch);
    REQUIRE(mouse_owner == mouse);

    // lv_evdev deletes its own device when a read fails, as it does on unplug.
    delete_pointer(touch);

    // The owner's own pointer is cleared the moment LVGL deletes the device,
    // not left pointing at freed memory until a destructor runs later. A
    // REQUIRE here, not a CHECK: a regression must stop before the next line
    // reads through a dangling owner instead of merely reporting one.
    REQUIRE(owner == nullptr);
    // The other device's slot is untouched - forget() matched the device that
    // was actually deleted, not every slot the hook knows about.
    REQUIRE(mouse_owner == mouse);

    // A backend destructor reaches its member through uninstall_calibration_wrapper()
    // before calibration_context_ is destroyed. With owner cleared this call
    // never touches the freed indev.
    helix::uninstall_calibration_wrapper(owner, ctx);
}

TEST_CASE_METHOD(
    PointerFrameHookFixture,
    "A slot repointed to a new device before the old one is deleted keeps the new device",
    "[display][indev]") {
    lv_indev_t* first = make_pointer(mouse_driver_read);
    lv_indev_t* second = make_pointer(mouse_driver_read);

    helix::IndevDeleteWatch watch;
    lv_indev_t* slot = first;
    watch.watch(first, &slot);

    // The slot moves to a freshly created device before the old one's own
    // delete event arrives.
    slot = second;

    // lv_evdev deletes its own device when a read fails, as it does on unplug.
    delete_pointer(first);

    // The slot names a live device, not the one that was just deleted, so it
    // is left alone rather than nulled out from under that live device.
    CHECK(slot == second);
}

TEST_CASE_METHOD(PointerFrameHookFixture, "A hook that goes away stops listening to its devices",
                 "[display][indev][rotation]") {
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    const uint32_t events = lv_indev_get_event_count(mouse);
    {
        PointerFrameHook scoped;
        REQUIRE(scoped.install(mouse, PointerKind::Relative));
        REQUIRE(lv_indev_get_event_count(mouse) == events + 1);
    }
    CHECK(lv_indev_get_event_count(mouse) == events);
    CHECK(lv_indev_get_read_cb(mouse) == mouse_driver_read);
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "A device hooked by its path takes its kind from its capabilities",
                 "[display][indev][rotation]") {
    MockInputTree tree("pointer_frame_hook_kind");
    add_pi3b_pointers(tree);
    hook.set_plane_rotation(180, kPanelW, kPanelH);

    lv_indev_t* touch = make_pointer(touch_driver_read);
    lv_indev_t* mouse = make_pointer(mouse_driver_read);
    CHECK(hook.install(touch, tree.dev_dir + "/event2", true, nullptr, tree.sysfs_dir,
                       fake_evdev_last_raw) == PointerKind::PanelAbsolute);
    CHECK(hook.install(mouse, tree.dev_dir + "/event5", true, nullptr, tree.sysfs_dir,
                       fake_evdev_last_raw) == PointerKind::Relative);
    REQUIRE(hook.fronts(touch));
    REQUIRE(hook.fronts(mouse));

    const lv_point_t touched = read(touch);
    CHECK(touched.x == kPanelW - 1 - kTouchOnPanel.x);
    CHECK(touched.y == kPanelH - 1 - kTouchOnPanel.y);

    const lv_point_t pointed = read(mouse);
    CHECK(pointed.x == kMousePosition.x);
    CHECK(pointed.y == kMousePosition.y);
}

TEST_CASE_METHOD(PointerFrameHookFixture,
                 "Only a device evdev opened has its position read before the driver's bound",
                 "[display][indev][rotation]") {
    MockInputTree tree("pointer_frame_hook_evdev");
    add_pi3b_pointers(tree);
    REQUIRE(disp != nullptr);
    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    const std::string mouse_path = tree.dev_dir + "/event5";

    lv_indev_t* evdev_mouse = make_pointer(portrait_mouse_driver_read);
    lv_indev_t* other_mouse = make_pointer(portrait_mouse_driver_read);
    REQUIRE(hook.install(evdev_mouse, mouse_path, true, nullptr, tree.sysfs_dir,
                         fake_evdev_last_raw) == PointerKind::Relative);
    REQUIRE(hook.install(other_mouse, mouse_path, false, nullptr, tree.sysfs_dir,
                         fake_evdev_last_raw) == PointerKind::Relative);

    g_evdev_raw_reads = 0;
    const lv_point_t pointed = read(evdev_mouse);
    CHECK(g_evdev_raw_reads > 0);
    CHECK(pointed.x == kMouseDownPortrait.x);
    CHECK(pointed.y == kMouseDownPortrait.y);

    // Another driver's data is never read as lv_evdev's, so its own bounded
    // point is what LVGL gets.
    g_evdev_raw_reads = 0;
    const lv_point_t bounded = read(other_mouse);
    CHECK(g_evdev_raw_reads == 0);
    CHECK(bounded.x == kMouseDownPortrait.x);
    CHECK(bounded.y == kPanelH - 1);
}
