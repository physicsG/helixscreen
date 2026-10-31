# SPDX-License-Identifier: GPL-3.0-or-later

"""reset returns the app to a known state so one instance can serve many tests."""


def test_reset_from_a_nested_overlay_lands_on_home(helix_app):
    helix_app.navigate("controls")
    helix_app.click("btn_motion")
    helix_app.wait_idle()
    assert helix_app.current()["overlays"], "setup failed — no overlay pushed"

    result = helix_app.reset()
    assert result["panel"] == "home"
    assert result["overlays_popped"] >= 1

    state = helix_app.current()
    assert state["panel"] == "home"
    assert state["overlays"] == []


def test_reset_is_idempotent(helix_app):
    helix_app.reset()
    second = helix_app.reset()
    assert second["panel"] == "home"
    assert second["overlays_popped"] == 0


def test_reset_dismisses_a_modal(helix_app):
    # Modal::hide()'s widget deletion is deferred behind a real ~150ms exit
    # fade (kModalExitDurationMs) when animations are on — wait_idle()
    # deliberately does not count animations (see handle_wait_idle), so an
    # `ls` taken right after reset() would race that fade. freeze() disables
    # animations, which routes Modal::hide() through its no-animation branch
    # (synchronous stack removal, deletion deferred only to the next LVGL
    # tick) so the widget-gone check below is deterministic rather than
    # depending on how much wall-clock time two subprocess round trips
    # happened to burn.
    helix_app.freeze()
    try:
        helix_app.ctl("demo", "runout-modal")
        helix_app.wait_idle()

        widgets_before = {w["name"] for w in helix_app.ls()["widgets"] if w.get("name")}
        assert any("runout_guidance_modal" in n for n in widgets_before), \
            "setup failed — modal never appeared"

        result = helix_app.reset()
        # A modal is tracked in ModalStack, not NavigationManager's overlay
        # stack (RunoutGuidanceModal is a Modal, never pushed as an overlay)
        # — checking `overlays` here would pass even if the modal were never
        # touched. Assert on the count reset() itself reports, plus a
        # widget-level check that the dialog is actually gone rather than
        # just marked "exiting".
        assert result["modals_cleared"] >= 1

        widgets_after = {w["name"] for w in helix_app.ls()["widgets"] if w.get("name")}
        assert not any("runout_guidance_modal" in n for n in widgets_after), \
            "modal widget still present after reset"

        # A lingering modal swallows input while every later command still
        # reports success — the exact failure the design doc warns about.
        # Confirm the screen is actually interactive again, not just
        # visually clear.
        helix_app.navigate("settings")
        assert helix_app.current()["panel"] == "settings"
    finally:
        helix_app.unfreeze()
