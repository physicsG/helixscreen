// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if HELIX_HAS_PLUGINS

#include "overlay_base.h"
#include "plugin_manifest.h"

#include <cstdint>
#include <list>
#include <string>
#include <utility>
#include <vector>

namespace helix::plugin {

/// The setting_*_row component and its attributes for one declaration, given the
/// current value. Pure, so the mapping is testable without LVGL.
struct SettingRowSpec {
    std::string component;                                  ///< e.g. "setting_toggle_row"
    std::vector<std::pair<std::string, std::string>> attrs; ///< in order
};

SettingRowSpec setting_row_spec(const std::string& plugin_id, const SettingDecl& d,
                                const json& current);

/// The settings screen generated from one plugin's manifest: one setting_*_row per
/// declaration, wired to PluginHost::set_setting. Owned by PluginHost while open.
class PluginSettingsOverlay : public OverlayBase {
  public:
    /// One row's payload, pointed at by the row root's user data. Kept in a list
    /// so the addresses the widgets hold stay valid as rows are added.
    struct RowBinding {
        std::string key;
        SettingDecl decl;
        json stored; ///< the last value the plugin's rule accepted
        lv_obj_t* row = nullptr;
    };

    PluginSettingsOverlay(std::string plugin_id, const Manifest& manifest, const json& settings,
                          uint64_t load_gen);
    ~PluginSettingsOverlay() override;

    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "PluginSettings";
    }

    const std::string& plugin_id() const {
        return plugin_id_;
    }
    /// The host's load generation this screen was built against; a plugin reloaded
    /// under the same id gets a new one, and the screen's rows stop routing to it.
    uint64_t load_gen() const {
        return load_gen_;
    }
    lv_obj_t* root() const {
        return overlay_root_;
    }
    /// The row root carrying `key`'s binding, or null.
    lv_obj_t* row_for(const std::string& key) const;

    /// The binding whose address is `ud`, or null when it is not one of ours.
    RowBinding* binding_at(const void* ud);
    /// Reads the row widget's current value and writes it through the plugin's
    /// rule; on refusal restores the row from `b.stored`. No-op when the value
    /// already equals `b.stored` (the text row reports ready and then defocused).
    void on_row_changed(RowBinding& b, lv_obj_t* target);
    /// Runs the action row's declared callback through the plugin event path.
    void on_row_action(const RowBinding& b);
    /// The navigation close callback's body: tears the widget tree down. The host
    /// destroys the object after this returns.
    void on_nav_closed();

  private:
    /// Creates the row for one declaration into `rows`, or returns false when the
    /// component cannot be created or its plugin-supplied attributes fail policy.
    bool build_row(lv_obj_t* rows, const SettingDecl& d, const json& current);
    /// Pushes `b.stored` back into the row's widget. Setting widget state can fire
    /// the row's own value_changed, so the binding must already be reachable.
    void apply_row_state(const RowBinding& b);

    std::list<RowBinding> bindings_;
    std::string plugin_id_;
    std::string title_;
    std::vector<SettingDecl> settings_decls_; ///< copied: the plugin's context can die first
    uint64_t load_gen_ = 0;
    json settings_; ///< snapshot the rows were built from
};

/// Registers the plugin_setting_changed / plugin_setting_action XML callbacks.
/// Called from register_plugin_event_callback().
void register_plugin_settings_callbacks();

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
