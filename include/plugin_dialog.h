// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace helix::plugin {

/// How a plugin surface ended. Exactly one is reported per request.
enum class CloseHow {
    User,      ///< the user dismissed it (backdrop tap, back/ESC)
    Plugin,    ///< the plugin closed it after it appeared
    App,       ///< the app closed it (remote sweep, hot reload)
    Cancelled, ///< withdrawn before it ever appeared
};

/// The Lua spelling: "user", "plugin", "app", "cancelled".
const char* close_how_name(CloseHow how);

/// What a modal close reason means to the plugin that owns the dialog.
CloseHow close_how_for(ModalCloseReason reason);

/**
 * @brief One of a plugin's own components shown as a dialog
 *
 * A real ModalStack entry, tagged with the plugin id, so the card, backdrop,
 * animation and dismissal match every app dialog. The stack owns the instance
 * once presented and frees it when the entry goes, so callers keep the dialog
 * object and reach the instance through the static helpers, which find nothing
 * once it is gone. Main thread only.
 */
class PluginDialog : public Modal {
  public:
    PluginDialog(std::string plugin_id, std::string component,
                 std::vector<std::pair<std::string, std::string>> attrs,
                 std::function<void(CloseHow)> on_closed);

    const char* get_name() const override;
    const char* component_name() const override;

    /// Shows @p d on the active screen and hands it to the stack. The dialog
    /// object, or nullptr when the component could not be created.
    static lv_obj_t* present(std::unique_ptr<PluginDialog> d);

    /// Closes @p dialog as the plugin's own close. False when it is not a live
    /// plugin dialog.
    static bool close_by_plugin(lv_obj_t* dialog);

    /// Drops @p dialog's close callback without running it (the plugin is
    /// unloading). The dialog itself stays until closed.
    static void forget(lv_obj_t* dialog);

  protected:
    void on_hide() override;

  private:
    static PluginDialog* find(lv_obj_t* dialog);

    std::string plugin_id_;
    std::string component_;
    std::vector<std::pair<std::string, std::string>> attrs_;
    std::function<void(CloseHow)> on_closed_;
};

} // namespace helix::plugin
