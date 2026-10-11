// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "ui_modal.h"
#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/log_capture.h"
#include "../test_helpers/plugin_host_test_support.h"
#include "../test_helpers/plugin_test_support.h"
#include "../test_helpers/scope_exit.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_component.h"
#include "layout_manager.h"
#include "lua_bindings.h"
#include "lvgl_log_handler.h"
#include "plugin_overlay_host.h"
#include "xml_registration.h"

#include "../catch_amalgamated.hpp"

using namespace helix::plugin;
using namespace helix::plugin::test;

TEST_CASE_METHOD(LVGLTestFixture, "subjects register under the plugin prefix",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(
        count = helix.subject.int("count", 3)
        label = helix.subject.string("label", "idle")
    )"));
    lv_subject_t* c = lv_xml_get_subject(nullptr, "test-plugin__count");
    lv_subject_t* l = lv_xml_get_subject(nullptr, "test-plugin__label");
    REQUIRE(c);
    REQUIRE(l);
    CHECK(lv_subject_get_int(c) == 3);
    CHECK(std::string(lv_subject_get_string(l)) == "idle");

    REQUIRE(b.t.run(R"(count:set(7); label:set("busy"); got = count:get() .. label:get())"));
    CHECK(lv_subject_get_int(c) == 7);
    CHECK(std::string(lv_subject_get_string(l)) == "busy");
    CHECK(b.t.global("got") == "7busy");
}

TEST_CASE_METHOD(LVGLTestFixture, "observers see changes from C++ and Lua, not registration",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(
        seen = {}
        local s = helix.subject.int("n", 0)
        s:observe(function(v) seen[#seen + 1] = v end)
        s:set(1)
    )"));
    lv_subject_set_int(lv_xml_get_subject(nullptr, "test-plugin__n"), 2);
    REQUIRE(b.t.run("result = table.concat(seen, ',')"));
    CHECK(b.t.global("result") == "1,2");
}

TEST_CASE_METHOD(LVGLTestFixture, "an observer setting its own subject stops at the depth cap",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    b.t.run(R"(
        local s = helix.subject.int("loop", 0)
        s:observe(function(v) s:set(v + 1) end)
        s:set(1)
    )");
    CHECK(lv_subject_get_int(lv_xml_get_subject(nullptr, "test-plugin__loop")) <= 10);
}

TEST_CASE_METHOD(LVGLTestFixture, "subject names and sizes are validated",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    CHECK_FALSE(b.t.run(R"(helix.subject.int("", 0))"));
    CHECK_FALSE(b.t.run(R"(helix.subject.int("has space", 0))"));
    REQUIRE(b.t.run(R"(helix.subject.int("dup", 0))"));
    // The 3rd error within 60 s faults the runtime, so the size checks need a fresh one.
    CHECK_FALSE(b.t.run(R"(helix.subject.int("dup", 0))"));

    BoundRuntime c({&install_ui_bindings});
    CHECK_FALSE(c.t.run(R"(helix.subject.string("big", string.rep("x", 2000)))"));
    REQUIRE(c.t.run(R"(s = helix.subject.string("small", ""))"));
    CHECK_FALSE(c.t.run(R"(s:set(string.rep("x", 2000)))"));
}

TEST_CASE_METHOD(LVGLTestFixture, "subjects are unregistered when the runtime closes",
                 "[plugin][bindings][ui]") {
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(helix.subject.int("gone", 1))"));
        REQUIRE(lv_xml_get_subject(nullptr, "test-plugin__gone"));
    }
    CHECK(lv_xml_get_subject(nullptr, "test-plugin__gone") == nullptr);
}

TEST_CASE_METHOD(LVGLTestFixture, "creating a plugin subject logs no missing-subject warning",
                 "[plugin][lua_bindings_ui]") {
    helix::logging::register_lvgl_log_handler();
    BoundRuntime b({&install_ui_bindings});
    {
        helix::TextLogCapture cap;
        REQUIRE(b.t.run(R"(s = helix.subject.int("quiet", 5))"));
        CHECK_FALSE(cap.contains("No subject was found"));
    }
    CHECK(lv_subject_get_int(lv_xml_get_subject(nullptr, "test-plugin__quiet")) == 5);

    lv_subject_t app_subject;
    lv_subject_init_int(&app_subject, 5);
    helix::test::ScopeExit cleanup([&app_subject] {
        lv_xml_unregister_subject(nullptr, "test-plugin__taken");
        lv_subject_deinit(&app_subject);
    });
    lv_xml_register_subject(nullptr, "test-plugin__taken", &app_subject);
    CHECK_FALSE(b.t.run(R"(helix.subject.int("taken", 1))"));
}

TEST_CASE_METHOD(LVGLTestFixture, "closing the runtime logs no missing-subject warning",
                 "[plugin][lua_bindings_ui]") {
    helix::logging::register_lvgl_log_handler();
    helix::TextLogCapture cap;
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(helix.subject.int("x", 1))"));
        // Take the name away before close: the teardown probe must treat a name
        // that no longer resolves as normal, not warn about it.
        lv_xml_unregister_subject(nullptr, "test-plugin__x");
    }
    CHECK_FALSE(cap.contains("No subject was found"));
}

