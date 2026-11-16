-- SaintNova client loader, second generation.
--
-- Roblox 0.747 removed loadstring from game scripts ("loadstring() is not
-- available"), so this loader never compiles text. It clones a prepared
-- ModuleScript and requires the clone: the engine compiles the module's Source
-- inside this VM.
--
-- The host rewrites the template's Source bytes in memory (same length, padded
-- with spaces), sets _G.SaintNovaGo, and reads _G.SaintNovaState / _G.SaintNovaResult /
-- _G.SaintNovaLog.
--
-- Template: ReplicatedStorage.SaintNovaTemplate, a ModuleScript whose Source is
-- "--SAINTNOVA" followed by spaces. The Studio backend writes the payload directly.

_G.SaintNovaRuns = 0
_G.SaintNovaGo = 0
_G.SaintNovaState = "boot"
_G.SaintNovaResult = ""
_G.SaintNovaLog = ""

local replicatedStorage = game:GetService("ReplicatedStorage")
local template = replicatedStorage:FindFirstChild("SaintNovaTemplate")

print("SAINTNOVA-READY", template ~= nil)
_G.SaintNovaState = template and "ready" or "no-template"

task.spawn(function()
    while true do
        task.wait(0.05)
        if _G.SaintNovaGo ~= 0 then
            _G.SaintNovaGo = 0
            _G.SaintNovaState = "running"
            _G.SaintNovaLog = ""
            local ok, value
            if template then
                local clone = template:Clone()
                clone.Parent = script
                ok, value = pcall(function()
                    local fn = require(clone)
                    local lines = {}
                    local realEnv = getfenv(fn)
                    local env = setmetatable({}, { __index = realEnv })
                    local function record(...)
                        local count = select("#", ...)
                        local parts = table.create(count)
                        for index = 1, count do
                            parts[index] = tostring(select(index, ...))
                        end
                        lines[#lines + 1] = table.concat(parts, "\t")
                    end
                    env.print = function(...)
                        record(...)
                        realEnv.print(...)
                    end
                    env.warn = function(...)
                        record(...)
                        realEnv.warn(...)
                    end
                    setfenv(fn, env)

                    local packed = table.pack(pcall(fn))
                    _G.SaintNovaLog = table.concat(lines, "\n")
                    if not packed[1] then
                        error(packed[2], 0)
                    end
                    return packed[2]
                end)
                clone:Destroy()
            else
                ok, value = false, "SaintNovaTemplate is missing from ReplicatedStorage"
            end
            _G.SaintNovaState = ok and "ok" or "runtime-error"
            _G.SaintNovaResult = tostring(value)
            print(ok and "SAINTNOVA-OK" or "SAINTNOVA-ERR", value)
            _G.SaintNovaRuns = _G.SaintNovaRuns + 1
        end
    end
end)
