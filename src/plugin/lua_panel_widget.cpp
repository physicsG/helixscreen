// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "lua_panel_widget.h"

#include "grid_layout.h"
#include "plugin_host.h"

namespace helix::plugin {

LuaPanelWidget::LuaPanelWidget(std::string plugin_id, std::string widget_id, std::string component,
                               LifetimeToken runtime)
    : plugin_id_(std::move(plugin_id)), widget_id_(std::move(widget_id)),
      component_(std::move(component)), runtime_(std::move(runtime)) {}

void LuaPanelWidget::run(WidgetHook hook, const LuaRuntime::PushFn& args) {
    if (runtime_.expired())
        return;
    PluginHost* host = PluginHost::live();
    LuaRuntime* rt = host ? host->runtime(plugin_id_) : nullptr;
    if (rt)
        dispatch_widget_hook(*rt, widget_id_, hook, args);
}

void LuaPanelWidget::attach(lv_obj_t*, lv_obj_t*) {
    run(WidgetHook::Attach);
}

// Runs only while the tile tree is alive; the base drops root() when it dies.
void LuaPanelWidget::detach() {
    if (root())
        run(WidgetHook::Detach);
}

LuaPanelWidget::~LuaPanelWidget() {
    detach_tile();
}

void LuaPanelWidget::on_size_changed(int colspan, int rowspan, int width_px, int height_px) {
    // The grid speaks tracks; a plugin hears cells. A whole cell arrives as a
    // Lua integer and only a half-cell span as a float (1.5).
    constexpr int kTracks = helix::GridLayout::TRACKS_PER_CELL;
    auto push_cells = [](lua_State* co, int tracks) {
        if (tracks % kTracks == 0)
            lua_pushinteger(co, tracks / kTracks);
        else
            lua_pushnumber(co, static_cast<lua_Number>(tracks) / kTracks);
    };
    run(WidgetHook::Size, [=](lua_State* co) {
        push_cells(co, colspan);
        push_cells(co, rowspan);
        lua_pushinteger(co, width_px);
        lua_pushinteger(co, height_px);
        return 4;
    });
}

void LuaPanelWidget::on_activate() {
    run(WidgetHook::Activate);
}

void LuaPanelWidget::on_deactivate() {
    run(WidgetHook::Deactivate);
}

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
