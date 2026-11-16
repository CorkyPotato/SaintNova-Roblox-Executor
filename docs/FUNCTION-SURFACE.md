# Wave function surface

Wave's function pages document 132 functions; its library pages add 47 more
names. This page records, for every one of them, what SaintNova's local harness
actually does. It is not aspirational: `tests/surface.luau` asserts the table
below, so a function cannot quietly change category.

| Status | Meaning |
| --- | --- |
| **Yes** | Real behaviour. The observable effect is produced by the harness. |
| **Yes\*** | Real behaviour, but the harness environment changes what "real" means. The note says how. |
| **No** | The name exists and raises an error that states why. It never returns a fake success or a silent no-op. |

Totals across 179 documented names: **120 yes, 57 no**. Two further names
(`Socket:Close`, `Socket:Send`) cannot be reached at all, because
`WebSocket.connect` refuses and no socket object can be constructed.

The 57 are not gaps in effort. They are functions whose contract requires a live
Roblox client (renderer, input pipeline, network stack, engine internals), VM
internals the public Luau API does not expose, or memory that a standalone VM
cannot have. Implementing them as plausible-looking stubs would be worse than
refusing: a script that "works" against a fake `Drawing` or a fake `httpget`
fails in a way that is invisible until it runs against a real client.

## Console

| Function | Status | Note |
| --- | --- | --- |
| `rconsolecreate` | Yes\* | Sets the console-open flag. The UI log exists whether or not it is called. |
| `rconsoleclose` | Yes | Clears the flag. |
| `rconsoleclear` | Yes | Emits `console.clear`; the UI clears the log. |
| `rconsoleprint` | Yes | A log line from source `console`. |
| `rconsoleinfo` | Yes | Same, prefixed `[INFO]`. |
| `rconsolewarn` | Yes | Warn-level line, prefixed `[WARN]`. |
| `rconsoleerr` | Yes | Error-level line, prefixed `[ERROR]`. |
| `rconsoledebug` | Yes | Debug-level line, prefixed `[DEBUG]`. |
| `rconsolename` | Yes\* | Stored and emitted as `console.title`; the UI retitles the window. |
| `printconsole` | Yes\* | Prints the text. The `red`/`green`/`blue` arguments are accepted and ignored: log lines are tagged by level, not by colour. |
| `rconsoleinput` | No | Scripts have no interactive stdin. |

## Environment

| Function | Status | Note |
| --- | --- | --- |
| `getinstances` | Yes | Every non-secure instance with a parent. |
| `getnilinstances` | Yes | Every non-secure instance without a parent. |
| `createsecurefolder` | Yes | Creates a hidden `Folder` and marks it secure. |
| `setsecureinstance` | Yes | Hides the instance and its descendants from the query functions. |
| `setnormalinstance` | Yes | Undoes `setsecureinstance`. Returns whether it changed anything. |
| `issecuredinstance` | Yes | True for both secure and protected instances. |
| `protectinstance` | Yes\* | Rejects property writes through `__newindex` with an error. It cannot stop raw writes; nothing in Luau can. |
| `fireclickdetector` | Yes | Fires `MouseClick(localPlayer, distance)`. |
| `fireproximityprompt` | Yes | Fires `Triggered(localPlayer)`. |
| `firetouchinterest` | Yes | `toggle == 1` fires `TouchEnded`, otherwise `Touched`. |
| `addspycallback` | No | There is no remote-event pipeline to spy on. |
| `setsimulationradius` | No | There is no physics simulation. |

## File system

All paths resolve under the workspace root (`%LOCALAPPDATA%\SaintNova\workspace` by
default, or `SAINTNOVA_WORKSPACE`). Absolute paths, drive letters, and `..` are
rejected with an error rather than clamped. Files are capped at 8 MiB, listings
at 10 000 entries.

| Function | Status | Note |
| --- | --- | --- |
| `readfile` | Yes | |
| `writefile` | Yes | |
| `appendfile` | Yes | |
| `isfile` | Yes | |
| `isfolder` | Yes | |
| `makefolder` | Yes | Creates intermediate directories. |
| `delfile` | Yes | Errors when the file is missing. |
| `delfolder` | Yes | Recursive. |
| `listfiles` | Yes | Sorted file names, not full paths. |
| `loadfile` | Yes | Compiles and returns a chunk; the chunk name is the path. |
| `getcustomasset` | No | No Roblox content host is active. |
| `writecustomasset` | No | Same. |

## Hooking

Luau closures are immutable from Luau: there is no supported way to replace the
code or captured values of an existing function object. `hookfunction` therefore
cannot be implemented honestly in this environment, and pretending otherwise
would produce hooks that never fire.

