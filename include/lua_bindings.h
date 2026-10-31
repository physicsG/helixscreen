// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lua_runtime.h"
#include "plugin_backend.h"
#include "plugin_manifest.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace helix::plugin {

struct PluginUi;

/// What every binding of one plugin may reach. The owner keeps it alive until after the
/// runtime is destroyed, because runtime closers may read it.
struct PluginContext {
    LuaRuntime& rt;
    PluginBackend& backend;
    const Manifest& manifest;
    json* settings; ///< this plugin's /plugins/settings/<id> object
    std::function<void()> save_settings;
    std::string storage_path; ///< <dir of settings.json>/plugin-data/<id>.json
    /// Overlay access; filled by PluginHost, null where no host provides it.
    PluginUi* ui = nullptr;
    /// Set while on_unload runs: the plugin is leaving (at shutdown navigation is
    /// already down), so it may not open overlays or dialogs.
    bool unloading = false;
    /// Called after a setting changed through set_plugin_setting, before its on_change
    /// handlers run; the host uses it to refresh an open generated settings screen.
    std::function<void(const std::string& key, const json& value)> setting_changed;
};

using Installer = void (*)(PluginContext&);

/// Subject observers and printer watches share one quota (the ui and printer bindings
/// both draw on it; the count lives on the runtime).
constexpr size_t kMaxObserverWatches = 256;

/// helix.log, helix.json, helix.timer, helix.sleep. Must run first: it registers the
/// context that context() returns.
void install_core_bindings(PluginContext& ctx);
void install_ui_bindings(PluginContext& ctx);
void install_printer_bindings(PluginContext& ctx);
void install_moonraker_bindings(PluginContext& ctx);
void install_io_bindings(PluginContext& ctx);
/// helix.widget. BoundRuntime users may pass it in `installers`.
void install_widget_bindings(PluginContext& ctx);
/// helix.canvas. BoundRuntime users may pass it in `installers`.
void install_canvas_bindings(PluginContext& ctx);
/// helix.sound. BoundRuntime users may pass it in `installers`.
void install_sound_bindings(PluginContext& ctx);
/// helix.ui.table. Runs after install_ui_bindings; BoundRuntime users may pass it in
/// `installers`.
void install_table_bindings(PluginContext& ctx);

/// The shortest gap between two helix.sound cues of one plugin at UI priority, and at
/// event priority, which holds the speaker against every UI sound. A cue inside its gap
/// is dropped.
constexpr uint32_t kPluginUiCueIntervalMs = 150;
constexpr uint32_t kPluginEventCueIntervalMs = 1000;

PluginContext& context(lua_State* L);

/// Room left before the runtime's post-push check would fault the plugin. Download and
/// http fetches ask for one byte more than this, so a body that fills the ask is refused
/// by the cap check instead of landing whole in the Lua state.
size_t memory_remaining(const LuaRuntime& rt);

/// Charges `bytes` of binding-held data (canvas lists, table rows) to the plugin's memory
/// budget, or raises "<call>: plugin memory budget exceeded" with nothing charged. A little
/// room stays uncharged so the error itself can still be raised.
void charge_plugin_memory(lua_State* L, size_t bytes, const char* call);

/// luaL_newmetatable for a binding's userdata type, with `__metatable` set so plugin
/// code sees a string from getmetatable and cannot edit the table (a `__gc` added to it
/// would be armed on the next userdata C creates, outside every time budget). Leaves
/// the metatable on the stack. Every binding metatable is made through this.
void new_locked_metatable(lua_State* L, const char* name);

/// Raises "<call> is not allowed in a <check label>" while the runtime runs a check
/// (LuaRuntime::call_sync). The first statement of every binding with a side effect;
/// async bindings are refused by await_async.
void forbid_in_check(lua_State* L, const char* call);

/// Raises "<call> needs the '<permission>' permission in manifest.json" unless granted.
void require_permission(lua_State* L, Permission p, const char* call);

/// Pushes the success value(s) of an RpcResult and returns how many were pushed.
using PushRpc = std::function<int(lua_State*, const RpcResult&)>;

