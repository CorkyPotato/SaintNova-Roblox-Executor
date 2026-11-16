-- Wave surface conformance check.
--
-- Every name documented at duckys-playground.gitbook.io/wave is listed exactly
-- once, as either implemented or explicitly unsupported. The test fails if a
-- name disappears, changes kind, or an unsupported function stops raising a
-- clear error. It is the executable form of docs/FUNCTION-SURFACE.md.

local implemented = {
    -- console
    "rconsoleclear", "rconsoleclose", "rconsolecreate", "rconsoledebug",
    "rconsoleerr", "rconsoleinfo", "rconsolename", "rconsoleprint",
    "printconsole",
    -- environment
    "createsecurefolder", "fireclickdetector", "fireproximityprompt",
    "firetouchinterest", "getinstances", "getnilinstances", "issecuredinstance",
    "protectinstance", "setnormalinstance", "setsecureinstance",
    -- file system
    "appendfile", "delfile", "delfolder", "isfile", "isfolder", "listfiles",
    "loadfile", "makefolder", "readfile", "writefile",
    -- hooking
    "clonefunction", "getcallbackvalue", "hookmetamethod", "ishooked",
    "isourcallstack", "newcclosure", "newlclosure",
    -- input
    "isrbxactive",
    -- miscellaneous
    "clearteleportqueue", "getclipboard", "getfflag", "gethui", "gethwid",
    "getnamecallmethod", "getthreadidentity", "isscriptable", "messagebox",
    "setclipboard", "setrbxclipboard", "setthreadidentity", "identifyexecutor",
    -- network
    "isnetworkowner",
    -- reflection
    "checkcaller", "gethiddenproperty", "isexecutorclosure", "iscclosure",
    "islineinfo", "islclosure", "loadstring", "sethiddenproperty",
    "setscriptable",
    -- script
    "getcallingscript", "getgenv", "getloadedmodules", "getmodules", "getrenv",
    "getrenvglobal", "getrenvshared", "getrunningscripts", "getscripts",
    "getscriptbytecode", "getsenv", "gettenv", "getthreadscript",
    "islocalsourcecontainer", "require",
    -- signal
    "cansignalreplicate", "disableconnection", "enableconnection", "firesignal",
    "getallreplicatesignals", "getconnections", "getsignalarguments",
    "hooksignal", "isconnectionenabled", "issignalhooked", "replicatesignal",
    "unhooksignal",
    -- table
    "getrawmetatable", "isreadonly", "iswritable", "makereadonly",
    "makewritable", "setrawmetatable", "setreadonly",
}

local unsupported = {
    -- console
    "rconsoleinput",
    -- environment
    "addspycallback", "setsimulationradius",
    -- file system
    "getcustomasset", "writecustomasset",
    -- hooking
    "hookfunction", "protectclosure", "restoreclosure",
    -- input
    "keydown", "keytap", "keyup", "mouse1click", "mouse1press",
    "mouse1release", "mousescroll", "mousemoverel", "mousemoveabs",
    "mouse2click", "mouse2press", "mouse2release",
    -- miscellaneous
    "decompile", "getfpscap", "getobjects", "httpget", "queue_on_teleport",
    "request", "saveinstance", "setfflag", "setfpscap", "setnamecallmethod",
    "zstdcompress", "zstddecompress",
    -- script
    "filtergc", "getgc", "getfunctionhash", "getscriptclosure", "getscripthash",
    "setclosureidentity",
}

-- Libraries (docs/wave/libs). Dotted names resolve through _G.
local libraryImplemented = {
    -- cache
    "cloneref", "compareinstances", "cache.invalidate", "cache.iscached",
    "cache.replace",
    -- crypt
    "base64_encode", "base64_decode", "crypt.hash", "crypt.random",
    "crypt.generatebytes", "crypt.generatekey", "crypt.derive", "crypt.encrypt",
    "crypt.decrypt",
    -- debug
    "debug.getinfo", "debug.getupvalue", "debug.getupvalues",
    "debug.setupvalue", "debug.setmetatable", "validlevel",
    -- actors
    "getactors", "get_actor_from_thread", "get_current_actor", "is_parallel",
    -- drawing
    "cleardrawcache", "isrenderproperty",
}

