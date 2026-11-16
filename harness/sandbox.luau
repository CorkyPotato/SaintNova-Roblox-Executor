-- SaintNova harness prelude
--
-- Runs once in the local Luau VM before user code. It builds a small,
-- deliberately honest subset of the Roblox API on top of the primitives the
-- host provides (print, warn, task, loadstring).
--
-- What exists here behaves for real: instances parent and clone, vectors do
-- vector math, the scheduler drives task.wait/spawn/delay. What does not exist
-- (rendering, physics, networking, decompilation) raises an error that says so,
-- instead of returning a plausible-looking placeholder.

local nativeTypeof = typeof
local nativeGetMetatable = getmetatable
local nativeSetMetatable = setmetatable
local nativeRawGet = rawget
local nativeRawSet = rawset
local nativeRawEqual = rawequal
local nativeType = type
local nativeTostring = tostring
local nativeTonumber = tonumber
local nativePcall = pcall
local nativeError = error
local nativeSelect = select
local tableInsert = table.insert
local tableRemove = table.remove
local tableConcat = table.concat
local tableUnpack = table.unpack
local tablePack = table.pack
local debugInfo = debug.info
local mathSqrt = math.sqrt
local mathHuge = math.huge
local nativeSetReadonly = setreadonly

-- ---------------------------------------------------------------------------
-- Vector3
-- ---------------------------------------------------------------------------

local Vector3Methods = {}
local Vector3Metatable

local function newVector3(x, y, z)
    return nativeSetMetatable({ X = x or 0, Y = y or 0, Z = z or 0 }, Vector3Metatable)
end

local function vectorOperand(value)
    if nativeType(value) == "number" then
        return value, value, value
    end
    return value.X, value.Y, value.Z
end

Vector3Metatable = {
    __index = function(self, key)
        local method = Vector3Methods[key]
        if method ~= nil then
            return method
        end
        if key == "Magnitude" then
            return mathSqrt(self.X * self.X + self.Y * self.Y + self.Z * self.Z)
        elseif key == "Unit" then
            local magnitude = mathSqrt(self.X * self.X + self.Y * self.Y + self.Z * self.Z)
            if magnitude == 0 then
                return newVector3(0, 0, 0)
            end
            return newVector3(self.X / magnitude, self.Y / magnitude, self.Z / magnitude)
        end
        return nil
    end,
    __add = function(a, b)
        local bx, by, bz = vectorOperand(b)
        return newVector3(a.X + bx, a.Y + by, a.Z + bz)
    end,
    __sub = function(a, b)
        local bx, by, bz = vectorOperand(b)
        return newVector3(a.X - bx, a.Y - by, a.Z - bz)
    end,
    __mul = function(a, b)
        local bx, by, bz = vectorOperand(b)
        return newVector3(a.X * bx, a.Y * by, a.Z * bz)
    end,
    __div = function(a, b)
        local bx, by, bz = vectorOperand(b)
        return newVector3(a.X / bx, a.Y / by, a.Z / bz)
    end,
    __unm = function(a)
        return newVector3(-a.X, -a.Y, -a.Z)
    end,
    __eq = function(a, b)
        return a.X == b.X and a.Y == b.Y and a.Z == b.Z
    end,
    __tostring = function(v)
        return string.format("%s, %s, %s", nativeTostring(v.X), nativeTostring(v.Y), nativeTostring(v.Z))
    end,
}

function Vector3Methods:Dot(other)
    return self.X * other.X + self.Y * other.Y + self.Z * other.Z
end

function Vector3Methods:Cross(other)
    return newVector3(self.Y * other.Z - self.Z * other.Y, self.Z * other.X - self.X * other.Z,
        self.X * other.Y - self.Y * other.X)
end

function Vector3Methods:Lerp(other, alpha)
    return newVector3(self.X + (other.X - self.X) * alpha, self.Y + (other.Y - self.Y) * alpha,
        self.Z + (other.Z - self.Z) * alpha)
end

function Vector3Methods:FuzzyEq(other, epsilon)
    epsilon = epsilon or 1e-5
    return math.abs(self.X - other.X) <= epsilon and math.abs(self.Y - other.Y) <= epsilon
        and math.abs(self.Z - other.Z) <= epsilon
end

local Vector3 = {}
Vector3.__index = Vector3Metatable
Vector3.zero = newVector3(0, 0, 0)
Vector3.one = newVector3(1, 1, 1)
Vector3.xAxis = newVector3(1, 0, 0)
Vector3.yAxis = newVector3(0, 1, 0)
Vector3.zAxis = newVector3(0, 0, 1)

function Vector3.new(x, y, z)
    return newVector3(nativeTonumber(x) or 0, nativeTonumber(y) or 0, nativeTonumber(z) or 0)
end

-- ---------------------------------------------------------------------------
-- CFrame (translation only; rotation is not simulated)
-- ---------------------------------------------------------------------------

local CFrameMethods = {}
local CFrameMetatable

local function newCFrame(x, y, z)
    x = nativeTonumber(x) or 0
    y = nativeTonumber(y) or 0
    z = nativeTonumber(z) or 0
    return nativeSetMetatable({ _position = newVector3(x, y, z) }, CFrameMetatable)
end

CFrameMetatable = {
    __index = function(self, key)
        local method = CFrameMethods[key]
        if method ~= nil then
            return method
        end
        local position = nativeRawGet(self, "_position")
        if key == "Position" or key == "p" then
            return position
        elseif key == "X" then
            return position.X
        elseif key == "Y" then
            return position.Y
        elseif key == "Z" then
            return position.Z
        end
        return nil
    end,
    __add = function(cf, vector)
        local p = nativeRawGet(cf, "_position")
        return newCFrame(p.X + vector.X, p.Y + vector.Y, p.Z + vector.Z)
    end,
    __sub = function(cf, vector)
        local p = nativeRawGet(cf, "_position")
        return newCFrame(p.X - vector.X, p.Y - vector.Y, p.Z - vector.Z)
    end,
    __mul = function(a, b)
        if nativeGetMetatable(b) == CFrameMetatable then
            local pa = nativeRawGet(a, "_position")
            local pb = nativeRawGet(b, "_position")
            return newCFrame(pa.X + pb.X, pa.Y + pb.Y, pa.Z + pb.Z)
        end
        local p = nativeRawGet(a, "_position")
        return newVector3(p.X + b.X, p.Y + b.Y, p.Z + b.Z)
    end,
    __tostring = function(cf)
        local p = nativeRawGet(cf, "_position")
        return string.format("%s, %s, %s", nativeTostring(p.X), nativeTostring(p.Y), nativeTostring(p.Z))
    end,
}

