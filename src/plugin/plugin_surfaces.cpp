// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_PLUGINS

#include "plugin_surfaces.h"

#include <algorithm>

namespace helix::plugin {

PluginSurfaces::PluginSurfaces(PluginOverlayHost& overlays, PluginPresenter& presenter)
    : overlays_(overlays), presenter_(presenter) {}

PluginSurfaces::~PluginSurfaces() {
    guard_.invalidate();
    for (auto& r : records_) {
        if (r.kind == SurfaceKind::Dialog && r.dialog) {
            PluginDialog::forget(r.dialog);
            PluginDialog::close_by_plugin(r.dialog);
        }
        presenter_.drop_plugin(r.plugin_id);
    }
}

PluginSurfaces::Record* PluginSurfaces::find(int handle) {
    auto it = std::find_if(records_.begin(), records_.end(),
                           [handle](const Record& r) { return r.handle == handle; });
    return it == records_.end() ? nullptr : &*it;
}

const PluginSurfaces::Record* PluginSurfaces::find(int handle) const {
    auto it = std::find_if(records_.begin(), records_.end(),
                           [handle](const Record& r) { return r.handle == handle; });
    return it == records_.end() ? nullptr : &*it;
}

int PluginSurfaces::open_overlay(const std::string& plugin_id, const std::string& component,
                                 PluginUi::Callbacks cb, const PluginUi::Attrs& attrs) {
    for (const auto& r : records_) {
        // A closing overlay (off the nav stack, sliding out) is not reused.
        if (r.kind == SurfaceKind::Overlay && r.plugin_id == plugin_id &&
            r.component == component && (!r.shown || overlays_.is_live(r.overlay_handle)))
            return r.handle;
    }
    Record rec;
    rec.plugin_id = plugin_id;
    rec.component = component;
    rec.kind = SurfaceKind::Overlay;
    rec.attrs = attrs;
    rec.cb = std::move(cb);
    return request(std::move(rec));
}

int PluginSurfaces::open_dialog(const std::string& plugin_id, const std::string& component,
                                PluginUi::Callbacks cb, const PluginUi::Attrs& attrs) {
    Record rec;
    rec.plugin_id = plugin_id;
    rec.component = component;
    rec.kind = SurfaceKind::Dialog;
    rec.attrs = attrs;
    rec.cb = std::move(cb);
    return request(std::move(rec));
}

int PluginSurfaces::request(Record rec) {
    const int handle = next_handle_++;
    rec.handle = handle;
    const std::string plugin_id = rec.plugin_id;
    const SurfaceKind kind = rec.kind;
    records_.push_back(std::move(rec));
    auto tok = guard_.token();
    const int ticket = presenter_.request(
        plugin_id, kind, [this, tok, handle] { return !tok.expired() && present(handle); },
        [this, tok, handle] {
            if (!tok.expired())
                finish(handle, CloseHow::Cancelled);
        });
    Record* r = find(handle);
    if (ticket == 0) {
        if (r)
            records_.remove_if([handle](const Record& x) { return x.handle == handle; });
        return 0;
    }
    if (r && !r->shown)
        r->ticket = ticket;
    return handle;
}

bool PluginSurfaces::present(int handle) {
    Record* r = find(handle);
    if (!r)
        return false;
    auto tok = guard_.token();
    if (r->kind == SurfaceKind::Dialog) {
        auto d = std::make_unique<PluginDialog>(r->plugin_id, r->component, r->attrs,
                                                [this, tok, handle](CloseHow how) {
                                                    if (!tok.expired())
                                                        finish(handle, how);
                                                });
        lv_obj_t* dialog = PluginDialog::present(std::move(d));
        if (!dialog)
            return false;
        r->dialog = dialog;
    } else {
        const int oh = overlays_.open(
            r->plugin_id, r->component,
            [this, tok, handle] {
                if (tok.expired())
                    return;
                const Record* rec = find(handle);
                finish(handle, rec && rec->closing_by_plugin ? CloseHow::Plugin : CloseHow::User);
            },
            r->attrs);
        if (oh == 0)
            return false;
        r->overlay_handle = oh;
    }
    r->shown = true;
    r->ticket = 0;
    // Copied out: on_open may close this very surface.
    auto on_open = r->cb.on_open;
    if (on_open)
        on_open();
    return true;
}

void PluginSurfaces::finish(int handle, CloseHow how) {
    Record* r = find(handle);
    if (!r)
        return;
    auto on_close = std::move(r->cb.on_close);
    records_.remove_if([handle](const Record& x) { return x.handle == handle; });
    if (on_close)
        on_close(how);
}

void PluginSurfaces::close(int handle) {
    Record* r = find(handle);
    if (!r)
        return;
    if (!r->shown) {
        presenter_.withdraw(r->ticket); // runs finish(Cancelled)
        return;
    }
    if (r->kind == SurfaceKind::Dialog) {
        PluginDialog::close_by_plugin(r->dialog); // on_hide runs finish(Plugin)
    } else {
        r->closing_by_plugin = true;
        overlays_.close(r->overlay_handle);
    }
}

bool PluginSurfaces::is_open(int handle) const {
    const Record* r = find(handle);
    return r && r->shown;
}

size_t PluginSurfaces::overlay_count(const std::string& plugin_id) const {
    return static_cast<size_t>(
        std::count_if(records_.begin(), records_.end(), [&](const Record& r) {
            return r.plugin_id == plugin_id && r.kind == SurfaceKind::Overlay;
        }));
}

size_t PluginSurfaces::dialog_count(const std::string& plugin_id) const {
    return static_cast<size_t>(
        std::count_if(records_.begin(), records_.end(), [&](const Record& r) {
            return r.plugin_id == plugin_id && r.kind == SurfaceKind::Dialog;
        }));
}

void PluginSurfaces::queue_confirm(const std::string& plugin_id, std::function<bool()> present,
                                   std::function<void()> cancelled) {
    presenter_.request(plugin_id, SurfaceKind::Confirm, std::move(present), std::move(cancelled));
}

void PluginSurfaces::drop_plugin(const std::string& plugin_id) {
    presenter_.drop_plugin(plugin_id);
    std::vector<lv_obj_t*> dialogs;
    for (const auto& r : records_) {
        if (r.plugin_id == plugin_id && r.kind == SurfaceKind::Dialog && r.dialog)
            dialogs.push_back(r.dialog);
    }
    records_.remove_if([&](const Record& r) { return r.plugin_id == plugin_id; });
    for (lv_obj_t* d : dialogs) {
        PluginDialog::forget(d);
        PluginDialog::close_by_plugin(d);
    }
}

} // namespace helix::plugin

#endif // HELIX_HAS_PLUGINS