TEST_CASE_METHOD(LVGLTestFixture, "helix.subject bounds subjects per plugin",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(
        for i = 1, 128 do helix.subject.int("s" .. i, 0) end
        ok, err = pcall(helix.subject.int, "one-too-many", 0)
    )"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.t.global("err").find("at most 128 subjects") != std::string::npos);
}

TEST_CASE_METHOD(LVGLTestFixture, "subject observe bounds live observers per plugin",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(
        local s = helix.subject.int("n", 0)
        for i = 1, 256 do s:observe(function() end) end
        ok, err = pcall(s.observe, s, function() end)
    )"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.t.global("err").find("at most 256") != std::string::npos);
}

namespace {
/// Answers the open confirm dialog the way a user would; every close path releases the
/// plugin's one dialog slot.
void answer_open_confirm(LVGLTestFixture& fx, const char* target, bool backdrop) {
    fx.process_lvgl(50); // the dialog's creation is queued; pump before reading the stack
    helix::ui::UpdateQueue::instance().drain();
    lv_obj_t* dialog = ModalStack::instance().top_dialog();
    REQUIRE(dialog);
    lv_obj_t* clicked = backdrop ? ModalStack::instance().backdrop_for(dialog)
                                 : lv_obj_find_by_name(dialog, target);
    REQUIRE(clicked);
    lv_obj_send_event(clicked, LV_EVENT_CLICKED, nullptr);
    fx.process_lvgl(50);
    helix::ui::UpdateQueue::instance().drain();
}
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "helix.ui.confirm allows one open dialog per plugin",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(helix.ui.confirm("One", "body"))"));
    REQUIRE(b.t.run(R"(ok, err = pcall(helix.ui.confirm, "Two", "body"))"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.t.global("err").find("1 open dialog") != std::string::npos);

    // each close path releases the slot: primary button, cancel button, backdrop dismissal
    answer_open_confirm(*this, "btn_primary", false);
    REQUIRE(b.t.run(R"(helix.ui.confirm("Three", "body", { on_cancel = function() end }))"));
    answer_open_confirm(*this, "btn_secondary", false);
    REQUIRE(b.t.run(R"(helix.ui.confirm("Four", "body"))"));
    answer_open_confirm(*this, nullptr, true);
    REQUIRE(b.t.run(R"(helix.ui.confirm("Five", "body"))"));
    REQUIRE(ModalStack::instance().top_dialog());
}

TEST_CASE_METHOD(LVGLTestFixture, "runtime close leaves an app subject that took the name",
                 "[plugin][bindings][ui]") {
    lv_subject_t app_subject;
    lv_subject_init_int(&app_subject, 5);
    helix::test::ScopeExit cleanup([&app_subject] {
        if (lv_xml_get_subject(nullptr, "test-plugin__x") == &app_subject)
            lv_xml_unregister_subject(nullptr, "test-plugin__x");
        lv_subject_deinit(&app_subject);
    });
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(helix.subject.int("x", 1))"));
        lv_xml_register_subject(nullptr, "test-plugin__x", &app_subject);
    }
    CHECK(lv_xml_get_subject(nullptr, "test-plugin__x") == &app_subject);
}

