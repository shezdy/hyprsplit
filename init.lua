---@class Hyprsplit
---@field protected _config Hyprsplit.Config
---@field protected monitor_priority_list string[]
local hyprsplit = {
    ---@class Hyprsplit.Config
    _config = {
        num_workspaces = 10,
        persistent_workspaces = false,
        force_monitor_priority = false,
    },
    monitor_priority_list = {},
    ---@type table<string, integer> map monitor name -> previous workspace id
    prev_workspaces = {},
    dsp = {
        window = {},
        workspace = {},
    },
}

---@param formatString string
local function log(formatString, ...)
    print(string.format("[hyprsplit] " .. formatString, ...))
end

---@param formatString string
local function notify_error(formatString, ...)
    local error = string.format("[hyprsplit] error: " .. formatString, ...)
    log(error)
    hl.notification.create({
        text = error,
        duration = "10000",
        icon = 3,
        color = "rgb(ff0000)",
    })
end

---@class Hyprsplit.MonitorRange
---@field base integer
---@field min integer
---@field max integer
local MonitorRange = {}
---@param monitor HL.Monitor
---@return Hyprsplit.MonitorRange
function MonitorRange:new(monitor)
    local base = -1
    if #hyprsplit.monitor_priority_list == 0 and not hyprsplit._config.force_monitor_priority then
        base = monitor.id
    else
        for i, monitor_selector in ipairs(hyprsplit.monitor_priority_list) do
            if monitor.name == monitor_selector or monitor.description == monitor_selector then
                base = i - 1
            end
        end
        if base == -1 then
            local unmappedMonitors = {}
            for _, m in ipairs(hl.get_monitors()) do
                if m ~= nil and not m.is_mirror and m.id ~= -1 then
                    local mapped = false
                    for _, monitor_selector in ipairs(hyprsplit.monitor_priority_list) do
                        if
                            monitor.name == monitor_selector
                            or monitor.description == monitor_selector
                        then
                            mapped = true
                            break
                        end
                    end
                    if not mapped then
                        table.insert(unmappedMonitors, m)
                    end
                end
            end

            -- sort into alphabetical order by name
            table.sort(unmappedMonitors, function(a, b)
                return a.name < b.name
            end)

            for i, m in ipairs(unmappedMonitors) do
                if monitor == m then
                    base = i - 1 + #hyprsplit.monitor_priority_list
                    log("auto assigning base %d to monitor %s", base, monitor.name)
                    -- notify_error("auto assigning base %d to monitor %s", base, monitor.name)
                end
            end
        end
    end
    local min = (base * hyprsplit._config.num_workspaces) + 1
    local max = (base + 1) * hyprsplit._config.num_workspaces

    return setmetatable({ base = base, min = min, max = max }, {
        __index = self,
    })
end
---@param num integer
---@return boolean
function MonitorRange:contains(num)
    if type(num) ~= "number" then
        return false
    end
    return num >= self.min and num <= self.max
end
hyprsplit.MonitorRange = MonitorRange

---@param workspace_str string
---@return string
function hyprsplit.get_workspace_string(workspace_str)
    local ws_id = 1
    local num_workspaces = hyprsplit._config.num_workspaces
    local active_monitor = hl.get_active_monitor()

    if active_monitor == nil or active_monitor.active_workspace == nil then
        log("missing monitor/active_active workspace in get_workspace?")
        return "0"
    end
    local range = MonitorRange:new(active_monitor)

    local workspace_int = math.tointeger(workspace_str)
    if workspace_str:sub(1, 1) == "+" or workspace_str:sub(1, 1) == "-" then
        if workspace_int == nil then
            return tostring(active_monitor.active_workspace.id)
        end

        local local_current = active_monitor.active_workspace.id - range.min + 1
        ws_id = local_current + workspace_int

        ws_id = math.max(ws_id, 1)
        ws_id = math.min(ws_id, num_workspaces)
    elseif workspace_int ~= nil then
        ws_id = math.max(workspace_int, 1)
    elseif
        workspace_str:sub(1, 1) == "r"
        and (workspace_str:sub(2, 2) == "-" or workspace_str:sub(2, 2) == "+")
    then
        local plusminus_num = math.tointeger(workspace_str:sub(2))
        if plusminus_num == nil then
            return tostring(active_monitor.active_workspace.id)
        end

        ws_id = active_monitor.active_workspace.id + plusminus_num

        if ws_id <= 0 then
            ws_id = ((((ws_id - 1) % num_workspaces) + num_workspaces) % num_workspaces) + 1
        end
    elseif
        workspace_str:sub(1, 1) == "e"
        and (workspace_str:sub(2, 2) == "-" or workspace_str:sub(2, 2) == "+")
    then
        local plusminus_num = math.tointeger(workspace_str:sub(2))
        if plusminus_num == nil then
            return tostring(active_monitor.active_workspace.id)
        end

        local valid_workspaces = {}
        for _, ws in ipairs(hl.get_workspaces()) do
            if range:contains(ws.id) then
                table.insert(valid_workspaces, ws)
            end
        end

        local active_ws_index = -1
        for index, ws in ipairs(valid_workspaces) do
            if ws.active then
                active_ws_index = index
            end
        end
        if active_ws_index == -1 then
            return tostring(active_monitor.active_workspace.id)
        end

        local result_index = active_ws_index + plusminus_num
        if result_index < 1 then
            result_index = 1
        elseif result_index > #valid_workspaces then
            result_index = #valid_workspaces
        end

        return tostring(valid_workspaces[result_index].id)
    elseif workspace_str == "empty" then
        for i = range.min, range.max do
            local ws = hl.get_workspace(i)
            if ws == nil or ws.windows == 0 then
                notify_error("empty %d", i)
                return tostring(i)
            end
        end

        log("no empty workspace on monitor")
        return tostring(active_monitor.active_workspace.id)
    else
        -- no change
        return workspace_str
    end

    if ws_id > num_workspaces then
        ws_id = ((ws_id - 1) % num_workspaces) + 1
    end

    return tostring(range.min + ws_id - 1)