function CFrameMethods:ToWorldSpace(other)
    return self * other
end

function CFrameMethods:PointToWorldSpace(point)
    return self * point
end

function CFrameMethods:Lerp(other, alpha)
    local a = nativeRawGet(self, "_position")
    local b = nativeRawGet(other, "_position")
    return newCFrame(a.X + (b.X - a.X) * alpha, a.Y + (b.Y - a.Y) * alpha, a.Z + (b.Z - a.Z) * alpha)
end

local CFrame = {}
CFrame.identity = newCFrame(0, 0, 0)

function CFrame.new(x, y, z)
    if nativeType(x) == "table" and nativeRawGet(x, "_position") ~= nil then
        local p = nativeRawGet(x, "_position")
        return newCFrame(p.X, p.Y, p.Z)
    end
    return newCFrame(x, y, z)
end

function CFrame.Angles()
    return newCFrame(0, 0, 0)
end

function CFrame.lookAt(from, to)
    return newCFrame(to.X, to.Y, to.Z)
end

-- ---------------------------------------------------------------------------
-- Color3 / UDim / UDim2
-- ---------------------------------------------------------------------------

local Color3 = {}
Color3.__index = Color3

local function newColor3(r, g, b)
    return nativeSetMetatable({ R = r or 0, G = g or 0, B = b or 0 }, Color3)
end

function Color3.new(r, g, b)
    return newColor3(nativeTonumber(r) or 0, nativeTonumber(g) or 0, nativeTonumber(b) or 0)
end

function Color3.fromRGB(r, g, b)
    return newColor3((nativeTonumber(r) or 0) / 255, (nativeTonumber(g) or 0) / 255, (nativeTonumber(b) or 0) / 255)
end

function Color3.fromHSV(h, s, v)
    return newColor3(h or 0, s or 0, v or 0)
end

function Color3:ToHex()
    local function channel(value)
        local byte = math.floor(math.clamp(value, 0, 1) * 255 + 0.5)
        return string.format("%02X", byte)
    end
    return channel(self.R) .. channel(self.G) .. channel(self.B)
end

local UDim = {}
UDim.__index = UDim

function UDim.new(scale, offset)
    return nativeSetMetatable({ Scale = scale or 0, Offset = offset or 0 }, UDim)
end

local UDim2 = {}
UDim2.__index = UDim2

function UDim2.new(xScale, xOffset, yScale, yOffset)
    return nativeSetMetatable({
        X = UDim.new(xScale, xOffset),
        Y = UDim.new(yScale, yOffset),
    }, UDim2)
end

function UDim2.fromScale(x, y)
    return UDim2.new(x, 0, y, 0)
end

function UDim2.fromOffset(x, y)
    return UDim2.new(0, x, 0, y)
end

-- ---------------------------------------------------------------------------
-- Signals
-- ---------------------------------------------------------------------------

local Signal = {}
Signal.__index = Signal

function Signal.new()
    return nativeSetMetatable({ _handlers = {}, _hook = nil, _lastArgs = nil }, Signal)
end

local function copyArguments(...)
    return tablePack(...)
end

function Signal:Connect(callback)
    if nativeType(callback) ~= "function" then
        nativeError("Connect expects a function", 2)
    end
    local connection = {
        Connected = true,
        Enabled = true,
        Function = callback,
        _callback = callback,
        _signal = self,
        _lastArgs = nil,
    }
    function connection:Disconnect()
        if not self.Connected then
            return
        end
        self.Connected = false
        local handlers = self._signal._handlers
        for index = #handlers, 1, -1 do
            if handlers[index] == self then
                tableRemove(handlers, index)
            end
        end
    end
    function connection:Enable()
        self.Enabled = true
    end
    function connection:Disable()
        self.Enabled = false
    end
    function connection:Fire(...)
        if not self.Connected or not self.Enabled then
            return
        end
        local args = copyArguments(...)
        self._lastArgs = args
        local ok, err = nativePcall(self.Function, tableUnpack(args, 1, args.n))
        if not ok then
            warn("signal handler error: " .. nativeTostring(err))
        end
    end
    tableInsert(self._handlers, connection)
    return connection
end

function Signal:Once(callback)
    local connection
    connection = self:Connect(function(...)
        connection:Disconnect()
        callback(...)
    end)
    return connection
end

-- Fires handlers synchronously. Each handler is protected so one failing
-- callback cannot abort the rest, matching Roblox's per-connection error
-- handling. `hooksignal` sees every invocation and can veto the original.
function Signal:Fire(...)
    local args = copyArguments(...)
    self._lastArgs = args

    local snapshot = {}
    for index, connection in ipairs(self._handlers) do
        snapshot[index] = connection
    end

    local hook = self._hook
    for _, connection in ipairs(snapshot) do
        if connection.Connected and connection.Enabled then
            connection._lastArgs = args
            local invokeOriginal = true
            if hook ~= nil then
                local info = {
                    Function = connection.Function,
                    Connection = connection,
                    Enabled = connection.Enabled,
                    Signal = self,
                }
                local ok, allow = nativePcall(hook, info, tableUnpack(args, 1, args.n))
                if not ok then
                    warn("hooksignal callback error: " .. nativeTostring(allow))
                    invokeOriginal = false
                elseif allow ~= true then
                    invokeOriginal = false
                end
            end
            if invokeOriginal then
                local ok, err = nativePcall(connection.Function, tableUnpack(args, 1, args.n))
                if not ok then
                    warn("signal handler error: " .. nativeTostring(err))
                end
            end
        end
    end
end

function Signal:GetConnections()
    local out = {}
    for _, connection in ipairs(self._handlers) do
        tableInsert(out, connection)
    end
    return out
end

-- ---------------------------------------------------------------------------
-- Instances
-- ---------------------------------------------------------------------------

local CLASS_PARENTS = {
    Part = "BasePart",
    MeshPart = "BasePart",
    WedgePart = "BasePart",
    SpawnLocation = "BasePart",
    BasePart = "PVInstance",
    Model = "PVInstance",
    PVInstance = "Instance",
    Player = "Instance",
    LocalScript = "Script",
    Script = "BaseScript",
    ModuleScript = "BaseScript",
    BaseScript = "LuaSourceContainer",
    LuaSourceContainer = "Instance",
    Humanoid = "Instance",
    HumanoidRootPart = "Part",
    Folder = "Instance",
    Actor = "Instance",
    ClickDetector = "Instance",
    ProximityPrompt = "Instance",
    RemoteEvent = "Instance",
    RemoteFunction = "Instance",
    BindableEvent = "Instance",
    BindableFunction = "Instance",
    Workspace = "WorldRoot",
    WorldRoot = "Model",
    Camera = "Instance",
    DataModel = "ServiceProvider",
    ServiceProvider = "Instance",
    ScreenGui = "LayerCollector",
    SurfaceGui = "LayerCollector",
    LayerCollector = "Instance",
    TextLabel = "GuiObject",
    TextButton = "GuiObject",
    Frame = "GuiObject",
    GuiObject = "GuiBase2d",
    GuiBase2d = "Instance",
}

