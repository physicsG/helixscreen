local temp = helix.subject.string("temp", "waiting")

helix.widget("tile", {})

helix.moonraker.subscribe({ extruder = { "temperature" } }, function(s)
    if s.extruder and s.extruder.temperature then
        temp:set(string.format("%.2f", s.extruder.temperature))
    end
end)