end

---@param args table args.workspace is required, it will be converted to string using tostring()
---@return function
function hyprsplit.dsp.focus(args)
    if args.workspace == nil then
        notify_error("dsp.focus: args.workspace is nil")
        return function() end
    end
    local workspace_arg = tostring(args.workspace)
    if workspace_arg then
        return function()
            local ws_string = hyprsplit.get_workspace_string(workspace_arg)
            local ws = hl.get_workspace(ws_string)
            local active_monitor = hl.get_active_monitor()
            -- if workspace exists, check that it is on the correct monitor.
            -- if not on the correct monitor just recheck all workspaces
            if ws and active_monitor then
                local range = MonitorRange:new(active_monitor)
                if ws.monitor.id ~= active_monitor.id and range:contains(ws.id) then
                    log("workspace exists but is on the wrong monitor")
                    hyprsplit.ensure_good_workspaces()
                    active_monitor = hl.get_active_monitor()
                end
            end

            if active_monitor and active_monitor.active_workspace then
                local current = active_monitor.active_workspace.id
                local target = math.tointeger(ws_string)
                local bf = hl.get_config("binds.workspace_back_and_forth")
                if bf and target == current then
                    local prev = hyprsplit.prev_workspaces[active_monitor.name]
                    if prev and prev ~= current then
                        log("back_and_forth: switching to prev workspace %d", prev)
                        ws_string = tostring(prev)
                        target = prev
                    end
                end
                if target ~= current then
                    hyprsplit.prev_workspaces[active_monitor.name] = current
                end
            end

            hl.dispatch(hl.dsp.focus({
                workspace = ws_string,
                on_current_monitor = true,
            }))
        end
    else
        notify_error("dsp.focus: failed to convert args.workspace to string")
        return function() end
    end
end

---@param args table args.workspace is required, it will be converted to string using tostring()
---@return function
function hyprsplit.dsp.window.move(args)
    if args.workspace == nil then
        notify_error("dsp.window.move: args.workspace is nil")
        return function() end
    end
    local workspace = tostring(args.workspace)
    if workspace then
        return function()
            args.workspace = hyprsplit.get_workspace_string(workspace)
            hl.dispatch(hl.dsp.window.move(args))
        end
    else
        notify_error("dsp.window.move: failed to convert args.workspace to string")
        return function() end
    end
end

---@param args table args.monitor1 and args.monitor2 are required as strings
---@return function
function hyprsplit.dsp.workspace.swap_monitors(args)
    if not type(args.monitor1) == "string" or not type(args.monitor2) == "string" then
        return function()
            notify_error("swap_monitors: monitor1 and monitor2 of type string are both required")
        end
    end
    return function()
        local monitor1 = hl.get_monitor(args.monitor1)
        local monitor2 = hl.get_monitor(args.monitor2)

        if monitor1 and monitor2 then
            local windows1 = hl.get_workspace_windows(monitor1.active_workspace)
            local windows2 = hl.get_workspace_windows(monitor2.active_workspace)

            for _, window in ipairs(windows1) do
                hl.dispatch(hl.dsp.window.move({
                    workspace = monitor2.active_workspace,
                    window = window,
                    follow = true,
                }))
            end
            for _, window in ipairs(windows2) do
                hl.dispatch(hl.dsp.window.move({
                    workspace = monitor1.active_workspace,
                    window = window,
                    follow = true,
                }))
            end
        end
    end
end

function hyprsplit.dsp.grab_rogue_windows()
    return function()
        hyprsplit.ensure_good_workspaces()
        local active_workspace = hl.get_active_workspace()
        if active_workspace == nil then
            return
        end

        for _, window in ipairs(hl.get_windows()) do
            if window.mapped and not window.workspace.special then
                local in_good_workspace = false

                for _, monitor in ipairs(hl.get_monitors()) do
                    local range = MonitorRange:new(monitor)

                    if range:contains(window.workspace.id) then
                        in_good_workspace = true
                    end
                end

                if not in_good_workspace then
                    log(
                        'moving window "%s" to active workspace %d',
                        window.title,
                        active_workspace.id
                    )
                    hl.dispatch(hl.dsp.window.move({
                        workspace = active_workspace,
                        window = window,
                        follow = false,
                    }))
                end
            end
        end
    end
