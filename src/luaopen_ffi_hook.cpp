extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include "inline.h"
#include <memory>
//#include <base/hook/iat.h>
//#include <bee/utility/unicode_win.h>	

//static std::wstring lua_towstring(lua_State* L, int idx)
//{
//	size_t len = 0;
//	const char* buf = luaL_checklstring(L, idx, &len);
//	return bee::u2w(std::string_view(buf, len));
//}

struct hook_t {
	enum class type {
		Inline,
		IAT,
	};
	type type = type::Inline;
	uintptr_t real = 0;
	uintptr_t fake = 0;
	base::hook::hook_t hi = 0;
};

int FFI_OBJ;

static int push_funcdef(lua_State* L, int idx) {
	switch (lua_type(L, idx)) {
		case LUA_TUSERDATA: {
			lua_pushvalue(L, idx); // 1 = obj
			lua_pushlightuserdata(L, &FFI_OBJ);
			lua_rawget(L, LUA_REGISTRYINDEX); // 2 = ffi
			lua_getfield(L, -1, "typeof"); // 3 = ffi.typeof
			lua_insert(L, -3); // ffi.typeof, obj, ffi
			lua_pop(L, 1);
			lua_call(L, 1, 1); // ffi.typeof(obj)
			return 1;
		}
		default:
			return luaL_argerror(L, idx, lua_pushfstring(L, "bad function def #%d, got %s", idx, luaL_typename(L, -1)));
	}
}

static int hook_call(lua_State* L) {
	hook_t* h = (hook_t*)lua_touserdata(L, 1);

	lua_getfenv(L, 1);
	if (LUA_TTABLE != lua_type(L, -1)) {
		return 0;
	}

	lua_pushlightuserdata(L, &FFI_OBJ);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "cast");
	lua_remove(L, -2); // ffi.cast

	lua_rawgeti(L, -2, 1);
	lua_pushlightuserdata(L, (void *)h->real);
	lua_call(L, 2, 1); // ffi.cast(cdef, h.real)

	lua_remove(L, -2);
	lua_replace(L, 1);
	lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	return lua_gettop(L);
}

static int hook_remove(lua_State* L) {
	hook_t* h = (hook_t*)lua_touserdata(L, 1); // "hook"
	if (!h) {
		return 0;
	}
	switch (h->type) {
	case hook_t::type::Inline:
		base::hook::uninstall(&h->hi);
		break;
	}
	return 0;
}

static int hookSetFFI(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_settop(L, 1);
	lua_pushlightuserdata(L, &FFI_OBJ);
	lua_insert(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);
	return 0;
}

typedef void(*infHook)(lua_State* L, hook_t* h);

static void initHookStruct(hook_t *h, void *old_addr, void *new_addr)
{
	h->type = hook_t::type::Inline;
	h->fake = (uintptr_t)new_addr;
	h->real = (uintptr_t)old_addr; // old func addr
	base::hook::install(&h->real, h->fake, &h->hi);
}

// function inline(old_func : userdata, callback : function)
static int hookInline(lua_State* L) {
	luaL_checktype(L, 1, LUA_TUSERDATA);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	lua_settop(L, 2);
	push_funcdef(L, 1); // 3=cdef=ffi.type(old_func)
	lua_pushvalue(L, 2); // 4 = callback

	lua_pushlightuserdata(L, &FFI_OBJ);
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_getfield(L, -1, "new");
	lua_remove(L, -2); // 5=ffi.new
	if (lua_type(L, -1) != LUA_TFUNCTION) {
		return luaL_error(L, "No initialization.");
	}

	lua_pushvalue(L, -3); // old_func, callback, cdef, callback, ffi.new, cdef

	lua_insert(L, -4); // old_func, callback, cdef, cdef, callback, ffi.new
	lua_insert(L, -3); // old_func, callback, cdef, ffi.new, cdef, callback
	lua_call(L, 2, 1); // old_func, callback, cdef, ffi.new(cdef, callback)

	hook_t* h = (hook_t*)lua_newuserdata(L, sizeof hook_t); // 5=hook
	if (!h) {
		return 0;
	}
	new (h) hook_t;
	luaL_getmetatable(L, "hook");
	lua_setmetatable(L, -2);

	lua_newtable(L); // 6=hook data
	lua_pushvalue(L, -4); // 7=cdef
	lua_rawseti(L, -2, 1);
	lua_pushvalue(L, -3); // 7=ffi.new(cdef, callback)
	lua_rawseti(L, -2, 2);
	lua_setfenv(L, -2); // 3=cdef, 4=ffi.new(cdef, callback), 5=hook

	lua_pushlightuserdata(L, &FFI_OBJ);
	lua_rawget(L, LUA_REGISTRYINDEX); // 6=ffi
	lua_getfield(L, -1, "cast");
	lua_remove(L, -2); // 6=ffi.cast

	lua_pushstring(L, "void (*)(void *, void *, void *)");
	lua_pushlightuserdata(L, (void*)initHookStruct);
	lua_call(L, 2, 1); // 6=ffi.cast("void (*)(void *, void *, void *)", initHookStruct)

	lua_pushvalue(L, -2); // 7=hook
	lua_pushvalue(L, 1); // 8=old_func cdata
	lua_pushvalue(L, -5); // 9=ffi.new(cdef, callback)
	lua_call(L, 3, 0); // 5=hook

	return 1; // return hook
}

// from lua 5.3
static void luaL_setfuncs(lua_State* L, const luaL_Reg* l, int nup) {
	luaL_checkstack(L, nup + 1, "too many upvalues");
	for (; l->name != NULL; l++) {  /* fill the table with given functions */
		int i;
		lua_pushstring(L, l->name);
		for (i = 0; i < nup; i++)  /* copy upvalues to the top */
			lua_pushvalue(L, -(nup + 1));
		lua_pushcclosure(L, l->func, nup);  /* closure with those upvalues */
		lua_settable(L, -(nup + 3)); /* table must be below the upvalues, the name and the closure */
	}
	lua_pop(L, nup);  /* remove upvalues */
}

extern "C" __declspec(dllexport)
int luaopen_ffi_hook(lua_State* L)
{
	luaL_newmetatable(L, "hook");
	lua_pushcfunction(L, hook_remove);
	lua_setfield(L, -2, "remove");
	lua_pushcfunction(L, hook_call);
	lua_setfield(L, -2, "__call");

	lua_newtable(L);
	lua_pushcfunction(L, hookSetFFI);
	lua_setfield(L, -2, "setffi");
	lua_pushcfunction(L, hookInline);
	lua_setfield(L, -2, "inline");
	return 1;
}

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID pReserved)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);
	}
	return TRUE;
}