local libraryUnsupported = {
    -- actors: parallel actor states do not exist in one VM
    "create_comm_channel", "get_comm_channel", "get_deleted_actors",
    "run_on_actor",
    -- crypt: no LZ4 implementation is bundled
    "lz4_compress", "lz4_decompress",
    -- debug: needs VM internals the public API does not expose
    "debug.getconstant", "debug.getconstants", "debug.getproto",
    "debug.getprotos", "debug.getregistry", "debug.getstack",
    "debug.setconstant", "debug.setproto", "debug.setstack",
    -- drawing: no renderer
    "Drawing.new", "getrenderproperty", "setrenderproperty",
    -- network
    "WebSocket.connect",
}

local failures = 0
local function fail(message)
    failures = failures + 1
    print("FAIL " .. message)
end

local function resolve(path)
    local node = _G
    for part in string.gmatch(path, "[^%.]+") do
        if type(node) ~= "table" then
            return nil
        end
        node = node[part]
    end
    return node
end

local seen = {}
local function checkImplemented(name)
    if seen[name] then
        fail("duplicate listing: " .. name)
    end
    seen[name] = true
    local value = resolve(name)
    if type(value) ~= "function" then
        fail("implemented function missing: " .. name .. " (" .. type(value) .. ")")
    end
end

local function checkUnsupported(name)
    if seen[name] then
        fail("listed as both implemented and unsupported: " .. name)
    end
    seen[name] = true
    local value = resolve(name)
    if type(value) ~= "function" then
        fail("unsupported function missing: " .. name .. " (" .. type(value) .. ")")
        return
    end
    local ok, err = pcall(value)
    if ok then
        fail("unsupported function did not raise: " .. name)
    elseif type(err) ~= "string" or not err:find("not available", 1, true) then
        fail("unsupported function lacks a reason: " .. name .. " -> " .. tostring(err))
    end
end

for _, name in ipairs(implemented) do checkImplemented(name) end
for _, name in ipairs(libraryImplemented) do checkImplemented(name) end
for _, name in ipairs(unsupported) do checkUnsupported(name) end
for _, name in ipairs(libraryUnsupported) do checkUnsupported(name) end

local documented = 0
for _ in pairs(seen) do documented = documented + 1 end
local expected = #implemented + #libraryImplemented + #unsupported + #libraryUnsupported
if documented ~= expected then
    fail("surface bookkeeping is inconsistent")
end

-- Spot-check real behaviour, not just arity.
local function expect(name, actual, wanted)
    if actual ~= wanted then
        fail(name .. ": expected " .. tostring(wanted) .. ", got " .. tostring(actual))
    end
end

expect("iscclosure(print)", iscclosure(print), true)
expect("islclosure(function)", islclosure(function() end), true)
expect("checkcaller", checkcaller(), true)
expect("isrbxactive", isrbxactive(), false)
expect("isnetworkowner", isnetworkowner(workspace), false)
expect("cansignalreplicate", cansignalreplicate(Signal.new()), false)
expect("getfflag", getfflag("DFIntMissing"), nil)
expect("getnamecallmethod", getnamecallmethod(), nil)
expect("ishooked", ishooked(print), false)
expect("type(gethwid)", type(gethwid()), "string")
expect("type(identifyexecutor())", type(identifyexecutor()), "string")
expect("getthreadidentity", getthreadidentity(), 7)

local raw = setmetatable({}, { __index = function() return 1 end })
expect("isreadonly(default)", isreadonly(raw), false)
makereadonly(raw)
expect("makereadonly", isreadonly(raw), true)
makewritable(raw)
expect("makereadonly undone", isreadonly(raw), false)

local loadable = loadstring("return 6 * 7")
expect("loadstring", loadable(), 42)

-- Library behaviour worth pinning here as well as in libs_smoke.
local part = Instance.new("Part")
local alias = cloneref(part)
expect("cloneref identity", alias == part, false)
expect("compareinstances", compareinstances(alias, part), true)
expect("base64", base64_decode(base64_encode("surface")), "surface")
expect("hash length", #crypt.hash("x"), 96)
expect("crypt roundtrip", crypt.decrypt(crypt.encrypt("x", "k"), "k"), "x")
expect("is_parallel", is_parallel(), false)

print(("surface: %d implemented, %d explicitly unsupported, %d callable, %d documented")
    :format(#implemented + #libraryImplemented, #unsupported + #libraryUnsupported, documented, documented + 2))

if failures > 0 then
    error(("%d surface checks failed"):format(failures), 0)
end
print("surface complete")