TEST_CASE_METHOD(LVGLTestFixture, "ui.on handlers dispatch with an argument",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(
        calls = ""
        helix.ui.on("pick", function(arg) calls = calls .. tostring(arg) .. ";" end)
    )"));
    CHECK(dispatch_ui_handler(*b.t.rt, "pick", std::string("3")));
    CHECK(dispatch_ui_handler(*b.t.rt, "pick", std::nullopt));
    CHECK_FALSE(dispatch_ui_handler(*b.t.rt, "missing", std::nullopt));
    CHECK(b.t.global("calls") == "3;nil;");
}

TEST_CASE("plugin_event user_data parsing", "[plugin][bindings][ui]") {
    auto t = parse_plugin_event("orca-cal__start");
    CHECK(t.id == "orca-cal");
    CHECK(t.name == "start");
    CHECK_FALSE(t.arg);

    t = parse_plugin_event("orca-cal__pick:3:4");
    CHECK(t.name == "pick");
    CHECK(t.arg == std::optional<std::string>("3:4"));

    t = parse_plugin_event("orca-cal__my_handler");
    CHECK(t.name == "my_handler");

    CHECK(parse_plugin_event("noowner").id.empty());
    CHECK(parse_plugin_event("_x").id.empty());
    CHECK(parse_plugin_event("__x").id.empty());
    CHECK(parse_plugin_event("orca-cal_").id.empty());
    CHECK(parse_plugin_event("orca-cal_x").id.empty());
    CHECK(parse_plugin_event("Bad_x").id.empty());
    CHECK(parse_plugin_event("").id.empty());
    CHECK(parse_plugin_event(":x").id.empty());
}

TEST_CASE_METHOD(LVGLTestFixture, "a confirm dialog that cannot be shown holds no slot",
                 "[plugin][bindings][ui]") {
    // Without the modal_dialog component the dialog cannot be built. The component
    // loader would register it again on the dialog's first lookup, so it is off for
    // the case; both are restored afterwards for the tests that share this process.
    struct HideModalDialog {
        bool was_registered = false;
        HideModalDialog() {
            lv_xml_set_component_loader(nullptr);
            was_registered = lv_xml_component_get_scope("modal_dialog") != nullptr;
            if (was_registered)
                lv_xml_component_unregister("modal_dialog");
        }
        ~HideModalDialog() {
            if (was_registered) {
                std::string path =
                    "A:" + helix::LayoutManager::instance().resolve_xml_path("modal_dialog.xml");
                lv_xml_register_component_from_file(path.c_str());
            }
            helix::register_xml_on_first_use();
        }
    } hidden;
    BoundRuntime b({&install_ui_bindings}, {});
    REQUIRE(b.t.run(R"(helix.ui.confirm("One", "body"))"));
    REQUIRE(b.t.run(R"(helix.ui.confirm("Two", "body"))"));
}

