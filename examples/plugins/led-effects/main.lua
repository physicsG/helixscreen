-- SPDX-License-Identifier: GPL-3.0-or-later
-- led-effects: a home tile that turns one klipper-led_effect effect on or off.
-- The permissioned companion to temp-spark: it sends G-code, so enabling it
-- asks for the gcode permission, and it shows the effect's state as Klipper
-- reports it through a live subscription rather than guessing.

-- Subjects the tile binds. A name given here registers as led-effects__<name>.
local effect_label = helix.subject.string("effect", "--")
local state_text = helix.subject.string("state", "Off")
local active = helix.subject.int("active", 0)
local wide = helix.subject.int("wide", 0)

local sub = nil -- the live subscription for the selected effect

-- The name goes into a G-code line, so only the characters Klipper section
-- names use are accepted: anything else could append a second command. It also
-- becomes the subscription object "led_effect " .. name, and subscribe refuses
-- objects over 64 bytes (kMaxObjectNameBytes), so a name that long is rejected
-- here rather than faulting the plugin at load.
local function effect_name()
    local name = helix.settings.get("effect")
    if type(name) == "string" and #name <= 64 - #"led_effect " and name:match("^[%w_]+$") then
        return name
    end
    return nil
end

local function show(on)
    active:set(on and 1 or 0)
    state_text:set(on and "On" or "Off")
end

-- The first callback carries the queried state and every later one a change,
-- so the tile always shows what Klipper says, including changes made elsewhere.
local function follow()
    if sub then
        sub:cancel()
        sub = nil
    end
    local name = effect_name()
    effect_label:set(name or "Invalid name")
    show(false)
    if not name then
        return
    end
    local object = "led_effect " .. name
    sub = helix.moonraker.subscribe({[object] = {"enabled"}}, function(status)
        local s = status[object]
        if s and s.enabled ~= nil then
            show(s.enabled)
        end
    end)
end

-- The tile's tap. STOP=1 stops only this effect, leaving any other running.
-- The tile does not flip on its own: the subscription reports the result.
helix.ui.on("toggle", function()
    local name = effect_name()
    if not name then
        helix.ui.toast("Effect names may use only letters, digits and _", "warning")
        return
    end
    local line = "SET_LED_EFFECT EFFECT=" .. name
    if active:get() == 1 then
        line = line .. " STOP=1"
    end
    local ok, err = helix.gcode(line)
    if not ok then
        helix.ui.toast("LED effect failed: " .. tostring(err), "error")
    end
end)

-- At two cells wide the tile also names the effect.
helix.widget("tile", {
    on_size = function(cols)
        wide:set(cols >= 2 and 1 or 0)
    end,
})

helix.settings.on_change("effect", follow)
follow()
