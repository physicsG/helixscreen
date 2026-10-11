local status = helix.subject.string("status", "idle")
local size = helix.subject.string("size", "")
events = {}

helix.widget("tile", {
  on_attach = function() table.insert(events, "attach") end,
  on_detach = function() table.insert(events, "detach") end,
  on_size = function(c, r, w, h) size:set(c .. "x" .. r) end,
  on_activate = function() table.insert(events, "activate") end,
  on_deactivate = function() table.insert(events, "deactivate") end,
})

helix.ui.on("ping", function() status:set("pinged") end)