namespace {
/// Closes a runtime while a bound label still observes its subject: the subject
/// is retired, kept alive by the helix-xml bind record on the returned widget.
/// Bind through helix-xml because its bind record keeps the observer handle and
/// detaches it on the widget's delete event; that record is the reference that
/// must outlive the runtime.
lv_obj_t* bind_label_to_doomed_subject() {
    lv_obj_t* root = nullptr;
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(s = helix.subject.string("status", "hi"))"));
        REQUIRE(lv_xml_register_component_from_data(
                    "test-plugin__bindrow",
                    "<component><view extends=\"lv_obj\">"
                    "<lv_label name=\"bound_label\" bind_text=\"test-plugin__status\"/>"
                    "</view></component>") == LV_RESULT_OK);
        const char* attrs[] = {nullptr};
        root = static_cast<lv_obj_t*>(
            lv_xml_create(lv_screen_active(), "test-plugin__bindrow", attrs));
        REQUIRE(root);
    }
    return root;
}
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "a bound object outlives its plugin's runtime safely",
                 "[plugin][lua_bindings_ui]") {
    sweep_retired_subjects();
    size_t before = retired_subject_count();
    lv_obj_t* root = bind_label_to_doomed_subject();
    helix::test::ScopeExit cleanup([&root] {
        if (root && lv_obj_is_valid(root))
            lv_obj_delete(root);
        lv_xml_component_unregister("test-plugin__bindrow");
    });
    lv_obj_t* label = lv_obj_find_by_name(root, "bound_label");
    REQUIRE(label);
    CHECK(lv_xml_get_subject(nullptr, "test-plugin__status") == nullptr);
    CHECK(retired_subject_count() == before + 1);
    CHECK(std::string(lv_label_get_text(label)) == "hi");

    lv_obj_delete(root); // detaches the bind record from a subject that must still be alive
    root = nullptr;
    sweep_retired_subjects();
    CHECK(retired_subject_count() == before);
}

TEST_CASE_METHOD(LVGLTestFixture, "a plugin load sweeps retired subjects",
                 "[plugin][lua_bindings_ui]") {
    sweep_retired_subjects();
    size_t before = retired_subject_count();
    lv_obj_t* root = bind_label_to_doomed_subject();
    helix::test::ScopeExit cleanup([&root] {
        if (root && lv_obj_is_valid(root))
            lv_obj_delete(root);
        lv_xml_component_unregister("test-plugin__bindrow");
    });
    REQUIRE(retired_subject_count() == before + 1);
    lv_obj_delete(root); // detaches the bind record: nothing observes the subject now
    root = nullptr;

    HostRig rig(enabled("hello", {"gcode"}));
    rig.host->load_from("tests/fixtures/plugins");
    drain();
    CHECK(retired_subject_count() == before);
}

TEST_CASE_METHOD(LVGLTestFixture, "an unobserved subject is freed at unload",
                 "[plugin][lua_bindings_ui]") {
    sweep_retired_subjects();
    size_t before = retired_subject_count();
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(s = helix.subject.int("n", 1); s:observe(function() end))"));
    }
    CHECK(retired_subject_count() == before);
}

TEST_CASE_METHOD(LVGLTestFixture, "toast and confirm validate their arguments",
                 "[plugin][bindings][ui]") {
    BoundRuntime b({&install_ui_bindings});
    REQUIRE(b.t.run(R"(
        helix.ui.toast("hello")
        helix.ui.toast("careful", "warning")
        helix.ui.confirm("Title", "Body", { severity = "warning", on_confirm = function() end })
    )"));
    CHECK_FALSE(b.t.run(R"(helix.ui.toast("x", "loud"))"));
    CHECK_FALSE(b.t.run(R"(helix.ui.confirm("t", "m", { severity = "loud" }))"));
}

namespace {
/// A PluginUi double recording opens and closes without touching navigation.
struct FakeUi {
    int opened = 0;
    int closed = 0;
    bool reject = false; ///< when set, open reports failure
    std::string last_component;
    PluginUi::Attrs last_attrs;

    PluginUi ui{
        [this](const std::string& component, std::function<void()>, const PluginUi::Attrs& attrs) {
            if (reject)
                return 0;
            ++opened;
            last_component = component;
            last_attrs = attrs;
            return 7;
        },
        [this](int handle) { closed += handle; }};
};
} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "helix.ui.overlay needs a host ui", "[plugin][lua_bindings_ui]") {
    BoundRuntime b({&install_ui_bindings}); // no ctx->ui: no host provides overlays here
    REQUIRE(b.t.run(R"(ok, err = pcall(helix.ui.overlay, "test-plugin__p"))"));
    CHECK(b.t.global("ok") == "false");
    CHECK(b.t.global("err").find("not available") != std::string::npos);
}

