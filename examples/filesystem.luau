-- Sandboxed filesystem demo.
--
-- Every path resolves under %LOCALAPPDATA%\SaintNova\workspace (override with the
-- SAINTNOVA_WORKSPACE environment variable). Absolute paths, drive letters, and
-- ".." are rejected rather than clamped.

makefolder("examples")
writefile("examples/notes.txt", "first line\n")
appendfile("examples/notes.txt", "second line\n")

print("contents:")
print(readfile("examples/notes.txt"))
print("isfile:", isfile("examples/notes.txt"))
print("isfolder:", isfolder("examples"))
print("entries:", table.concat(listfiles("examples"), ", "))

-- loadfile/dofile compile from the same sandbox.
writefile("examples/greet.luau", "return function(name) return 'hello ' .. name end")
local greet = loadfile("examples/greet.luau")()
print(greet("sandbox"))

-- Escapes fail loudly instead of being rewritten.
for _, attempt in ipairs({ "../outside.txt", "C:/Windows/win.ini", "/etc/passwd" }) do
    local ok, err = pcall(readfile, attempt)
    print(("readfile(%q) -> %s: %s"):format(attempt, tostring(ok), tostring(err)))
end

delfile("examples/greet.luau")
delfile("examples/notes.txt")
delfolder("examples")
print("cleaned up:", isfolder("examples"))
