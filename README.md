# ffi hook

这是一个借助[cffi](https://github.com/q66/cffi-lua)实现的inline hook库。这是一个简单的例子

``` lua
local ffi = require 'ffi'
local ffi_hook = require 'ffi_hook'

ffi_hook.set_ffi(ffi)

ffi.cdef [[
    int __stdcall MessageBoxA(int hWnd, const char* lpText, const char* lpCaption, unsigned int uType);
]]

local hookMessageBoxA 
hookMessageBoxA = ffi_hook.inline(ffi.C.MessageBoxA, function(hwnd, text, title, type)
    return hookMessageBoxA(hwnd, 'Hello ' .. ffi.string(text), ffi.string(title), type)
end)
ffi.C.MessageBoxA(0, 'Test', 'Title', 0)
hookMessageBoxA:remove()
ffi.C.MessageBoxA(0, 'Test', 'Title', 0)
```