| Function | Status | Note |
| --- | --- | --- |
| `newcclosure` | Yes | Returns a real C closure (a trampoline holding the original as an upvalue). `iscclosure` reports true, as in a live executor. |
| `getcallbackvalue` | Yes | Returns the original for wrappers created by `newcclosure`. |
| `clonefunction` | Yes | Real clone via `lua_clonefunction`; C closures are refused. |
| `newlclosure` | Yes | Clones L closures; wraps C closures in a Luau function. |
| `hookmetamethod` | Yes | Replaces the metamethod in the object's metatable and returns the original. Works on read-only metatables only after `setreadonly(mt, false)`. |
| `isourcallstack` | Yes | Walks `debug.info` levels, looking for `[C]` or sandbox frames. |
| `ishooked` | Yes\* | Always false, and correctly so: no function can be hooked here. |
| `hookfunction` | No | Closures cannot be patched from Luau. |
| `protectclosure` | No | Same reason. |
| `restoreclosure` | No | Same reason. |

## Input

The harness never touches the operator's keyboard or mouse. Simulating input
into a Roblox client that does not exist would be theatre, and simulating it
into the desktop would be a hazard.

| Function | Status | Note |
| --- | --- | --- |
| `isrbxactive` | Yes | False: no Roblox client is attached. |
| `keydown`, `keyup`, `keytap` | No | Requires a live client. |
| `mouse1click`, `mouse1press`, `mouse1release` | No | Same. |
| `mouse2click`, `mouse2press`, `mouse2release` | No | Same. |
| `mousemoverel`, `mousemoveabs`, `mousescroll` | No | Same. |

## Miscellaneous

| Function | Status | Note |
| --- | --- | --- |
| `setclipboard` / `setrbxclipboard` | Yes | Win32 clipboard, Unicode, 4 MiB cap. |
| `getclipboard` | Yes | Returns text; empty clipboard yields `""`. |
| `gethwid` | Yes | `saintnova-` + FNV-1a of `MachineGuid` and the system volume serial. Stable per machine, not reversible. |
| `gethui` | Yes | A secure `Folder` named `SaintNovaHui`, created on first use. |
| `getthreadidentity` / `setthreadidentity` | Yes | Stored integer, 0–15, default 7. |
| `identifyexecutor` | Yes | `SaintNova`, plus the version string. |
| `isscriptable` | Yes | Reflects `setscriptable`. |
| `messagebox` | Yes | Real Win32 dialog, flags 0–6, mapped return values. Blocks the VM until dismissed. |
| `getfflag` | Yes\* | Always `nil`: no engine flags exist here. `nil` means "unknown flag", which is exactly true. |
| `getnamecallmethod` | Yes\* | Always `nil`: there is no namecall hook. |
| `clearteleportqueue` | Yes\* | Nothing is ever queued, so clearing is complete. |
| `getfpscap`, `setfpscap` | No | No renderer. |
| `setfflag` | No | No engine flags. |
| `setnamecallmethod` | No | No namecall hooks. |
| `decompile` | No | No Luau decompiler is bundled. |
| `saveinstance` | No | No RBXL/RBXM serialiser. |
| `getobjects` | No | Requires Roblox asset APIs. |
| `queue_on_teleport` | No | No teleport queue. |
| `httpget`, `request` | No | Networking is disabled in the harness. |
| `zstdcompress`, `zstddecompress` | No | zstd is not bundled. |

## Network

| Function | Status | Note |
| --- | --- | --- |
| `isnetworkowner` | Yes | False. The harness has no network ownership. |

## Reflection

| Function | Status | Note |
| --- | --- | --- |
| `iscclosure` | Yes | True for C closures and `newcclosure` wrappers. |
| `islclosure` | Yes | True for Luau closures. |
| `isexecutorclosure` | Yes | C closures, plus Luau functions defined in the sandbox prelude. |
| `islineinfo` | Yes | `debug.info(fn, "l") ~= -1`. |
| `checkcaller` | Yes | True: every call in this VM is a caller call. |
| `loadstring` | Yes | Compiles and returns a chunk. Bytecode is accepted. |
| `gethiddenproperty` | Yes\* | Ordinary property read. The harness has no separate hidden-property layer, so there is nothing to hide. |
| `sethiddenproperty` | Yes\* | Ordinary property write, same caveat. |
| `setscriptable` | Yes | Blocks writes to the named property; the block raises. |

## Script

