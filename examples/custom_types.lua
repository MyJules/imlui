-- C++ types from Lua: run with imlui_custom_types (it registers Gauge and the `backend` singleton).
--   Gauge    an element type written in C++ (examples/custom_types/gauge.cppm)
--   backend  a singleton object (examples/custom_types/system_info.cppm)

local s = state { level = 0.4, peaks = 0, ticks = 0, last = "" }

theme { seed = "#006A6A", dark = true }

-- Signal handlers on a singleton are plain functions: handler(args...).
backend.onTick = function(count) s.ticks = count end

local function gauge(id, label, scale)
    return Gauge {
        id = id, label = label, size = 150,
        -- The first gauge is two-way bound: clicking it writes the new value back into s.level.
        value = scale == 1 and bind(s, "level") or function() return math.min(1, s.level * scale) end,
        onPeak = function(self, v)
            s.peaks = s.peaks + 1
            s.last = ("%s peaked at %.0f%%"):format(self.label, v * 100)
        end,
    }
end

return ApplicationWindow {
    title = "imlui: C++ types", width = 640, height = 560,
    Column {
        spacing = 16,
        Text { text = "Custom C++ types", style = "headlineSmall" },
        Text {
            text = "Three Gauge elements (C++), driven by one slider. Click the first gauge: it advances and "
                .. "notify() writes the value back through the bind, so the slider follows.",
            style = "bodyMedium", color = "onSurfaceVariant", wrap = true,
        },
        Row {
            spacing = 24,
            gauge("cpu", "CPU", 1),
            gauge("gpu", "GPU", 1.5),
            gauge("disk", "Disk", 2),
        },
        Slider { value = bind(s, "level"), min = 0, max = 1, fillWidth = true, label = "Level" },
        Row {
            spacing = 12,
            Button { text = "Reset gauges", icon = "restart_alt",
                     onClicked = function() cpu:reset(); gpu:reset(); disk:reset() end },
            Button { text = "Tick now", variant = "tonal", onClicked = function() backend:tick() end },
            Switch { text = "Pause backend",
                     checked = bind(function() return backend.paused end, function(v) backend.paused = v end) },
        },
        Text { text = function() return ("Peaks: %d   %s"):format(s.peaks, s.last) end },
        Divider {},
        Text { text = "backend (a C++ singleton)", style = "titleMedium" },
        Text { text = function()
            return ("time %s   ticks %d   frame %d"):format(backend.time, s.ticks, backend.frame)
        end },
        Text { text = function() return ("load %.0f%%"):format(backend.load * 100) end },
        ProgressBar { value = function() return backend.load end, fillWidth = true },
        Text { text = function() return "Registered types: " .. table.concat(types(), ", ") end,
               style = "bodySmall", color = "onSurfaceVariant" },
    },
}