local Instance = {}
local InstanceMetatable = {}
local instanceById = {}
local nextInstanceId = 0

-- Instances marked secure are hidden from getinstances/getnilinstances and
-- the script queries, but remain reachable through the reference a script
-- already holds. Protected instances reject property writes.
local securedInstances = nativeSetMetatable({}, { __mode = "k" })
local protectedInstances = nativeSetMetatable({}, { __mode = "k" })
local unscriptableProps = nativeSetMetatable({}, { __mode = "k" })

-- `cloneref` returns an alias: a distinct table identity that forwards every
-- read, write, and method call to the same underlying instance. The registry
-- only ever holds canonical instances, so queries keep returning the original.
local instanceAliasTarget = nativeSetMetatable({}, { __mode = "k" })

-- Instances removed from the executor's cache view by cache.invalidate.
local cacheInvalid = nativeSetMetatable({}, { __mode = "k" })

local DEFAULTS = {
    Part = { Size = Vector3.new(4, 1, 2), Anchored = false, CanCollide = true, Transparency = 0, Material = "Plastic" },
    MeshPart = { Size = Vector3.new(4, 1, 2), Anchored = false, Transparency = 0 },
    Humanoid = { Health = 100, MaxHealth = 100, WalkSpeed = 16, JumpPower = 50, JumpHeight = 7.2 },
    Player = { UserId = 0, DisplayName = "Player", AccountAge = 0 },
    TextLabel = { Text = "", Visible = true, TextSize = 14 },
    TextButton = { Text = "", Visible = true, TextSize = 14 },
    Frame = { Visible = true, BackgroundTransparency = 0 },
    Camera = { FieldOfView = 70, CameraType = "Custom" },
    ModuleScript = { Source = "" },
    Script = { Source = "", Disabled = false },
    LocalScript = { Source = "", Disabled = false },
    ClickDetector = { MaxActivationDistance = 32 },
    ProximityPrompt = {
        Enabled = true,
        ActionText = "Interact",
        ObjectText = "Use",
        HoldDuration = 0,
        MaxActivationDistance = 10,
    },
}

local function deref(value)
    return instanceAliasTarget[value] or value
end

local function isInstance(value)
    return nativeType(value) == "table"
        and (nativeGetMetatable(value) == InstanceMetatable or instanceAliasTarget[value] ~= nil)
end

local function propsOf(instance)
    return nativeRawGet(deref(instance), "_props")
end

local function classOf(instance)
    return nativeRawGet(deref(instance), "_class")
end

local function childList(instance)
    return nativeRawGet(deref(instance), "_children")
end

local function detach(instance)
    local target = deref(instance)
    local parent = nativeRawGet(target, "_parent")
    if parent == nil then
        return
    end
    local siblings = childList(parent)
    for index = #siblings, 1, -1 do
        if siblings[index] == target then
            tableRemove(siblings, index)
            break
        end
    end
    nativeRawSet(target, "_parent", nil)
end

InstanceMetatable.__index = function(self, key)
    local method = Instance[key]
    if method ~= nil then
        return method
    end

    if key == "ClassName" then
        return classOf(self)
    elseif key == "Parent" then
        return nativeRawGet(deref(self), "_parent")
    elseif key == "Children" then
        local copy = {}
        for index, child in ipairs(childList(self)) do
            copy[index] = child
        end
        return copy
    end

    local value = propsOf(self)[key]
    if value ~= nil then
        return value
    end

    local defaults = DEFAULTS[classOf(self)]
    return defaults and defaults[key]
end

InstanceMetatable.__newindex = function(self, key, value)
    local target = deref(self)
    if protectedInstances[target] then
        nativeError("instance is protected: " .. (propsOf(target).Name or classOf(target)), 2)
    end
    local restricted = unscriptableProps[target]
    if restricted ~= nil and restricted[key] then
        nativeError("property '" .. nativeTostring(key) .. "' is not scriptable", 2)
    end

    if key == "Parent" then
        if value ~= nil and not isInstance(value) then
            nativeError("Parent must be an Instance or nil", 2)
        end
        detach(target)
        if value ~= nil then
            nativeRawSet(target, "_parent", deref(value))
            tableInsert(childList(value), target)
        end
        return
    end
    propsOf(target)[key] = value
end

InstanceMetatable.__tostring = function(self)
    return propsOf(self).Name or classOf(self)
end

-- No __eq: instance equality is table identity, exactly as userdata equality
-- works in a client. cloneref deliberately returns a distinct identity, and
-- compareinstances exists to look through that.

local function classNameIsA(current, target)
    while current ~= nil do
        if current == target then
            return true
        end
        current = CLASS_PARENTS[current]
    end
    return target == "Instance"
end

function Instance.new(className, parent)
    className = className or "Instance"
    nextInstanceId = nextInstanceId + 1

    local instance = nativeSetMetatable({
        _id = nextInstanceId,
        _class = className,
        _children = {},
        _props = { Name = className },
        _attributes = {},
        _destroyed = false,
        ChildAdded = Signal.new(),
        ChildRemoved = Signal.new(),
        AncestryChanged = Signal.new(),
        Destroying = Signal.new(),
    }, InstanceMetatable)

    local defaults = DEFAULTS[className]
    if defaults ~= nil then
        for key, value in pairs(defaults) do
            propsOf(instance)[key] = value
        end
    end

    if classNameIsA(className, "BasePart") then
        propsOf(instance).Touched = Signal.new()
        propsOf(instance).TouchEnded = Signal.new()
    elseif className == "ClickDetector" then
        propsOf(instance).MouseClick = Signal.new()
        propsOf(instance).MouseRightClick = Signal.new()
    elseif className == "ProximityPrompt" then
        propsOf(instance).Triggered = Signal.new()
        propsOf(instance).TriggerEnded = Signal.new()
    elseif className == "RemoteEvent" then
        propsOf(instance).OnClientEvent = Signal.new()
        propsOf(instance).OnServerEvent = Signal.new()
    end

    instanceById[nextInstanceId] = instance

    if parent ~= nil then
        instance.Parent = parent
    end
    return instance
end

function Instance:IsA(className)
    return classNameIsA(classOf(self), className)
end

function Instance:GetChildren()
    local out = {}
    for index, child in ipairs(childList(self)) do
        out[index] = child
    end
    return out
