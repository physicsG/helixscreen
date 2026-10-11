-- Calibration Bridge: run slicer-side filament calibrations at the printer.
-- A companion (an OrcaSlicer plugin) posts wizard context through Moonraker
-- agent events; the screen starts the pre-sliced print, asks which result is
-- best, and posts the answer back. Overlay-only: no home tile, the companion
-- event is the front door and the settings action row is the manual one.
--
-- Contract (the README is the spec):
--   event helixcal.available  {wizard, filament, printer, nozzle_mm, current,
--                              choices (numbers), file}
--   event helixcal.cancel     {}
--   POST companion_url        {event="chosen", wizard, value, filament, current}

local t = helix.i18n.t

-- States: 0 idle, 1 available, 2 printing, 3 asking, 4 done.
local state = helix.subject.int("state", 0)
local start_on = helix.subject.int("start_on", 0)
local printing_on = helix.subject.int("printing_on", 0)
local pick_on = helix.subject.int("pick_on", 0)
local result_on = helix.subject.int("result_on", 0)
-- LVGL dropdown selection, 0-based; the first option is pre-selected.
local choice_idx = helix.subject.int("choice_idx", 0)
local state_title = helix.subject.string("state_title", t("No calibration running"))
local ctx_printer = helix.subject.string("ctx_printer", "")
local ctx_filament = helix.subject.string("ctx_filament", "")
local ctx_current = helix.subject.string("ctx_current", "")
local result_value = helix.subject.string("result_value", "")
local result_sub = helix.subject.string("result_sub", "")
local status_line = helix.subject.string("status_line", "")

-- The English source of whatever the status line and result note show, so a
-- language switch can say them again in the new language. nil = raw text or
-- nothing standing.
local status_key, result_key

local function set_status(key)
    status_key = key
    status_line:set(key and t(key) or "")
end

local function set_result_note(key, raw)
    result_key = key
    result_sub:set(key and t(key) or raw)
end

local WIZARDS = {
    temp_tower = { name = "Temperature tower", fmt = "%d\u{00B0}C", integral = true },
    pa_pattern = { name = "PA pattern", fmt = "%.2f" },
    flow_rate = { name = "Flow rate", fmt = "%d%%", integral = true },
}

-- The calibration in flight. `choices` holds numbers; the dropdown labels and
-- the posted value come from one formatter, so what the user read is what the
-- companion receives.
local cal = nil
local overlay_handle = nil
local start_inflight = false

local function set_state(v)
    state:set(v)
    start_on:set(v == 1 and 1 or 0)
    printing_on:set(v == 2 and 1 or 0)
    pick_on:set(v == 3 and 1 or 0)
    result_on:set(v == 4 and 1 or 0)
end

-- The context lines, rebuilt rather than patched so a language switch can
-- re-say them from the same data.
local function refresh_ctx()
    if not cal then
        state_title:set(t("No calibration running"))
        ctx_printer:set("")
        ctx_filament:set("")
        ctx_current:set("")
        return
    end
    local w = WIZARDS[cal.wizard]
    state_title:set(t(w.name))
    local nozzle = cal.nozzle_mm and (" \u{00B7} " .. t("nozzle") .. " " .. tostring(cal.nozzle_mm)) or ""
    ctx_printer:set(cal.printer .. nozzle)
    ctx_filament:set(cal.filament)
    ctx_current:set(t("Profile") .. ": " .. w.fmt:format(cal.current))
end

local function open_ask()
    if overlay_handle then
        -- A showing overlay keeps the attrs it was opened with, so a new
        -- calibration closes it to get its own choices into the dropdown.
        overlay_handle.close()
        overlay_handle = nil
    end
    local labels = {}
    for i, v in ipairs(cal.choices) do
        labels[i] = WIZARDS[cal.wizard].fmt:format(v)
    end
    overlay_handle = helix.ui.overlay("calibration-bridge__ask", { opts = table.concat(labels, "\n") })
end

local function cancel_cal()
    cal = nil
    set_state(0)
    refresh_ctx()
    choice_idx:set(0)
    set_status(nil)
    set_result_note(nil, "")
end

