/* SPDX-License-Identifier: GPL-3.0-or-later
 * WASM stubs for HelixScreen hooks that patches/ bake into the LVGL submodule.
 * In the app these are provided by src/ (telemetry, app lifecycle); the browser
 * harness has neither, so they are no-ops. Extend as new patched hooks surface.
 */

#ifdef __cplusplus
extern "C" {
#endif

void helix_lvgl_anomaly(const char *code, const char *context) {
    (void)code;
    (void)context;
}

void helix_notify_app_backgrounded(void) {}
void helix_notify_app_foregrounded(void) {}

#ifdef __cplusplus
}
#endif