end

function Instance:GetDescendants()
    local out = {}
    local function walk(node)
        for _, child in ipairs(childList(node)) do
            tableInsert(out, child)
            walk(child)
        end
    end
    walk(self)
    return out
end

function Instance:FindFirstChild(name, recursive)
    for _, child in ipairs(childList(self)) do
        if propsOf(child).Name == name then
            return child
        end
    end
    if recursive then
        for _, child in ipairs(childList(self)) do
            local found = child:FindFirstChild(name, true)
            if found ~= nil then
                return found
            end
        end
    end
    return nil
end

function Instance:FindFirstChildOfClass(className)
    for _, child in ipairs(childList(self)) do
        if classOf(child) == className then
            return child
        end
    end
    return nil
end

function Instance:FindFirstChildWhichIsA(className, recursive)
    for _, child in ipairs(childList(self)) do
        if child:IsA(className) then
            return child
        end
    end
    if recursive then
        for _, child in ipairs(childList(self)) do
            local found = child:FindFirstChildWhichIsA(className, true)
            if found ~= nil then
                return found
            end
        end
    end
    return nil
end

function Instance:FindFirstAncestor(name)
    local parent = nativeRawGet(deref(self), "_parent")
    while parent ~= nil do
        if propsOf(parent).Name == name then
            return parent
        end
        parent = nativeRawGet(parent, "_parent")
    end
    return nil
end

function Instance:IsDescendantOf(ancestor)
    local target = deref(ancestor)
    local parent = nativeRawGet(deref(self), "_parent")
    while parent ~= nil do
        if parent == target then
            return true
        end
        parent = nativeRawGet(parent, "_parent")
    end
    return false
end

function Instance:WaitForChild(name, timeout)
    local deadline = os.clock() + (timeout or 5)
    while true do
        local found = self:FindFirstChild(name)
        if found ~= nil then
            return found
        end
        if os.clock() >= deadline then
            return nil
        end
        task.wait(0.05)
    end
end

function Instance:GetFullName()
    local parts = { propsOf(self).Name or classOf(self) }
    local parent = nativeRawGet(deref(self), "_parent")
    while parent ~= nil do
        tableInsert(parts, 1, propsOf(parent).Name or classOf(parent))
        parent = nativeRawGet(parent, "_parent")
    end
    return table.concat(parts, ".")
end

function Instance:SetAttribute(name, value)
    nativeRawGet(deref(self), "_attributes")[name] = value
end

function Instance:GetAttribute(name)
    return nativeRawGet(deref(self), "_attributes")[name]
end

function Instance:GetAttributes()
    local out = {}
    for key, value in pairs(nativeRawGet(deref(self), "_attributes")) do
        out[key] = value
    end
    return out
end

function Instance:Clone()
    local copy = Instance.new(classOf(self))
    for key, value in pairs(propsOf(self)) do
        if key ~= "Name" and nativeGetMetatable(value) ~= Signal then
            propsOf(copy)[key] = value
        end
    end
    propsOf(copy).Name = propsOf(self).Name
    for key, value in pairs(nativeRawGet(deref(self), "_attributes")) do
        nativeRawGet(copy, "_attributes")[key] = value
    end
    for _, child in ipairs(childList(self)) do
        child:Clone().Parent = copy
    end
    return copy
end

function Instance:Destroy()
    local target = deref(self)
    if nativeRawGet(target, "_destroyed") then
        return
    end
    nativeRawSet(target, "_destroyed", true)
    for _, child in ipairs(self:GetChildren()) do
        child:Destroy()
    end
    detach(target)
    instanceById[nativeRawGet(target, "_id")] = nil
end

function Instance:ClearAllChildren()
    for _, child in ipairs(self:GetChildren()) do
        child:Destroy()
    end
end

-- ---------------------------------------------------------------------------
-- Data model and services
-- ---------------------------------------------------------------------------

local function makeService(name)
    local service = Instance.new(name)
    propsOf(service).Name = name
    return service
end

local game = Instance.new("DataModel")
propsOf(game).Name = "Game"
propsOf(game).PlaceId = 0
propsOf(game).GameId = 0
propsOf(game).JobId = "saintnova-harness"
propsOf(game).CreatorId = 0

local services = {}

function game:GetService(name)
    local existing = services[name]
    if existing ~= nil then
        return existing
    end
    local service = makeService(name)
    service.Parent = self
    services[name] = service
    return service
end

function game:FindService(name)
    return services[name]
end

function game:HttpGet()
    error("HttpGet is disabled in the SaintNova harness", 2)
end

function game:HttpGetAsync()
    error("HttpGetAsync is disabled in the SaintNova harness", 2)
end

local workspace = game:GetService("Workspace")
propsOf(workspace).Name = "Workspace"
propsOf(workspace).Gravity = 196.2
propsOf(workspace).CurrentCamera = nil

function workspace:Raycast()
    return nil
end

function workspace:GetPartBoundsInBox()
    return {}
end

function workspace:GetPartBoundsInRadius()
    return {}
end

local players = game:GetService("Players")
propsOf(players).Name = "Players"
propsOf(players).MaxPlayers = 1
propsOf(players).LocalPlayer = nil

local player = Instance.new("Player")
propsOf(player).Name = "Player1"
propsOf(player).DisplayName = "Player1"
propsOf(player).UserId = 1
player.Parent = players

local character = Instance.new("Model")
propsOf(character).Name = "Player1"
character.Parent = workspace

local humanoid = Instance.new("Humanoid")
propsOf(humanoid).Name = "Humanoid"
humanoid.Parent = character

local rootPart = Instance.new("Part")
propsOf(rootPart).Name = "HumanoidRootPart"
propsOf(rootPart).Size = Vector3.new(2, 2, 1)
propsOf(rootPart).Anchored = true
propsOf(rootPart).CFrame = CFrame.new(0, 5, 0)
propsOf(rootPart).Position = Vector3.new(0, 5, 0)
rootPart.Parent = character

propsOf(player).Character = character
propsOf(players).LocalPlayer = player

function players:GetPlayers()
    return { player }
end

function players:GetPlayerByUserId(userId)
    if propsOf(player).UserId == userId then
        return player
    end
    return nil
end

function players:GetPlayerFromCharacter(model)
    if model == character then
        return player
    end
    return nil
end

local runService = game:GetService("RunService")
propsOf(runService).Name = "RunService"

function runService:IsStudio()
    return false
end

function runService:IsClient()
    return true
end

function runService:IsServer()
    return false
end

function runService:IsRunning()
    return true
end

propsOf(runService).Heartbeat = Signal.new()
propsOf(runService).RenderStepped = Signal.new()
propsOf(runService).Stepped = Signal.new()