local function on_available(data)
    if type(data) ~= "table" then
        return
    end
    local w = WIZARDS[data.wizard]
    if not w or type(data.choices) ~= "table" then
        set_status("bad event")
        return
    end
    -- Validate everything before mutating anything: a refused event must not
    -- leave a half-applied calibration behind.
    local choices = {}
    for i, v in ipairs(data.choices) do
        local n = tonumber(v)
        if not n or (w.integral and n ~= math.floor(n)) then
            set_status("bad event")
            return
        end
        choices[i] = n
    end
    if #choices < 2 then
        set_status("bad event")
        return
    end
    local current = tonumber(data.current)
    if not current or (w.integral and current ~= math.floor(current)) then
        set_status("bad event")
        return
    end
    cal = {
        wizard = data.wizard,
        filament = tostring(data.filament or ""),
        printer = tostring(data.printer or ""),
        nozzle_mm = tonumber(data.nozzle_mm),
        current = current,
        choices = choices,
        file = tostring(data.file or ""),
    }
    choice_idx:set(0)
    set_status(nil)
    set_state(1)
    refresh_ctx()
    if helix.settings.get("auto_open") ~= false then
        open_ask()
    end
end

local function post_result(value)
    local url = helix.settings.get("companion_url") or ""
    if not url:match("^https?://") then
        set_result_note("(no companion URL set)", "")
        return
    end
    local body = helix.json.encode({
        event = "chosen",
        wizard = cal.wizard,
        value = value,
        filament = cal.filament,
        current = cal.current,
    })
    local r, err = helix.http.post(url, {
        body = body,
        headers = { ["Content-Type"] = "application/json" },
    })
    if r and r.status >= 200 and r.status < 300 then
        set_result_note("sent to the companion", "")
    elseif r then
        set_result_note(nil, ("HTTP %d"):format(r.status))
    else
        set_result_note(nil, err or "")
    end
end

-- Printing ends: complete hands over to the picker; a pause is not an end;
-- anything else returns to available with the reason on the status line.
local prev_printing = false
local function on_print_state(v)
    if not cal or state:get() ~= 2 then
        prev_printing = v == "printing"
        return
    end
    if v == "printing" then
        prev_printing = true
        return
    end
    if v == "paused" or not prev_printing then
        return
    end
    prev_printing = false
    if v == "complete" then
        set_state(3)
        if helix.settings.get("auto_ask") ~= false then
            open_ask()
        end
    else
        set_state(1)
        set_status(v == "cancelled" and "print cancelled" or "print failed")
    end
end

helix.printer.watch("print_state", on_print_state)

helix.moonraker.on_agent_event("helixcal.available", function(_, data)
    on_available(data)
end)

helix.moonraker.on_agent_event("helixcal.cancel", function()
    cancel_cal()
end)

helix.ui.on("open", function()
    if cal then
        open_ask()
    else
        helix.ui.toast(t("No calibration running"))
    end
end)

helix.ui.on("start", function()
    if not cal or state:get() ~= 1 or start_inflight then
        return
    end
    if cal.file == "" then
        set_status("no print file in the event")
        return
    end
    start_inflight = true
    local ok, err = helix.moonraker.call("printer.print.start", { filename = cal.file })
    start_inflight = false
    if not ok then
        set_status("print failed")
        return
    end
    set_state(2)
end)

helix.ui.on("accept", function()
    if not cal or state:get() ~= 3 then
        return
    end
    local i = choice_idx:get()
    if i < 0 or i >= #cal.choices then
        return -- out of range: a list shrank between pick and accept
    end
    local value = cal.choices[i + 1]
    result_value:set(WIZARDS[cal.wizard].fmt:format(value))
    set_state(4)
    post_result(value)
end)

helix.ui.on("cancel", function()
    if cal and state:get() == 2 then
        -- The button sits next to the running calibration print: cancelling
        -- the calibration stops the print too.
        helix.moonraker.call("printer.print.cancel", {})
    end
    cancel_cal()
end)

-- Subject-carried strings re-say themselves on a language switch.
helix.i18n.on_change(function()
    refresh_ctx()
    if status_key then
        status_line:set(t(status_key))
    end
    if result_key then
        result_sub:set(t(result_key))
    end
end)
