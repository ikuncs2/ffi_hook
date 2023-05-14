cmake_minimum_required(VERSION 3.18)
project(ffi_hook)
set(CMAKE_CXX_STANDARD 20)

add_library(detours STATIC
	Detours/src/creatwth.cpp
	Detours/src/detours.cpp
	Detours/src/disasm.cpp
	Detours/src/disolx64.cpp
	Detours/src/image.cpp
	Detours/src/modules.cpp
)
target_include_directories(detours PUBLIC 
	Detours/src
)

find_package(Lua 5.1 REQUIRED)

add_library(lua5.1 INTERFACE)
target_include_directories(lua5.1 INTERFACE 
	${LUA_INCLUDE_DIR}
)
target_link_libraries(lua5.1 INTERFACE 
	${LUA_LIBRARIES}
)


add_library(ffi_hook SHARED 
	src/luaopen_ffi_hook.cpp
	src/inline.cpp
)
target_include_directories(ffi_hook PUBLIC 
	include
)
target_link_libraries(ffi_hook PUBLIC detours lua5.1)