/* NDK 的 bionic 在 API 26 以下没有 localeconv()，而 Lua 只用它取小数点
 * （lua_getlocaledecpoint）。luaconf.h 用 #if !defined 保护该宏，属于官方
 * 定制点，这里固定为 '.'（等同 C locale）。
 * 由 CMake 的 -include 强制包含，只作用于本工程（Android）。 */
#ifndef CPYMO_LUA_ANDROID_COMPAT_H
#define CPYMO_LUA_ANDROID_COMPAT_H

#if !defined(lua_getlocaledecpoint)
#define lua_getlocaledecpoint() '.'
#endif

#endif