/// An RpcCallback that resumes `p` with on_ok's value(s), or with (nil, error) on failure.
RpcCallback make_resolver(LuaRuntime::Pending p, PushRpc on_ok);

/// Pushes `true`.
int push_rpc_true(lua_State* co, const RpcResult&);
/// Pushes the result's JSON value.
int push_rpc_value(lua_State* co, const RpcResult&);
/// Pushes `value`, or (nil, "response larger than the plugin memory cap") when a response
/// body of `body_bytes` would not fit between the runtime's memory use and its cap. A body
/// that lands whole in the Lua state and overshoots would fault the runtime; refusing keeps
/// it a normal (nil, error) return. Returns how many were pushed.
int push_rpc_capped_body(lua_State* co, const json& value, size_t body_bytes);

/// How one row of the stable printer table converts its subject into a Lua value.
/// KlippyState is the klippy_state subject's int as Klipper's own state name.
enum class PrinterValueKind { Bool, String, Int, DeciDegrees, KlippyState };

/// One Lua-facing printer name and the XML subject backing it.
struct PrinterField {
    const char* lua_name;
    const char* subject;
    PrinterValueKind kind;
};

/// The stable table from the spec's "Printer state" section.
const std::vector<PrinterField>& printer_fields();

/// Objects and arrays become tables; null becomes nil. Never raises: nesting deeper
/// than 64 levels becomes nil.
void push_json(lua_State* L, const json& j);

/// Raises a Lua error for functions, userdata, cycles, non-string object keys and nesting
/// deeper than 32. An empty table converts to an empty array.
json to_json(lua_State* L, int index);

/// Stores `value` in the plugin's settings, saves, and runs its on_change handlers. False,
/// with nothing stored, when `key` is undeclared or `value` does not fit its declaration.
bool set_plugin_setting(PluginContext& ctx, const std::string& key, const json& value);
/// The value to show for `d`: the stored value when it still fits its declaration,
/// else the declaration's default. The rule helix.settings.get reads with.
json effective_setting(const json& settings, const SettingDecl& d);
/// <dir of settings_path>/plugin-data/<id>.json
std::string plugin_storage_path(const std::string& settings_path, const std::string& id);

/// Target of a plugin_event: "<id>_<name>[:arg]". `id` is empty when malformed.
struct PluginEventTarget {
    std::string id;
    std::string name;
    std::optional<std::string> arg;
};

PluginEventTarget parse_plugin_event(std::string_view user_data);

/// Runs the helix.ui.on handler `name` of `rt` with `arg` (or nil). False if there is none.
bool dispatch_ui_handler(LuaRuntime& rt, const std::string& name,
                         const std::optional<std::string>& arg);

/// Runs the helix.ui.on handler `name` of `rt` with `args`. False if there is none.
bool dispatch_ui_handler(LuaRuntime& rt, const std::string& name, const LuaRuntime::PushFn& args);

/// Runs the helix.ui.on handler `name` of `rt` through LuaRuntime::call_sync, as a menu
/// check. False, after report_error, when there is no such handler; otherwise
/// call_sync's result.
bool call_ui_handler_sync(LuaRuntime& rt, const std::string& name, const LuaRuntime::PushFn& args,
                          const LuaRuntime::ReadFn& read);

/// One lifecycle hook a plugin may register on a widget it declares.
enum class WidgetHook { Attach, Detach, Size, Activate, Deactivate };

/// Runs the helix.widget hook `rt` registered for `widget_id` (the full id, with the
/// plugin prefix). False when the plugin registered none for that hook.
bool dispatch_widget_hook(LuaRuntime& rt, const std::string& widget_id, WidgetHook hook,
                          const LuaRuntime::PushFn& args = {});

/// Frees every retired plugin subject that no observer holds any more. Cheap; PluginHost
/// calls it on every load and unload.
void sweep_retired_subjects();

/// Retired subjects still waiting for their observers to go.
size_t retired_subject_count();

} // namespace helix::plugin
