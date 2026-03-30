-- hello.lua: Example QEmacs Lua plugin
--
-- Install: cp hello.lua ~/.qe/
-- Or load at runtime: M-x load-plugin
--
-- This is the Lua equivalent of the original my_plugin.c example.

qe.command("insert-hello", "C-c h", function()
    qe.insert("Hello world\n")
end)
