// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lua_bindings.h"
#include "panel_widget.h"

#include <string>

namespace helix::plugin {

/// A home widget declared by a plugin. Forwards the grid's lifecycle calls to the
/// plugin's helix.widget hooks while the runtime the widget was built against is
/// alive, and does nothing once it is not: home skips rebuilds while grid edit is
/// active or a widget overlay is open, so an instance can outlive its runtime, and
/// a same-id reload must leave the old instance inert rather than speaking for the
/// new runtime. The token, taken from that runtime at factory time, is what makes
/// the check safe: a live host can hand out a NEW runtime under the same plugin id.
class LuaPanelWidget : public helix::PanelWidget {
  public:
    LuaPanelWidget(std::string plugin_id, std::string widget_id, std::string component,
                   LifetimeToken runtime);
    ~LuaPanelWidget() override;
    const char* id() const override {
        return widget_id_.c_str();
    }
    std::string get_component_name() const override {
        return component_;
    }
    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_activate() override;
    void on_deactivate() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    bool supports_reuse() const override {
        return false;
    }

  private:
    void run(WidgetHook hook, const LuaRuntime::PushFn& args = {});
    std::string plugin_id_;
    std::string widget_id_;
    std::string component_;
    LifetimeToken runtime_;
};

} // namespace helix::plugin