local userInput = game:GetService("UserInputService")
function userInput:IsKeyDown()
    return false
end
function userInput:GetMouseLocation()
    return Vector3.new(0, 0, 0)
end
propsOf(userInput).InputBegan = Signal.new()
propsOf(userInput).InputEnded = Signal.new()

local scriptInstance = Instance.new("LocalScript")
propsOf(scriptInstance).Name = "SaintNovaScript"
scriptInstance.Parent = workspace

-- ---------------------------------------------------------------------------
-- Enum
-- ---------------------------------------------------------------------------

local function newEnumType(name)
    return nativeSetMetatable({}, {
        __index = function(self, key)
            local item = { Name = key, Value = 0, EnumType = name }
            nativeRawSet(self, key, item)
            return item
        end,
    })
end

local Enum = nativeSetMetatable({}, {
    __index = function(self, key)
        local enumType = newEnumType(key)
        nativeRawSet(self, key, enumType)
        return enumType
    end,
})

-- ---------------------------------------------------------------------------
-- typeof and environment
-- ---------------------------------------------------------------------------

function typeof(value)
    local metatable = nativeGetMetatable(value)
    if metatable == Vector3Metatable then
        return "Vector3"
    elseif metatable == CFrameMetatable then
        return "CFrame"
    elseif metatable == InstanceMetatable then
        return classOf(value)
    elseif metatable == Signal then
        return "RBXScriptSignal"
    end
    return nativeTypeof(value)
end

-- ---------------------------------------------------------------------------
-- Harness helpers
-- ---------------------------------------------------------------------------

local function isSignal(value)
    return nativeType(value) == "table" and nativeGetMetatable(value) == Signal
end

local function signalFor(instance, name)
    local props = propsOf(instance)
    local signal = props[name]
    if not isSignal(signal) then
        signal = Signal.new()
        props[name] = signal
    end
    return signal
end

local function expectInstance(value, name)
    if not isInstance(value) then
        nativeError(name .. " expects an Instance", 3)
    end
    return value
end

local function expectSignal(value, name)
    if not isSignal(value) then
        nativeError(name .. " expects a signal created by the harness", 3)
    end
    return value
end

local function isHiddenInstance(instance)
    local node = instance
    while node ~= nil do
        if securedInstances[node] then
            return true
        end
        node = nativeRawGet(node, "_parent")
    end
    return false
end

local function forEachInstance(callback)
    -- cache.replace can point two registry slots at one instance, and
    -- cache.invalidate hides a slot; dedupe and filter so queries stay sane.
    local seen = {}
    for id = 1, nextInstanceId do
        local instance = instanceById[id]
        if instance ~= nil and not cacheInvalid[instance] and not seen[instance] then
            seen[instance] = true
            callback(instance)
        end
    end
end

local function collectScripts(predicate)
    local out = {}
    forEachInstance(function(instance)
        if instance ~= game and not isHiddenInstance(instance) and instance:IsA("BaseScript")
            and (predicate == nil or predicate(instance)) then
            tableInsert(out, instance)
        end
    end)
    return out
end

function getinstances()
    local out = {}
    forEachInstance(function(instance)
        if instance ~= game and nativeRawGet(instance, "_parent") ~= nil and not isHiddenInstance(instance) then
            tableInsert(out, instance)
        end
    end)
    return out
end

function getnilinstances()
    local out = {}
    forEachInstance(function(instance)
        if instance ~= game and nativeRawGet(instance, "_parent") == nil and not isHiddenInstance(instance) then
            tableInsert(out, instance)
        end
    end)
    return out
end

function getscripts()
    return collectScripts(nil)
end

function getmodules(parent)
    if parent ~= nil then
        expectInstance(parent, "getmodules")
    end
    local out = {}
    if parent == nil then
        forEachInstance(function(instance)
            if instance ~= game and not isHiddenInstance(instance) and instance.ClassName == "ModuleScript" then
                tableInsert(out, instance)
            end
        end)
        return out
    end
    for _, descendant in ipairs(parent:GetDescendants()) do
        if descendant.ClassName == "ModuleScript" and not isHiddenInstance(descendant) then
            tableInsert(out, descendant)
        end
    end
    return out
end

function getloadedmodules()
    local out = {}
    forEachInstance(function(instance)
        if instance ~= game and not isHiddenInstance(instance) and instance.ClassName == "ModuleScript" then
            tableInsert(out, instance)
        end
    end)
    return out
end

function getrunningscripts()
    if nativeRawGet(scriptInstance, "_parent") == nil then
        return {}
    end
    return { scriptInstance }
end

function getcallingscript()
    return scriptInstance
end

function getthreadscript(thread)
    if nativeType(thread) ~= "thread" then
        return nil
    end
    return scriptInstance
end

function getsenv(target)
    expectInstance(target, "getsenv")
    return getgenv()
end

function gettenv()
    return getgenv()
end

function getrenvglobal(name)
    return getgenv()[name]
end

function getrenvshared(name)
    return shared[name]
end

function islocalsourcecontainer(value)
    if not isInstance(value) then
        return false
    end
    return value.ClassName == "LocalScript" or value.ClassName == "ModuleScript"
end

local hiddenUi = nil

function gethui()
    if hiddenUi == nil then
        hiddenUi = Instance.new("Folder")
        hiddenUi.Name = "SaintNovaHui"
        securedInstances[hiddenUi] = true
    end
    return hiddenUi
end

local moduleCache = nativeSetMetatable({}, { __mode = "k" })
local moduleNil = {}

function require(target)
    expectInstance(target, "require")
    if target.ClassName ~= "ModuleScript" then
        nativeError("require expects a ModuleScript", 2)
    end

    local cached = moduleCache[target]
    if cached ~= nil then
        if cached == moduleNil then
            return nil
        end
        return cached
    end

    local source = target.Source
    if source == nil or source == "" then
        nativeError("ModuleScript has no Source in the SaintNova harness: " .. target:GetFullName(), 2)
    end

    local chunk, err = loadstring(source, "=" .. target:GetFullName())
    if chunk == nil then
        nativeError(err, 2)
    end

    local result = chunk()
    if result == nil then
        moduleCache[target] = moduleNil
    else
        moduleCache[target] = result
    end
    return result
end

-- ---------------------------------------------------------------------------
-- Instance security surface
-- ---------------------------------------------------------------------------

function createsecurefolder(path)
    if nativeType(path) ~= "string" then
        nativeError("createsecurefolder expects a string", 2)
    end
    local folder = Instance.new("Folder")
    folder.Name = path:match("([^/\\]+)$") or "SecureFolder"
    securedInstances[folder] = true
    return folder
end

