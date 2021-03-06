/******************************************************************************
* Copyright (C) 2014 Nikolay Zapolnov
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
******************************************************************************/
#include "lua.hpp"
#include <exception>
#include <limits>

extern "C" {
#include "ldo.h"
#include "ldebug.h"
extern const lua_Number LUA_NAN = std::numeric_limits<lua_Number>::quiet_NaN();
extern const lua_Number LUA_INFINITY = std::numeric_limits<lua_Number>::infinity();
}

void lua_catch(lua_State * L)
{
	try
	{
		try
		{
			throw;
		}
		catch (const LuaException &)
		{
		}
		catch (const std::exception & e)
		{
			luaL_checkstack(L, 1, "lua_catch");
			lua_pushfstring(L, "unhandled C++ exception (%s)", e.what());
			luaG_errormsg(L);
		}
		catch (...)
		{
			luaL_checkstack(L, 1, "lua_catch");
			lua_pushliteral(L, "unhandled C++ exception");
			luaG_errormsg(L);
		}
	}
	catch (const LuaException &)
	{
	}
}
