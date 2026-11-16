-- Basics: printing, vectors, instances, scheduler.
print("saintnova", 1 + 1, _VERSION)

local v = Vector3.new(3, 4, 0)
print("vector", v, "magnitude", v.Magnitude, "unit", v.Unit)

local part = Instance.new("Part")
part.Name = "TestPart"
part.Parent = workspace
print("fullname", part:GetFullName(), "class", part.ClassName, "isA BasePart", part:IsA("BasePart"))

local humanoid = Instance.new("Humanoid")
print("default health", humanoid.Health, "walkspeed", humanoid.WalkSpeed)

print("children of workspace", #workspace:GetChildren())
print("local player", game:GetService("Players").LocalPlayer.Name)

task.spawn(function()
    print("spawned immediately")
end)

task.delay(0.05, function()
    print("delayed task fired")
end)

task.wait(0.1)
print("after wait")