TEST_CASE_METHOD(LVGLTestFixture, "helix.ui.overlay checks ownership, attributes and the handle",
                 "[plugin][lua_bindings_ui]") {
    FakeUi fake;
    BoundRuntime b({&install_ui_bindings});
    b.ctx->ui = &fake.ui;
    REQUIRE(b.t.run(R"(
        ok1, e1 = pcall(helix.ui.overlay, "other__panel")
        ok2, e2 = pcall(helix.ui.overlay, "test-plugin__p", { bind_text = "other__s" })
        ok3, e3 = pcall(helix.ui.overlay, "test-plugin__p", { title = 5 })
        ok4, e4 = pcall(helix.ui.overlay, "test-plugin__p", { on_close = "no" })
        h = helix.ui.overlay("test-plugin__p", { title = "Hi", on_close = function() end })
        h:close()
    )"));
    CHECK(b.t.global("ok1") == "false"); // component not owned by this plugin
    CHECK(b.t.global("e1").find("not owned") != std::string::npos);
    CHECK(b.t.global("ok2") == "false"); // policy: bind_text must name an owned subject
    CHECK(b.t.global("e2").find("subject") != std::string::npos);
    CHECK(b.t.global("ok3") == "false"); // attribute values must be strings
    CHECK(b.t.global("ok4") == "false"); // on_close must be a function
    CHECK(fake.opened == 1);
    CHECK(fake.last_component == "test-plugin__p");
    REQUIRE(fake.last_attrs.size() == 1); // on_close is not an attribute
    CHECK(fake.last_attrs[0].first == "title");
    CHECK(fake.last_attrs[0].second == "Hi");
    CHECK(fake.closed == 7);

    fake.reject = true;
    REQUIRE(b.t.run(R"(ok5, e5 = pcall(helix.ui.overlay, "test-plugin__p"))"));
    CHECK(b.t.global("ok5") == "false");
    CHECK(b.t.global("e5").find("cannot open") != std::string::npos);
}

TEST_CASE_METHOD(LVGLUITestFixture, "closing the runtime hides the plugin's open confirm dialog",
                 "[plugin][lua_bindings_ui]") {
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(helix.ui.confirm("Q", "body"))"));
        process_lvgl(50); // the dialog's creation is queued
        helix::ui::UpdateQueue::instance().drain();
        REQUIRE(ModalStack::instance().top_dialog());
    } // the runtime closer hides the still-open dialog before the state goes
    process_lvgl(50);
    helix::ui::UpdateQueue::instance().drain();
    CHECK(ModalStack::instance().top_dialog() == nullptr);
}

TEST_CASE_METHOD(LVGLUITestFixture, "a confirm the user closed is not hidden again at close",
                 "[plugin][lua_bindings_ui]") {
    helix::TextLogCapture log;
    {
        BoundRuntime b({&install_ui_bindings});
        REQUIRE(b.t.run(R"(helix.ui.confirm("Q", "body"))"));
        answer_open_confirm(*this, "btn_secondary", false);
        REQUIRE(ModalStack::instance().top_dialog() == nullptr);
    } // the dialog's close path cleared the pointer, so the closer must find nothing to hide
    process_lvgl(50);
    helix::ui::UpdateQueue::instance().drain();
    CHECK_FALSE(log.contains("Dialog not found in stack"));
}

#endif // HELIX_HAS_PLUGINS
