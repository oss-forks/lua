
CMAKE_MINIMUM_REQUIRED(VERSION 2.8)

IF(MSVC)
	ADD_DEFINITIONS(-D_CRT_SECURE_NO_DEPRECATE=1)
	ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS=1)
	ADD_DEFINITIONS(-D_CRT_NONSTDC_NO_DEPRECATE=1)
ENDIF()

ADD_DEFINITIONS(-DLUA_USE_LONGLONG)

ADD_LIBRARY(lua STATIC EXCLUDE_FROM_ALL
	src/lapi.c
	src/lapi.h
	src/lauxlib.c
	src/lauxlib.h
	src/lbaselib.c
	src/lbitlib.c
	src/lcode.c
	src/lcode.h
	src/lcorolib.c
	src/lctype.c
	src/lctype.h
	src/ldblib.c
	src/ldebug.c
	src/ldebug.h
	src/ldo.cpp
	src/ldo.h
	src/ldump.c
	src/lfunc.c
	src/lfunc.h
	src/lgc.c
	src/lgc.h
	src/linit.c
	src/liolib.c
	src/llex.c
	src/llex.h
	src/llimits.h
	src/lmathlib.c
	src/lmem.c
	src/lmem.h
	src/loadlib.c
	src/lobject.c
	src/lobject.h
	src/lopcodes.c
	src/lopcodes.h
	src/loslib.c
	src/lparser.c
	src/lparser.h
	src/lstate.c
	src/lstate.h
	src/lstring.c
	src/lstring.h
	src/lstrlib.c
	src/ltable.c
	src/ltable.h
	src/ltablib.c
	src/ltm.c
	src/ltm.h
	src/lua.h
	src/lua.hpp
	src/luaconf.h
	src/lualib.h
	src/lundump.c
	src/lundump.h
	src/lvm.c
	src/lvm.h
	src/lzio.c
	src/lzio.h
	src/util.cpp
)

ADD_EXECUTABLE(luai EXCLUDE_FROM_ALL src/lua.c)
TARGET_LINK_LIBRARIES(luai lua)

ADD_EXECUTABLE(luac EXCLUDE_FROM_ALL src/luac.c)
TARGET_LINK_LIBRARIES(luac lua)