function issecuredinstance(instance)
    expectInstance(instance, "issecuredinstance")
    return securedInstances[instance] == true or protectedInstances[instance] == true
end

function setsecureinstance(instance)
    expectInstance(instance, "setsecureinstance")
    if securedInstances[instance] then
        return false
    end
    securedInstances[instance] = true
    return true
end

function setnormalinstance(instance)
    expectInstance(instance, "setnormalinstance")
    if not securedInstances[instance] then
        return false
    end
    securedInstances[instance] = nil
    return true
end

function protectinstance(instance)
    expectInstance(instance, "protectinstance")
    protectedInstances[instance] = true
end

function setscriptable(instance, property, toggle)
    expectInstance(instance, "setscriptable")
    if nativeType(property) ~= "string" then
        nativeError("setscriptable expects a property name", 2)
    end
    local restricted = unscriptableProps[instance]
    if restricted == nil then
        restricted = {}
        unscriptableProps[instance] = restricted
    end
    restricted[property] = not toggle
end

function isscriptable(instance, property)
    if not isInstance(instance) then
        return false
    end
    if property == nil then
        return true
    end
    local restricted = unscriptableProps[instance]
    return restricted == nil or not restricted[property]
end

function gethiddenproperty(instance, property)
    expectInstance(instance, "gethiddenproperty")
    return instance[property]
end

function sethiddenproperty(instance, property, value)
    expectInstance(instance, "sethiddenproperty")
    instance[property] = value
end

function fireclickdetector(detector, distance)
    expectInstance(detector, "fireclickdetector")
    if detector.ClassName ~= "ClickDetector" then
        nativeError("fireclickdetector expects a ClickDetector", 2)
    end
    local localPlayer = game:GetService("Players").LocalPlayer
    signalFor(detector, "MouseClick"):Fire(localPlayer, nativeTonumber(distance) or 0)
end

function fireproximityprompt(prompt)
    expectInstance(prompt, "fireproximityprompt")
    if prompt.ClassName ~= "ProximityPrompt" then
        nativeError("fireproximityprompt expects a ProximityPrompt", 2)
    end
    local localPlayer = game:GetService("Players").LocalPlayer
    signalFor(prompt, "Triggered"):Fire(localPlayer)
end

function firetouchinterest(totouch, part, toggle)
    expectInstance(totouch, "firetouchinterest")
    expectInstance(part, "firetouchinterest")
    if not totouch:IsA("BasePart") then
        nativeError("firetouchinterest expects a BasePart", 2)
    end
    if toggle == 1 then
        signalFor(totouch, "TouchEnded"):Fire(part)
    else
        signalFor(totouch, "Touched"):Fire(part)
    end
end

-- ---------------------------------------------------------------------------
-- Signal surface
-- ---------------------------------------------------------------------------

function getconnections(signal)
    expectSignal(signal, "getconnections")
    return signal:GetConnections()
end

function firesignal(signal, ...)
    expectSignal(signal, "firesignal")
    signal:Fire(...)
end

function replicatesignal(signal, ...)
    expectSignal(signal, "replicatesignal")
    signal:Fire(...)
end

function disableconnection(connection)
    if nativeType(connection) ~= "table" then
        nativeError("disableconnection expects a connection", 2)
    end
    connection.Enabled = false
end

function enableconnection(connection)
    if nativeType(connection) ~= "table" then
        nativeError("enableconnection expects a connection", 2)
    end
    connection.Enabled = true
end

function isconnectionenabled(connection)
    if nativeType(connection) ~= "table" then
        return false
    end
    return connection.Connected ~= false and connection.Enabled ~= false
end

function getsignalarguments(connection)
    if nativeType(connection) ~= "table" then
        return {}
    end
    return connection._lastArgs or {}
end

function hooksignal(signal, callback)
    expectSignal(signal, "hooksignal")
    if nativeType(callback) ~= "function" then
        nativeError("hooksignal expects a function", 2)
    end
    signal._hook = callback
end

function unhooksignal(signal)
    expectSignal(signal, "unhooksignal")
    signal._hook = nil
end

function issignalhooked(signal)
    expectSignal(signal, "issignalhooked")
    return signal._hook ~= nil
end

function cansignalreplicate(signal)
    expectSignal(signal, "cansignalreplicate")
    return false
end

function getallreplicatesignals()
    return {}
end

-- ---------------------------------------------------------------------------
-- Console
-- ---------------------------------------------------------------------------

local consoleState = { created = false, title = "SaintNova console" }

function rconsolecreate()
    consoleState.created = true
end

function rconsoleclose()
    consoleState.created = false
end

rconsoledestroy = rconsoleclose

function rconsoleclear()
    __saintnova.emit("info", "", "console.clear")
end

function rconsoleprint(text)
    __saintnova.emit("info", nativeTostring(text), "console")
end

function rconsoleinfo(text)
    __saintnova.emit("info", "[INFO] " .. nativeTostring(text), "console")
end

function rconsolewarn(text)
    __saintnova.emit("warn", "[WARN] " .. nativeTostring(text), "console")
end

function rconsoleerr(text)
    __saintnova.emit("error", "[ERROR] " .. nativeTostring(text), "console")
end

function rconsoledebug(text)
    __saintnova.emit("debug", "[DEBUG] " .. nativeTostring(text), "console")
end

function rconsolename(title)
    consoleState.title = nativeTostring(title)
    __saintnova.emit("info", consoleState.title, "console.title")
end

rconsolesettitle = rconsolename

function rconsoleinput()
    nativeError("rconsoleinput is not available in the SaintNova harness: scripts have no interactive stdin", 2)
end

function printconsole(message, red, green, blue)
    __saintnova.emit("info", nativeTostring(message), "console")
end

-- ---------------------------------------------------------------------------
-- Reflection / closures
-- ---------------------------------------------------------------------------

local function executorClosure(function_)
    if nativeType(function_) ~= "function" then
        return false
    end
    if iscclosure(function_) then
        return true
    end
    return debugInfo(function_, "s") == "saintnova.sandbox"
end

isexecutorclosure = executorClosure
isourclosure = executorClosure
is_our_closure = executorClosure
is_executor_closure = executorClosure
checkclosure = executorClosure

function islineinfo(function_)
    if nativeType(function_) ~= "function" then
        return false
    end
    return debugInfo(function_, "l") ~= -1
end

function checkcaller()
    return true
end

function isourcallstack(level)
    local start = (nativeTonumber(level) or 1) + 1
    for depth = start, start + 25 do
        local source = debugInfo(depth, "s")
        if source == nil then
            break
        end
        if source == "[C]" or source == "saintnova.sandbox" then
            return true
        end
    end
    return false
end

