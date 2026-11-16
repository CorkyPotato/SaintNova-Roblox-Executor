-- Signals, connections, and hooksignal.
--
-- The harness fires handlers synchronously and protects each one, mirroring
-- Roblox's per-connection error isolation: one failing callback does not stop
-- the others, and the error is reported through warn().

local emitted = Signal.new()
local received = {}

local first = emitted:Connect(function(value)
    table.insert(received, "first:" .. value)
end)

emitted:Connect(function(value)
    table.insert(received, "second:" .. value)
    error("deliberate handler failure")
end)

emitted:Connect(function(value)
    table.insert(received, "third:" .. value)
end)

firesignal(emitted, 1)
print("after first fire:", #received, table.concat(received, " | "))

-- getconnections returns real connection objects; Enabled is honoured.
print("connections:", #getconnections(emitted))
disableconnection(first)
firesignal(emitted, 2)
print("after disabling the first connection:", #received)
enableconnection(first)

-- A hook observes every delivery and can veto it by returning anything but
-- true. The last argument the connection saw is kept for getsignalarguments.
hooksignal(emitted, function(info, value)
    return value % 2 == 0
end)
print("signal hooked:", issignalhooked(emitted))

firesignal(emitted, 3)  -- vetoed
firesignal(emitted, 4)  -- delivered
print("arguments seen by the connection:", getsignalarguments(first)[1])

unhooksignal(emitted)
print("signal hooked:", issignalhooked(emitted))

-- Once fires exactly once.
local once = emitted:Once(function() print("once fired") end)
firesignal(emitted, 5)
firesignal(emitted, 6)
print("once still connected:", once.Connected)

-- Instance signals exist on the same machinery.
local part = Instance.new("Part")
part.Name = "SignalDemo"
part.Parent = workspace
part.Touched:Connect(function(other)
    print("touched by", other:GetFullName())
end)
firetouchinterest(part, game:GetService("Players").LocalPlayer.Character or workspace, 0)

-- replicatesignal fires the local handlers. There is no server in the harness,
-- so nothing is actually replicated; cansignalreplicate reports that honestly.
local before = #received
replicatesignal(emitted, 8)
print("replicatesignal fired locally:", #received > before, "replicates:", cansignalreplicate(emitted))
