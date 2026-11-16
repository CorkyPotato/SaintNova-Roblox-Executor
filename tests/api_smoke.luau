-- Harness API smoke test
local function check(name, value, expected)
    local ok = expected == nil and value ~= nil or value == expected
    print((ok and "PASS " or "FAIL ") .. name .. " -> " .. tostring(value))
end

-- filesystem ---------------------------------------------------------------
makefolder("smoke/sub")
writefile("smoke/a.txt", "hello")
appendfile("smoke/a.txt", " world")
check("readfile", readfile("smoke/a.txt"), "hello world")
check("isfile", isfile("smoke/a.txt"), true)
check("isfolder", isfolder("smoke"), true)
check("isfile missing", isfile("smoke/nope.txt"), false)
local listed = listfiles("smoke")
check("listfiles count", #listed, 2)
writefile("smoke/module.luau", "return { value = 41 + 1 }")
local mod = loadfile("smoke/module.luau")
check("loadfile result", mod().value, 42)
writefile("smoke/run.luau", "return 'dofile worked'")
check("dofile", dofile("smoke/run.luau"), "dofile worked")
local escaped, escape_err = pcall(readfile, "../escape.txt")
check("path escape rejected", escaped, false)
check("path escape message", escape_err:find("leave the workspace") ~= nil, true)
delfile("smoke/a.txt")
check("delfile", isfile("smoke/a.txt"), false)
delfolder("smoke")
check("delfolder", isfolder("smoke"), false)

-- console ------------------------------------------------------------------
rconsolecreate()
rconsolename("smoke console")
rconsoleprint("console print")
rconsoleinfo("console info")
rconsolewarn("console warn")
rconsoleerr("console err")
rconsoledebug("console debug")
printconsole("colored", 255, 0, 0)
rconsoleclear()
rconsoleclose()

-- reflection ---------------------------------------------------------------
local table_value = setmetatable({}, { __index = function() return 7 end })
check("getrawmetatable", getrawmetatable(table_value) ~= nil, true)
check("isreadonly plain", isreadonly({}), false)
setreadonly(table_value, true)
check("isreadonly set", isreadonly(table_value), true)
local write_ok = pcall(function() table_value.x = 1 end)
check("readonly blocks write", write_ok, false)
setreadonly(table_value, false)
table_value.x = 1
check("writable after setreadonly", table_value.x, 1)

local original_function = function(a, b) return a + b end
local clone = clonefunction(original_function)
check("clonefunction", clone(2, 3), 5)
check("iscclosure print", iscclosure(print), true)
check("islclosure user", islclosure(original_function), true)
check("iscclosure user", iscclosure(original_function), false)
local wrapped = newcclosure(function(a) return a * 2 end)
check("newcclosure call", wrapped(21), 42)
check("newcclosure is c", iscclosure(wrapped), true)
check("getcallbackvalue", getcallbackvalue(wrapped)(5), 10)
check("isexecutorclosure prelude", isexecutorclosure(getconnections), true)
check("isexecutorclosure user", isexecutorclosure(original_function), false)
check("islineinfo", islineinfo(original_function), true)

local hooked = setmetatable({}, { __index = function(_, key) return "orig:" .. key end })
local old_index
old_index = hookmetamethod(hooked, "__index", function(self, key) return old_index(self, key) .. ":hooked" end)
check("hookmetamethod", hooked.foo, "orig:foo:hooked")

local exec_mt = getrawmetatable(Instance.new("Part"))
check("instance metatable readonly", isreadonly(exec_mt), true)

-- scheduling / closures ----------------------------------------------------
local closure_ok, closure_err = pcall(function() hookfunction(print, function() end) end)
check("hookfunction unsupported", closure_ok, false)
check("hookfunction message", closure_err:find("not available") ~= nil, true)
check("ishooked", ishooked(print), false)
check("checkcaller", checkcaller(), true)
check("getnamecallmethod", getnamecallmethod(), nil)

-- identity / host ----------------------------------------------------------
check("default identity", getidentity(), 7)
setthreadidentity(8)
check("set identity", getthreadidentity(), 8)
setidentity(7)
local hwid = gethwid()
check("gethwid shape", type(hwid) == "string" and hwid:sub(1, 10) == "saintnova-", true)
check("getfflag unknown", getfflag("DFIntNope"), nil)
check("isnetworkowner", isnetworkowner(workspace), false)
check("isrbxactive", isrbxactive(), false)
local clipboard_ok, clipboard_value = pcall(getclipboard)
check("getclipboard", clipboard_ok, true)

-- filesystem denials -------------------------------------------------------
local net_ok, net_err = pcall(httpget, "https://example.com")
check("httpget disabled", net_ok, false)
check("httpget message", net_err:find("disabled") ~= nil, true)
local fflag_ok = pcall(setfflag, "DFIntX", "1")
check("setfflag unavailable", fflag_ok, false)

-- signals ------------------------------------------------------------------
local signal = Signal.new()
local received = {}
local connection = signal:Connect(function(a, b) table.insert(received, a + b) end)
check("getconnections", #getconnections(signal), 1)
firesignal(signal, 2, 3)
check("firesignal delivered", received[1], 5)
disableconnection(connection)
check("disableconnection", isconnectionenabled(connection), false)
firesignal(signal, 9, 9)
check("disabled delivers nothing", #received, 1)
enableconnection(connection)
firesignal(signal, 4, 4)
check("enableconnection", received[2], 8)
check("getsignalarguments", getsignalarguments(connection)[1], 4)

local hook_calls = 0
hooksignal(signal, function(info, a, b)
    hook_calls = hook_calls + 1
    return a == 1 -- only allow this invocation through
end)
check("issignalhooked", issignalhooked(signal), true)
firesignal(signal, 1, 2)
firesignal(signal, 5, 5)
check("hooksignal veto count", hook_calls, 2)
check("hooksignal allowed once", #received, 3)
unhooksignal(signal)
check("unhooksignal", issignalhooked(signal), false)
connection:Disconnect()
check("disconnect", #getconnections(signal), 0)

-- environment --------------------------------------------------------------
local before = #getinstances()
local loose = Instance.new("Folder")
loose.Name = "LooseFolder"
check("getinstances stable", #getinstances(), before)
check("getnilinstances finds loose", #getnilinstances() >= 1, true)
local secure = createsecurefolder("Secure")
check("secure folder", issecuredinstance(secure), true)
local secureListed = false
for _, instance in ipairs(getnilinstances()) do
    if instance == secure then
        secureListed = true
    end
end
check("secure folder hidden", secureListed, false)
check("secured instance query", issecuredinstance(secure), true)
check("setnormalinstance", setnormalinstance(secure), true)
check("normal afterwards", issecuredinstance(secure), false)

local module_script = Instance.new("ModuleScript")
module_script.Name = "TestModule"
module_script.Source = "return 123"
module_script.Parent = workspace
check("getmodules", #getmodules(workspace) >= 1, true)
check("require", require(module_script), 123)
check("getloadedmodules", #getloadedmodules() >= 1, true)
check("getscripts finds", #getscripts() >= 1, true)
check("getrunningscripts", #getrunningscripts(), 1)
check("getcallingscript", getcallingscript() ~= nil, true)
check("getsenv", getsenv(module_script) == getgenv(), true)
check("islocalsourcecontainer", islocalsourcecontainer(module_script), true)
check("getscriptbytecode", type(getscriptbytecode(script)) == "string" and #getscriptbytecode(script) > 0, true)
check("bytecode for other", getscriptbytecode(module_script), nil)

local hidden = gethui()
check("gethui", hidden ~= nil and hidden.Name == "SaintNovaHui", true)

-- instance simulation ------------------------------------------------------
local detector = Instance.new("ClickDetector")
local clicks = 0
detector.MouseClick:Connect(function(player, distance) clicks = clicks + 1 end)
fireclickdetector(detector, 4)
check("fireclickdetector", clicks, 1)

local prompt = Instance.new("ProximityPrompt")
local triggered = 0
prompt.Triggered:Connect(function() triggered = triggered + 1 end)
fireproximityprompt(prompt)
check("fireproximityprompt", triggered, 1)

local part_a = Instance.new("Part")
local part_b = Instance.new("Part")
local touched = 0
part_a.Touched:Connect(function(other) touched = touched + 1 end)
firetouchinterest(part_a, part_b, 0)
check("firetouchinterest", touched, 1)

-- property security --------------------------------------------------------
local guarded = Instance.new("Part")
setscriptable(guarded, "Material", false)
check("isscriptable false", isscriptable(guarded, "Material"), false)
local write_blocked = pcall(function() guarded.Material = "Neon" end)
check("blocked write", write_blocked, false)
setscriptable(guarded, "Material", true)
guarded.Material = "Neon"
check("unblocked write", guarded.Material, "Neon")
protectinstance(guarded)
local protected_write = pcall(function() guarded.Name = "Nope" end)
check("protected write blocked", protected_write, false)

print("smoke complete")