function getnamecallmethod()
    return nil
end

function newlclosure(function_)
    if nativeType(function_) ~= "function" then
        nativeError("newlclosure expects a function", 2)
    end
    if not iscclosure(function_) then
        return clonefunction(function_)
    end
    return function(...)
        return function_(...)
    end
end

function hookmetamethod(object, method, hook)
    if nativeType(method) ~= "string" then
        nativeError("hookmetamethod expects a metamethod name", 2)
    end
    if nativeType(hook) ~= "function" then
        nativeError("hookmetamethod expects a function", 2)
    end
    local metatable = getrawmetatable(object)
    if metatable == nil then
        nativeError("hookmetamethod: object has no metatable", 2)
    end
    local original = nativeRawGet(metatable, method)
    nativeRawSet(metatable, method, hook)
    return original
end

function ishooked(function_)
    return false
end

function getscriptbytecode(target)
    if target == scriptInstance then
        return __saintnova.bytecode()
    end
    return nil
end

-- ---------------------------------------------------------------------------
-- File helpers
-- ---------------------------------------------------------------------------

function dofile(path)
    local chunk, err = loadfile(path)
    if chunk == nil then
        nativeError(err, 2)
    end
    return chunk()
end

-- ---------------------------------------------------------------------------
-- Explicitly unavailable surface
-- ---------------------------------------------------------------------------

local function unsupported(name, reason)
    return function()
        nativeError(name .. " is not available in the SaintNova harness: " .. reason, 2)
    end
end

local function disabledNetwork(name)
    return unsupported(name, "harness networking is disabled")
end

-- Hooking and memory:
hookfunction = unsupported("hookfunction", "function closures cannot be patched from Luau")
replaceclosure = hookfunction
hookfunc = hookfunction
replacefunction = hookfunction
replacefunc = hookfunction
detourfunction = hookfunction
protectclosure = unsupported("protectclosure", "function closures cannot be patched from Luau")
protectfunction = protectclosure
restoreclosure = unsupported("restoreclosure", "function closures cannot be patched from Luau")
restorefunction = restoreclosure
restorefunc = restoreclosure
setclosureidentity = unsupported("setclosureidentity", "closure identities are not modelled")

-- Script introspection:
getscriptclosure = unsupported("getscriptclosure", "the harness does not retain loaded script closures")
getscriptfunction = getscriptclosure
getscripthash = unsupported("getscripthash", "script hashing is not implemented")
getfunctionhash = unsupported("getfunctionhash", "function hashing is not implemented")
getgc = unsupported("getgc", "the harness does not expose the Luau heap")
get_gc_objects = getgc
filtergc = unsupported("filtergc", "the harness does not expose the Luau heap")
filter_gc = filtergc
getreg = unsupported("getreg", "the Lua registry is not exposed")

-- Network:
httpget = disabledNetwork("httpget")
HttpGet = disabledNetwork("HttpGet")
request = disabledNetwork("request")
http_request = disabledNetwork("http_request")
syn = { request = disabledNetwork("syn.request") }
Http = { Get = disabledNetwork("Http.Get") }
WebSocket = { connect = disabledNetwork("WebSocket.connect") }
getobjects = unsupported("getobjects", "asset loading requires a live Roblox client")
queue_on_teleport = unsupported("queue_on_teleport", "the harness has no teleport queue")
queueonteleport = queue_on_teleport

function clearteleportqueue()
    -- There is no teleport queue in the harness, so clearing it is complete.
end

clear_teleport_queue = clearteleportqueue

-- Client / renderer:
decompile = unsupported("decompile", "no Luau decompiler is bundled")
saveinstance = unsupported("saveinstance", "RBXL/RBXM serialisation is not implemented")
getcustomasset = unsupported("getcustomasset", "no Roblox content host is active")
writecustomasset = unsupported("writecustomasset", "no Roblox content host is active")
setfflag = unsupported("setfflag", "engine fast flags do not exist in the harness")
setfpscap = unsupported("setfpscap", "the harness has no renderer")
getfpscap = unsupported("getfpscap", "the harness has no renderer")
setnamecallmethod = unsupported("setnamecallmethod", "namecall hooks are not modelled")
setsimulationradius = unsupported("setsimulationradius", "the harness has no physics simulation")
addspycallback = unsupported("addspycallback", "the harness has no remote event pipeline")
zstdcompress = unsupported("zstdcompress", "zstd is not bundled with the harness")
zstddecompress = unsupported("zstddecompress", "zstd is not bundled with the harness")

function getfflag(name)
    -- No fast flags are registered in the harness; nil means "unknown flag".
    return nil
end

function isnetworkowner(part)
    return false
end

function isrenderobj(object)
    return false
end

getrenderproperty = unsupported("getrenderproperty", "the harness has no renderer")
setrenderproperty = unsupported("setrenderproperty", "the harness has no renderer")

local Drawing = {}
function Drawing.new()
    nativeError("Drawing.new is not available in the SaintNova harness: there is no renderer", 2)
end
_G.Drawing = Drawing

-- Input simulation is deliberately not wired to the operator's desktop.
function isrbxactive()
    return false
end

local inputNames = {
    "keydown", "keypress", "keyup", "keyrelease", "keytap",
    "mouse1click", "mouse1press", "mouse1release", "mouse2click", "mouse2press", "mouse2release",
    "mousescroll", "mousemoverel", "mousemoveabs",
}
for _, name in ipairs(inputNames) do
    _G[name] = unsupported(name, "input simulation requires a live Roblox client")
end

-- ---------------------------------------------------------------------------
-- Libraries
-- ---------------------------------------------------------------------------

-- cache ---------------------------------------------------------------------

local cacheTable = {}

function cloneref(instance)
    if not isInstance(instance) then
        nativeError("cloneref expects an Instance", 2)
    end
    local alias = nativeSetMetatable({}, InstanceMetatable)
    instanceAliasTarget[alias] = deref(instance)
    return alias
end

function compareinstances(a, b)
    if not isInstance(a) or not isInstance(b) then
        return false
    end
    return deref(a) == deref(b)
end

function cacheTable.iscached(instance)
    if not isInstance(instance) then
        return false
    end
    local target = deref(instance)
    return instanceById[nativeRawGet(target, "_id")] == target and not cacheInvalid[target]
end

function cacheTable.invalidate(instance)
    if not isInstance(instance) then
        nativeError("cache.invalidate expects an Instance", 2)
    end
    cacheInvalid[deref(instance)] = true
end

function cacheTable.replace(old, new)
    if not isInstance(old) or not isInstance(new) then
        nativeError("cache.replace expects two Instances", 2)
    end
    -- Repoints the registry slot: queries that would have returned `old` now
    -- return `new`. References the script already holds keep working, exactly
    -- as they would in a client where the cache is a lookup, not the object.
    instanceById[nativeRawGet(deref(old), "_id")] = deref(new)
