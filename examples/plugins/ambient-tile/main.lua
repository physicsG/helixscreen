-- Ambient Tile: room temperature and humidity from Home Assistant or any
-- JSON endpoint, polled over http. The readings, their age and the recent
-- temperature history land on subjects; a bad source is a status line and a
-- growing age, never a toast and never stale data presented as fresh.

local t = helix.i18n.t

local compact = helix.subject.int("compact", 1)
local header_on = helix.subject.int("header_on", 0)
local spark_on = helix.subject.int("spark_on", 0)
local age_on = helix.subject.int("age_on", 0)
local temp_text = helix.subject.string("temp_text", "--")
local rh_text = helix.subject.string("rh_text", "--")
local age_text = helix.subject.string("age_text", t("Never"))
local source_line = helix.subject.string("source_line", "")
local status_line = helix.subject.string("status_line", "")

-- The temperature history the sparklines draw. Capped, in-memory only: it
-- refills within a few polls of a restart.
local WINDOW_MAX = 240
local window = {}

local spark = helix.canvas("spark")
local plot = helix.canvas("plot")

local function draw(c, cw, ch)
    local w, h = cw or 0, ch or 0
    if w > 4 and h > 4 and #window >= 2 then
        local lo, hi = window[1], window[1]
        for _, v in ipairs(window) do
            lo = math.min(lo, v)
            hi = math.max(hi, v)
        end
        if hi - lo < 1 then
            hi = lo + 1
        end
        local pad = (hi - lo) * 0.1
        lo, hi = lo - pad, hi + pad
        -- The newest dot keeps its radius clear of the right edge.
        local xmax = w - 1 - 3
        local pts = {}
        for i, v in ipairs(window) do
            pts[#pts + 1] = (i - 1) * xmax / (#window - 1)
            pts[#pts + 1] = (h - 1) * (1 - (v - lo) / (hi - lo))
        end
        c:polyline(pts, { color = "primary", width = 2, fill = "primary", fill_opa = 15, baseline = h - 1 })
        c:circle(pts[#pts - 1], pts[#pts], 2, { fill = "primary" })
    end
    c:commit()
end

-- Each canvas reports its size through on_size; redraws reuse the last one.
local sizes = { spark = {}, plot = {} }

local function redraw()
    draw(spark, sizes.spark.w, sizes.spark.h)
    draw(plot, sizes.plot.w, sizes.plot.h)
end

spark:on_size(function(w, h)
    sizes.spark.w, sizes.spark.h = w, h
    draw(spark, w, h)
end)
plot:on_size(function(w, h)
    sizes.plot.w, sizes.plot.h = w, h
    draw(plot, w, h)
end)

-- The status line keeps the English key it was built from so a language
-- switch can say it again; a raw status ("HTTP 500") has no key.
local status_key, status_raw

local function status_text()
    return status_key and t(status_key) or status_raw or ""
end

local function set_status(key, raw)
    status_key, status_raw = key, raw
    status_line:set(status_text())
end

-- ---------------------------------------------------------------- age ----
-- No wall clock in the sandbox: a one-second tick counts from the last good
-- reading, and the age text only re-renders when its wording changes.
local elapsed = -1 -- -1: no reading yet, so the line reads "Never"

local function age_string()
    if elapsed < 0 then
        return t("Never")
    end
    if elapsed < 30 then
        return t("Updated just now")
    end
    if elapsed < 90 then
        return t("Updated 1m ago")
    end
    if elapsed < 3570 then
        return t("Updated %dm ago"):format(math.floor((elapsed + 30) / 60))
    end
    return t("Updated %dh ago"):format(math.max(1, math.floor((elapsed + 1800) / 3600)))
end

helix.timer.every(1000, function()
    elapsed = elapsed >= 0 and elapsed + 1 or elapsed
    local s = age_string()
    if s ~= age_text:get() then
        age_text:set(s)
    end
end)

-- ----------------------------------------------------------- settings ----

-- The host (and port) alone: no scheme, userinfo, path, query or fragment.
local function host_of(u)
    local authority = u:gsub("^https?://", ""):match("^[^/?#]*")
    return (authority:gsub("^.*@", ""))
end

local function refresh_source()
    local src = helix.settings.get("source") or "Home Assistant"
    local url = helix.settings.get("url") or ""
    local label = url ~= "" and host_of(url) or t("(not set)")
    source_line:set(src .. " \u{00B7} " .. label)
end

-- The settings one poll works from, read once so a change mid-poll cannot mix
-- two sources.
local function read_config()
    local function get(key)
        return helix.settings.get(key) or ""
    end
    return {
        source = helix.settings.get("source") or "Home Assistant",
        url = get("url"),
        token = get("token"),
        temp_entity = get("temp_entity"),
        humidity_entity = get("humidity_entity"),
        temp_pointer = get("temp_pointer"),
        humidity_pointer = get("humidity_pointer"),
    }
end

-- Every destination is checked to its shape before anything reaches the
-- network, so a typo is a status line, not a request to a half-parsed URL.
local function configured(cfg)
    if not cfg.url:match("^https?://") then
        return false
    end
    local keys = { "temp_pointer", "humidity_pointer" }
    if cfg.source == "Home Assistant" then
        if cfg.token == "" then
            return false
        end
        keys = { "temp_entity", "humidity_entity" }
    end
    for _, key in ipairs(keys) do
        if cfg[key] ~= "" and not cfg[key]:match("^[%w_.]+$") then
            return false
        end
    end
    return cfg[keys[1]] ~= ""
end

-- ------------------------------------------------------------- fetch -----

-- A number, or a string holding one, that is finite and small enough to print.
local function reading(v)
    local n = type(v) == "number" and v or (type(v) == "string" and tonumber(v)) or nil
    if n and n == n and math.abs(n) < 1e6 then
        return n
    end
    return nil
end

-- Dot-separated keys; a numeric segment indexes a JSON array from zero (the
-- decoded Lua table is one-based, so the segment shifts by one) and otherwise
-- names an object key.
local function resolve(root, path)
    local node = root
    for seg in path:gmatch("[^%.]+") do
        if type(node) ~= "table" then
            return nil
        end
        local nxt
        if seg:match("^%d+$") then
            nxt = node[tonumber(seg) + 1]
        end
        if nxt == nil then
            nxt = node[seg]
        end
        node = nxt
    end
    return reading(node)
end

-- Transport errors carry the full request URL, credentials included, so they
-- are never shown or logged: a fixed string stands in.
local function http_problem(r)
    if not r then
        return { key = "unreachable" }
    end
    if r.status ~= 200 then
        return { raw = ("HTTP %d"):format(r.status) }
    end
    return nil
end

-- Returns the two readings (either may be nil) and a problem to report, or
-- nil, problem when nothing was read. An entity reporting "unavailable" or
-- "unknown" has no reading but is not an error; one failing request leaves the
-- other entity's reading alone.
local function fetch_ha(cfg)
    local base = (cfg.url:gsub("/$", ""))
    local out, problem = {}, nil
    for i, key in ipairs({ "temp_entity", "humidity_entity" }) do
        local ent = cfg[key]
        if ent ~= "" then
            local r = helix.http.get(base .. "/api/states/" .. ent,
                { headers = { Authorization = "Bearer " .. cfg.token } })
            local p = http_problem(r)
            if p then
                problem = problem or p
            else
                local j = helix.json.decode(r.body)
                out[i] = type(j) == "table" and reading(j.state) or nil
            end
        end
    end
    if out[1] == nil and out[2] == nil then
        return nil, problem or { key = "bad data" }
    end
    return out, problem
end

local function fetch_json(cfg)
    local r = helix.http.get(cfg.url)
    local p = http_problem(r)
    if p then
        return nil, p
    end
    local j = helix.json.decode(r.body)
    if type(j) ~= "table" then
        return nil, { key = "bad data" }
    end
    local out = { resolve(j, cfg.temp_pointer), resolve(j, cfg.humidity_pointer) }
    if out[1] == nil and out[2] == nil then
        return nil, { key = "bad data" }
    end
    return out
end

-- A reading missing from a successful poll shows dashes rather than the
-- previous value under a fresh age.
local function apply(temp, rh)
    temp_text:set(temp and ("%.1f\u{00B0}"):format(temp) or "--")
    rh_text:set(rh and ("%d%%"):format(math.floor(rh + 0.5)) or "--")
    if temp then
        window[#window + 1] = temp
        if #window > WINDOW_MAX then
            table.remove(window, 1)
        end
    end
    elapsed = 0
end

-- Consecutive failed polls; past STALE_AFTER the tile stops showing the old
-- values, since small tiles have no age line to mark them stale.
local STALE_AFTER = 3
local failures = 0

local function note_failure()
    failures = failures + 1
    if failures >= STALE_AFTER then
        temp_text:set("--")
        rh_text:set("--")
    end
end

-- Bumped whenever a source setting changes: a poll started under an older
-- generation never applies its result.
local generation = 0
local polling = false

-- One poll against the current settings. Returns without applying when the
-- settings changed meanwhile, so the caller polls again.
local function poll_body(gen)
    local cfg = read_config()
    if not configured(cfg) then
        set_status("not configured")
        return
    end
    local out, problem
    if cfg.source == "Home Assistant" then
        out, problem = fetch_ha(cfg)
    else
        out, problem = fetch_json(cfg)
    end
    if gen ~= generation then
        return
    end
    if not out then
        set_status(problem.key, problem.raw)
        note_failure()
        return
    end
    failures = 0
    apply(out[1], out[2])
    set_status(problem and problem.key, problem and problem.raw)
    age_text:set(age_string())
    redraw()
end

local function poll_once()
    if polling then
        return -- the running poll re-reads the settings if they changed
    end
    polling = true
    local ok = pcall(function()
        local gen
        repeat
            gen = generation
            poll_body(gen)
        until gen == generation
    end)
    polling = false
    if not ok then
        set_status("bad data")
    end
end

-- -------------------------------------------------------------- loop -----

local poll_timer

local function rearm()
    if poll_timer then
        poll_timer:cancel()
    end
    local s = helix.settings.get("poll_s") or 60
    poll_timer = helix.timer.every(s * 1000, poll_once)
end

for _, key in ipairs({ "source", "url", "token", "temp_entity", "humidity_entity",
                       "temp_pointer", "humidity_pointer" }) do
    helix.settings.on_change(key, function()
        generation = generation + 1
        refresh_source()
        poll_once()
    end)
end
helix.settings.on_change("poll_s", function() rearm() end)

helix.i18n.on_change(function()
    refresh_source()
    age_text:set(age_string())
    status_line:set(status_text())
end)

helix.widget("tile", {
    on_size = function(cols, rows, w, h)
        compact:set(w < 110 and 1 or 0)
        header_on:set(h >= 90 and 1 or 0)
        spark_on:set(h >= 140 and 1 or 0)
        age_on:set(h >= 140 and 1 or 0)
    end,
})

helix.ui.on("open", function()
    helix.ui.overlay("ambient-tile__detail")
end)

refresh_source()
rearm()
pcall(poll_once) -- the top level may await: the first reading does not wait a poll