end

function hyprsplit.ensure_good_workspaces()
    local cursor_no_warps_orig = hl.get_config("cursor.no_warps")
    hl.config({ cursor = { no_warps = true } })

    -- something funky seems to be happening with the active workspaces
    -- and i don't feel like debugging it properly.
    -- so i'm just gonna use this jank as a workaround for now
    local orig_focused_mon = hl.get_active_monitor()
    local monitors_active_workspaces = {}

    local monitors = hl.get_monitors()
    for _, m in ipairs(monitors) do
        if m ~= nil and not m.is_mirror and m.id ~= -1 then
            local range = MonitorRange:new(m)
            monitors_active_workspaces[m.name] = m.active_workspace.id

            if not range:contains(m.active_workspace.id) then
                log(
                    "%s base %d active workspace %d out of bounds, changing workspace to %d",
                    m.name,
                    range.base,
                    m.active_workspace.id,
                    range.min
                )
                monitors_active_workspaces[m.name] = range.min
                hl.dispatch(hl.dsp.focus({ workspace = range.min, on_current_monitor = true }))
            end
        end
    end

    for _, m in ipairs(monitors) do
        if m ~= nil and not m.is_mirror and m.id ~= -1 then
            local range = MonitorRange:new(m)

            for _, ws in ipairs(hl.get_workspaces()) do
                if ws.monitor and ws.monitor.id ~= m.id and range:contains(ws.id) then
                    log(
                        "workspace %d on monitor %d move to %s %d",
                        ws.id,
                        ws.monitor.id,
                        m.name,
                        range.base
                    )
                    hl.dispatch(hl.dsp.workspace.move({ workspace = ws, monitor = m }))
                    hl.dispatch(hl.dsp.focus({ monitor = m }))
                    hl.dispatch(hl.dsp.focus({
                        workspace = monitors_active_workspaces[m.name],
                        on_current_monitor = true,
                    }))
                end
            end

            if hyprsplit._config.persistent_workspaces then
                for i = range.min, range.max do
                    hl.workspace_rule({
                        workspace = tostring(i),
                        persistent = true,
                        monitor = m.name,
                    })
                end
            end
        end
    end

    if orig_focused_mon then
        hl.dispatch(hl.dsp.focus({ monitor = orig_focused_mon }))
    end

    hl.config({ cursor = { no_warps = cursor_no_warps_orig } })
end

---@param k string
---@param v any
---@param valid boolean|nil
local function load_config_value(k, v, valid)
    if v ~= nil then
        if (valid == nil and type(v) == type(hyprsplit._config[k])) or valid then
            hyprsplit._config[k] = v
        else
            notify_error('config key "%s" invalid value "%s"', tostring(k), tostring(v))
        end
    end
end

---@param config Hyprsplit.Config
function hyprsplit.config(config)
    load_config_value(
        "num_workspaces",
        config.num_workspaces,
        math.type(config.num_workspaces) == "integer" and config.num_workspaces >= 1
    )
    load_config_value("persistent_workspaces", config.persistent_workspaces)
    load_config_value("force_monitor_priority", config.force_monitor_priority)
end

---@param key string
---@return any
function hyprsplit.get_config(key)
    return hyprsplit._config[key]
end

---@param priority table array of monitor strings
function hyprsplit.monitor_priority(priority)
    for _, monitor in ipairs(priority) do
        if type(monitor) == "string" then
            log("assigning monitor priority: %s -> %d", monitor, #hyprsplit.monitor_priority_list)
            table.insert(hyprsplit.monitor_priority_list, monitor)
        end
    end
end

hl.on("config.reloaded", function()
    hyprsplit.prev_workspaces = {}
    hyprsplit.ensure_good_workspaces()
end)

hl.on("monitor.added", function(monitor)
    local cursor_no_warps_orig = hl.get_config("cursor.no_warps")
    hl.config({ cursor = { no_warps = true } })

    -- change to first workspace on the new monitor
    local orig_focused_mon = hl.get_active_monitor()
    hl.dispatch(hl.dsp.focus({ monitor = monitor }))
    local range = MonitorRange:new(monitor)
    hl.dispatch(hl.dsp.focus({ workspace = range.min, on_current_monitor = true }))
    hl.dispatch(hl.dsp.focus({ monitor = orig_focused_mon }))

    hl.config({ cursor = { no_warps = cursor_no_warps_orig } })

    hyprsplit.ensure_good_workspaces()
end)

hl.on("monitor.removed", function(monitor)
    hyprsplit.prev_workspaces[monitor.name] = nil
    if hyprsplit._config.persistent_workspaces then
        local range = MonitorRange:new(monitor)
        for i = range.min, range.max do
            hl.workspace_rule({
                workspace = tostring(i),
                persistent = false,
            })
        end
    end
end)

return hyprsplit