end

_G.cache = cacheTable

-- debug ---------------------------------------------------------------------

local function debugTarget(f)
    if nativeType(f) == "number" then
        return debugInfo(f, "f")
    end
    return f
end

function debug.getinfo(f)
    local source = debugInfo(f, "s")
    if source == nil then
        return nil
    end
    local func = debugTarget(f)
    local upvalueCount = 0
    if nativeType(func) == "function" then
        local _, count = __saintnova.upvalues(func)
        upvalueCount = count or 0
    end
    return {
        name = debugInfo(f, "n") or "",
        short_src = source,
        what = source == "[C]" and "C" or "Lua",
        currentline = debugInfo(f, "l"),
        nups = upvalueCount,
        func = func,
        source = source,
    }
end

function debug.getupvalue(f, index)
    local target = debugTarget(f)
    if nativeType(target) ~= "function" then
        return nil
    end
    local value, name = __saintnova.getupvalue(target, index)
    if name == nil then
        return nil
    end
    return value, name
end

function debug.getupvalues(f)
    local target = debugTarget(f)
    if nativeType(target) ~= "function" then
        return {}
    end
    return (__saintnova.upvalues(target))
end

function debug.setupvalue(f, index, value)
    local target = debugTarget(f)
    if nativeType(target) ~= "function" then
        return nil
    end
    return __saintnova.setupvalue(target, index, value)
end

function debug.setmetatable(object, metatable)
    if nativeType(object) ~= "table" then
        nativeError("debug.setmetatable expects a table", 2)
    end
    if nativeType(metatable) ~= "table" then
        nativeError("debug.setmetatable expects a table metatable", 2)
    end
    setrawmetatable(object, metatable)
    return object
end

-- Prototypes, constants, the registry, and frame stacks are VM internals that
-- Luau's public API does not expose. Reading them would mean compiling against
-- private headers for a layout that changes between Luau releases.
debug.getconstant = unsupported("debug.getconstant", "the harness does not expose VM constants")
debug.getconstants = unsupported("debug.getconstants", "the harness does not expose VM constants")
debug.setconstant = unsupported("debug.setconstant", "the harness does not expose VM constants")
debug.getproto = unsupported("debug.getproto", "inner prototypes are not exposed")
debug.getprotos = unsupported("debug.getprotos", "inner prototypes are not exposed")
debug.setproto = unsupported("debug.setproto", "inner prototypes are not exposed")
debug.getregistry = unsupported("debug.getregistry", "the Lua registry is not exposed")
debug.getstack = unsupported("debug.getstack", "frame stacks are not exposed")
debug.setstack = unsupported("debug.setstack", "frame stacks are not exposed")

function validlevel(f, level)
    local target = level
    if target == nil then
        target = f
    end
    return debugInfo(target, "s") ~= nil
end

isvalidlevel = validlevel

-- actors --------------------------------------------------------------------

function getactors()
    local out = {}
    forEachInstance(function(instance)
        if instance ~= game and not isHiddenInstance(instance) and classOf(instance) == "Actor" then
            tableInsert(out, instance)
        end
    end)
    return out
end

function getcurrentactor()
    local node = nativeRawGet(deref(scriptInstance), "_parent")
    while node ~= nil do
        if classOf(node) == "Actor" then
            return node
        end
        node = nativeRawGet(node, "_parent")
    end
    return nil
end

function is_parallel()
    return false
end

function get_actor_from_thread(thread)
    -- Nothing runs in a parallel actor state here, so no thread belongs to one.
    return nil
end

getactorfromthread = get_actor_from_thread
getthreadactor = get_actor_from_thread
get_thread_actor = get_actor_from_thread
get_current_actor = getcurrentactor

create_comm_channel = unsupported("create_comm_channel", "parallel actor states are not modelled")
get_comm_channel = unsupported("get_comm_channel", "parallel actor states are not modelled")
get_deleted_actors = unsupported("get_deleted_actors", "parallel actor states are not modelled")
run_on_actor = unsupported("run_on_actor", "parallel actor states are not modelled")

-- Drawing -------------------------------------------------------------------

function cleardrawcache()
    -- Drawing objects cannot be created without a renderer, so the cache is
    -- already empty and clearing it is genuinely complete.
end

function isrenderproperty()
    return false
end

is_render_property = isrenderproperty
getrenderobj = getrenderproperty

-- Compression ---------------------------------------------------------------

lz4_compress = unsupported("lz4_compress", "LZ4 is not bundled with the harness")
lz4_decompress = unsupported("lz4_decompress", "LZ4 is not bundled with the harness")

-- ---------------------------------------------------------------------------
-- Global surface
-- ---------------------------------------------------------------------------

_G.Vector3 = Vector3
_G.CFrame = CFrame
_G.Color3 = Color3
_G.UDim = UDim
_G.UDim2 = UDim2
_G.Instance = Instance
_G.Enum = Enum
_G.Signal = Signal
_G.game = game
_G.Game = game
_G.workspace = workspace
_G.Workspace = workspace
_G.script = scriptInstance
_G.syn = syn
_G.Http = Http
_G.WebSocket = WebSocket
_G.HttpGet = disabledNetwork("HttpGet")

-- Aliases used by Wave and other executors.
get_instances = getinstances
get_nil_instances = getnilinstances
get_scripts = getscripts
get_loaded_modules = getloadedmodules
get_modules = getmodules
get_script_function = getscriptclosure
get_calling_script = getcallingscript
getscriptcaller = getcallingscript
getcaller = getcallingscript
getmenv = getsenv
getstateenv = gettenv
get_thread_script = getthreadscript
is_secured_instance = issecuredinstance
set_secure_instance = setsecureinstance
set_normal_instance = setnormalinstance
create_secure_folder = createsecurefolder
get_executor_name = identifyexecutor
getexecutorname = identifyexecutor
set_thread_identity = setthreadidentity
get_thread_identity = getthreadidentity

-- Class tables in the harness behave like Roblox's readonly metatables. A
-- script can still call setreadonly(getrawmetatable(obj), false) to opt in to
-- mutation, exactly as it would in a live client.
nativeSetReadonly(InstanceMetatable, true)
nativeSetReadonly(Vector3Metatable, true)
nativeSetReadonly(CFrameMetatable, true)
nativeSetReadonly(Color3, true)
nativeSetReadonly(UDim, true)
nativeSetReadonly(UDim2, true)
nativeSetReadonly(Signal, true)

_G.getgenv().game = game
_G.getgenv().workspace = workspace
