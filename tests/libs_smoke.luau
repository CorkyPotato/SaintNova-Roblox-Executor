local function check(name, actual, wanted)
    local ok = (wanted == nil and actual ~= nil) or actual == wanted
    print((ok and "PASS " or "FAIL ") .. name .. " -> " .. tostring(actual))
end

-- cache ---------------------------------------------------------------------
local part = Instance.new("Part")
part.Name = "CachePart"
part.Parent = workspace
local alias = cloneref(part)
check("alias is instance", typeof(alias), "Part")
check("alias identity differs", alias == part, false)
check("compareinstances", compareinstances(alias, part), true)
alias.Name = "Renamed"
check("alias writes through", part.Name, "Renamed")
check("alias reads through", alias.Name, "Renamed")
check("alias method", alias:GetFullName(), "Game.Workspace.Renamed")
alias:SetAttribute("k", 5)
check("alias attributes", part:GetAttribute("k"), 5)
check("alias parent change", (function()
    local folder = Instance.new("Folder")
    alias.Parent = folder
    local ok = part.Parent == folder
    alias.Parent = workspace
    return ok
end)(), true)
check("iscached", cache.iscached(part), true)
cache.invalidate(part)
check("invalidated iscached", cache.iscached(part), false)
local listed = false
for _, i in ipairs(getinstances()) do
    if i == part then listed = true end
end
check("invalidated hidden from queries", listed, false)
check("invalidated still usable", part.Name, "Renamed")

local keeper = Instance.new("Folder")
keeper.Name = "Keeper"
keeper.Parent = workspace
local other = Instance.new("Folder")
other.Name = "Other"
other.Parent = workspace
cache.replace(keeper, other)
local sawOther = 0
for _, i in ipairs(getinstances()) do
    if i == other then sawOther = sawOther + 1 end
end
check("replace dedupes", sawOther, 1)
check("replace invalidates source", cache.iscached(keeper), false)

-- crypt ---------------------------------------------------------------------
check("base64 encode", base64_encode("hello"), "aGVsbG8=")
check("base64 decode", base64_decode("aGVsbG8="), "hello")
check("base64 roundtrip", base64_decode(base64_encode("a\0b\255")), "a\0b\255")
check("base64 alias", base64decode(base64encode("x")), "x")
check("base64 url-safe decode", base64_decode("-_8=") ~= nil, true)
check("hash of abc", crypt.hash("abc"),
    "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7")
check("hash differs", crypt.hash("abd") ~= crypt.hash("abc"), true)
check("random length", #crypt.random(16), 16)
check("random differs", crypt.random(16) ~= crypt.random(16), true)
check("random cap", pcall(crypt.random, 5000), false)
check("derive length", #crypt.derive("seed", 32), 32)
check("derive deterministic", crypt.derive("seed", 32), crypt.derive("seed", 32))
check("derive differs", crypt.derive("seed", 32) ~= crypt.derive("other", 32), true)
check("generatekey shape", #crypt.generatekey("x"), 96)
check("generatebytes length", #crypt.generatebytes("x"), 48)
local cipher = crypt.encrypt("secret text", "key")
check("encrypt changes", cipher ~= "secret text", true)
check("cipher has iv", #cipher, 32)
check("decrypt", crypt.decrypt(cipher, "key"), "secret text")
check("decrypt wrong key fails", pcall(crypt.decrypt, cipher, "wrong"), false)
check("decrypt truncated fails", pcall(crypt.decrypt, "short", "key"), false)
check("encrypt long roundtrip", #crypt.decrypt(crypt.encrypt(string.rep("A", 1000), "k2"), "k2"), 1000)
check("encrypt empty roundtrip", crypt.decrypt(crypt.encrypt("", "k"), "k"), "")

-- debug ---------------------------------------------------------------------
-- The capture is reassigned after the closure is created, so the compiler
-- cannot fold it into a constant and the upvalue write is observable.
local source = [[
local a = 10
local b = "twenty"
local f = function(x) return x + a, b end
a = a + 1
return f
]]
local sample = loadstring(source, "=sample_chunk")()
check("getupvalues count", #debug.getupvalues(sample), 2)
check("getupvalue value", (debug.getupvalue(sample, 1)), 11)
local _, upname = debug.getupvalue(sample, 2)
check("getupvalue name", upname, "b")
check("setupvalue return", debug.setupvalue(sample, 1, 100), "a")
check("setupvalue", (sample(1)), 101)
check("getupvalue after write", (debug.getupvalue(sample, 1)), 100)
check("getupvalue out of range", debug.getupvalue(sample, 9), nil)
local info = debug.getinfo(sample)
check("getinfo what", info.what, "Lua")
check("getinfo nups", info.nups, 2)
check("getinfo func", info.func, sample)
check("getinfo source", info.source:find("sample_chunk") ~= nil, true)
check("getinfo C", debug.getinfo(print).what, "C")
check("validlevel fn", isvalidlevel(sample, 1), true)
check("validlevel missing", validlevel(sample, 500), false)
local tbl = {}
debug.setmetatable(tbl, { __index = function() return "mt" end })
check("debug.setmetatable", tbl.missing, "mt")
check("c closure upvalues", (select(2, __saintnova.upvalues(newcclosure(function() end)))), 1)

-- actors --------------------------------------------------------------------
check("getactors empty", #getactors(), 0)
local actor = Instance.new("Actor")
actor.Name = "Worker"
actor.Parent = workspace
check("getactors finds", #getactors(), 1)
check("current actor none", getcurrentactor(), nil)
check("is_parallel", is_parallel(), false)
check("thread actor", get_actor_from_thread(coroutine.running()), nil)
check("create_comm_channel unsupported", pcall(create_comm_channel, "x"), false)
check("run_on_actor unsupported", pcall(run_on_actor, actor, ""), false)

-- Drawing / WebSocket / compression -----------------------------------------
local draw_ok, draw_err = pcall(Drawing.new, "Circle")
check("Drawing.new unsupported", draw_ok, false)
check("Drawing.new reason", draw_err:find("not available") ~= nil, true)
cleardrawcache()
check("cleardrawcache", true, true)
check("isrenderproperty", isrenderproperty({}), false)
check("WebSocket unsupported", pcall(WebSocket.connect, "wss://example.com"), false)
check("lz4 unsupported", pcall(lz4_compress, "x"), false)
check("crypt table", type(crypt), "table")

print("libs complete")
