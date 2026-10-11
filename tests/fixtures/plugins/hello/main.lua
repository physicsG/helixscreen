local status = helix.subject.string("status", helix.settings.get("greeting"))

helix.ui.on("press", function(arg)
    status:set("pressed " .. tostring(arg))
end)

helix.ui.on("home", function()
    local ok, err = helix.gcode("G28")
    status:set(ok and "homed" or ("failed: " .. err))
end)

function on_unload()
    helix.moonraker.call("server.info", {})
end