| Function | Status | Note |
| --- | --- | --- |
| `getgenv` | Yes | The VM's real globals table. Writes persist across runs until reset. |
| `getrenv` | Yes\* | Same table: the harness has one environment, not a separate privileged one. |
| `gettenv` | Yes\* | Same table. |
| `getrenvglobal` | Yes | `getgenv()[name]`. |
| `getrenvshared` | Yes | `shared[name]`. |
| `getscripts` | Yes | All non-secure `BaseScript` instances. |
| `getmodules` | Yes | `ModuleScript` descendants of a parent, or all when omitted. |
| `getloadedmodules` | Yes\* | Every `ModuleScript`; the harness does not track which have been `require`d. |
| `getrunningscripts` | Yes\* | The single synthetic `LocalScript` that represents the running chunk. |
| `getcallingscript` | Yes | That same `LocalScript`. |
| `getthreadscript` | Yes | That same `LocalScript` for any thread. |
| `getsenv` | Yes\* | The shared globals table; the harness has one env per VM. |
| `getscriptbytecode` | Yes\* | Bytecode of the running chunk only; `nil` for other scripts. |
| `getthreadscript` | Yes | |
| `islocalsourcecontainer` | Yes | `LocalScript` or `ModuleScript`. |
| `require` | Yes | Compiles `ModuleScript.Source`, caches the result, and returns `nil` for modules that return nothing. |
| `getgc`, `filtergc` | No | The Luau heap is not exposed. |
| `getreg` | No | The registry is not exposed. |
| `getscripthash`, `getfunctionhash` | No | Not implemented. |
| `getscriptclosure` | No | The harness does not retain loaded script closures. |
| `setclosureidentity` | No | Closure identities are not modelled. |

## Signal

Firing is synchronous: handlers run inside the `firesignal` call, each one
protected by `pcall`, so one failing handler does not stop the others. The error
is reported through `warn` with the handler's file and line.

| Function | Status | Note |
| --- | --- | --- |
| `getconnections` | Yes | Real connection objects with `Connected`, `Enabled`, `Function`, `Fire`, `Enable`, `Disable`. |
| `firesignal` | Yes | Fires local handlers. |
| `replicatesignal` | Yes\* | Fires local handlers. Nothing is replicated: there is no server. |
| `cansignalreplicate` | Yes | False, for the same reason. |
| `getallreplicatesignals` | Yes\* | Empty table. |
| `disableconnection` / `enableconnection` | Yes | Sets `Enabled`; disabled connections receive nothing. |
| `isconnectionenabled` | Yes | |
| `getsignalarguments` | Yes | The argument table from the connection's most recent delivery. |
| `hooksignal` | Yes | Called before each delivery with `(info, ...)`. Returning anything but `true` vetoes the original handler. A hook that errors vetoes too, and warns. |
| `unhooksignal` | Yes | |
| `issignalhooked` | Yes | |

Signals exist on `Instance` (`ChildAdded`, `ChildRemoved`, `AncestryChanged`,
`Destroying`), `BasePart` (`Touched`, `TouchEnded`), `ClickDetector`
(`MouseClick`, `MouseRightClick`), `ProximityPrompt` (`Triggered`,
`TriggerEnded`), `RemoteEvent` (`OnClientEvent`, `OnServerEvent`), and
`RunService` (`Heartbeat`, `RenderStepped`, `Stepped`). `RunService` signals are
registered but never fire; there is no frame loop to drive them.

## Table

| Function | Status | Note |
| --- | --- | --- |
| `getrawmetatable` | Yes | Tables only; raises for other types. |
| `setrawmetatable` | Yes | Applies the metatable; returns whether it took. |
| `isreadonly` / `iswritable` | Yes | |
| `setreadonly` | Yes | |
| `makereadonly` / `makewritable` | Yes | |

Class tables (`InstanceMetatable`, `Vector3Metatable`, `CFrameMetatable`,
`Signal`) are marked read-only when the prelude finishes, so `Instance.new`,
`Vector3.new`, and friends behave like protected Roblox metatables. A script can
opt in to mutation with `setreadonly(getrawmetatable(part), false)`, exactly as
it would in a live client.

# Libraries

The library pages add 47 names. 26 are implemented, 19 raise a stated error, and
two socket methods are unreachable because no connection can be created.

## Cache

| Function | Status | Note |
| --- | --- | --- |
| `cloneref` | Yes | Returns an alias: a distinct identity, so `alias == original` is **false**, while reads, writes, methods, attributes, and parenting all forward to the same instance. |
| `compareinstances` | Yes | Compares the underlying identity, so an alias and its original are equal. |
| `cache.iscached` | Yes | True while the instance is in the registry and has not been invalidated or replaced. |
| `cache.invalidate` | Yes\* | Drops the instance from the queryable cache: `getinstances`, `getnilinstances`, and the script queries stop returning it. References already held keep working, as in a client where the cache is a lookup. |
| `cache.replace` | Yes\* | Repoints the source's registry slot at the target, so queries that would return the source return the target. References the script already holds keep the old object. |

## Crypt

Everything here is real cryptography through Windows CNG (`bcrypt`), not a
toy cipher. The *formats*, however, are SaintNova's own: Wave does not document
its key derivation or IV handling, so byte-compatibility with it is not
claimed. `crypt.encrypt` output is `16-byte IV || AES-256-CBC(PKCS#7)`, with the
AES key taken as the first 32 bytes of `SHA-384(key)`.

| Function | Status | Note |
| --- | --- | --- |
| `base64_encode` / `base64_decode` | Yes | Standard alphabet and padding. Decoding also accepts URL-safe `-` and `_`, and skips whitespace. |
| `crypt.hash` | Yes | Lowercase hex SHA-384 (96 characters). `crypt.hash("abc")` matches the published digest. |
| `crypt.random` | Yes | CSPRNG string of `[A-Za-z0-9]`, 0–1024 characters. |
| `crypt.derive` | Yes\* | Deterministic SHA-384 counter-mode expansion, 1–1024 bytes: same value and length always give the same key. |
| `crypt.generatebytes` | Yes\* | 48 bytes from `SHA-384(value || 32 random bytes)`. |
| `crypt.generatekey` | Yes\* | 96 hex characters from `SHA-384(value || 16 random bytes)`. |
| `crypt.encrypt` | Yes\* | AES-256-CBC with a fresh random IV per call. |
| `crypt.decrypt` | Yes\* | Inverse of `crypt.encrypt`. A wrong key or truncated ciphertext raises rather than returning junk. |
| `lz4_compress` | No | LZ4 is not bundled. |
| `lz4_decompress` | No | Same. |

## Debug

| Function | Status | Note |
| --- | --- | --- |
| `debug.getinfo` | Yes | Table with `name`, `short_src`, `what` (`Lua`/`C`), `currentline`, `nups`, `func`, `source`. Accepts a function or a stack level. |
| `debug.getupvalue` | Yes | Returns value and name. Works for L and C closures. |
| `debug.getupvalues` | Yes | Table of upvalues; indices with `nil` values are holes. |
| `debug.setupvalue` | Yes | Returns the upvalue name, or `nil` when out of range. |
| `debug.setmetatable` | Yes | Table metatables only. |
| `validlevel` / `isvalidlevel` | Yes | Whether a stack level exists. |
| `debug.getconstant` / `debug.getconstants` | No | Constants are not exposed by Luau's public API. |
| `debug.setconstant` | No | Same. |
| `debug.getproto` / `debug.getprotos` | No | Inner prototypes are not exposed. |
| `debug.setproto` | No | Same. |
| `debug.getregistry` | No | The registry is internal to the VM. |
| `debug.getstack` / `debug.setstack` | No | Frame stacks are not exposed. |

One VM nuance worth knowing: Luau's optimizer folds a captured local that is
never reassigned into a constant inside the closure. The upvalue slot still
exists and reads correctly, but `debug.setupvalue` on it changes nothing
observable. Captures that are reassigned, or captured by two closures, behave
normally. Scripts are compiled at debug level 2 so upvalue and local names are
available to this library.

## Actors

Wave's actor library is Premium-only and models Roblox parallel Luau state. This
harness runs one VM on one thread, so there is no actor state to execute into.

| Function | Status | Note |
| --- | --- | --- |
| `getactors` / `get_actors` | Yes | `Actor` instances present in the model. `Instance.new("Actor")` works. |
| `get_current_actor` | Yes | Nearest `Actor` ancestor of the running script, or `nil`. |
| `is_parallel` | Yes | False: nothing runs in parallel. |
| `get_actor_from_thread` (+ four aliases) | Yes\* | Always `nil`, because no thread belongs to an actor state. |
| `create_comm_channel` | No | Requires parallel actor states. |
| `get_comm_channel` | No | Same. |
| `run_on_actor` | No | Same. |
| `get_deleted_actors` | No | Same. |

## Drawing

| Function | Status | Note |
| --- | --- | --- |
| `cleardrawcache` | Yes\* | No drawing object can be created, so the cache is already empty and clearing it is complete. |
| `isrenderproperty` / `is_render_property` | Yes | False for every value. |
| `isrenderobj` | Yes | False. |
| `Drawing.new` | No | There is no renderer. |
| `getrenderproperty` / `setrenderproperty` | No | Same. |

## WebSocket

| Function | Status | Note |
| --- | --- | --- |
| `WebSocket.connect` | No | Networking is disabled in the harness. |
| `Socket:Close` | Unreachable | No connection object can be created. |
| `Socket:Send` | Unreachable | Same. |

