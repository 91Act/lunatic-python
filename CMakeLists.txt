cmake_minimum_required(VERSION 3.5)

# project name
set(CUR_PROJ lua)
project(${CUR_PROJ})

# variables
set(PROJ_ROOT ${CMAKE_SOURCE_DIR})
set(LUA_ROOT ${PROJ_ROOT}/lua)

# source code
set(SOURCE 
    # lunatic sources
    ${PROJ_ROOT}/src/luainpython.c
    ${PROJ_ROOT}/src/pythoninlua.c
    # lua sources
    ${LUA_ROOT}/lapi.c
    ${LUA_ROOT}/lauxlib.c
    ${LUA_ROOT}/lbaselib.c
    ${LUA_ROOT}/lbitlib.c
    ${LUA_ROOT}/lcode.c
    ${LUA_ROOT}/lcorolib.c
    ${LUA_ROOT}/lctype.c
    ${LUA_ROOT}/ldblib.c
    ${LUA_ROOT}/ldebug.c
    ${LUA_ROOT}/ldo.c
    ${LUA_ROOT}/ldump.c
    ${LUA_ROOT}/lfunc.c
    ${LUA_ROOT}/lgc.c
    ${LUA_ROOT}/linit.c
    ${LUA_ROOT}/liolib.c
    ${LUA_ROOT}/llex.c
    ${LUA_ROOT}/lmathlib.c
    ${LUA_ROOT}/lmem.c
    ${LUA_ROOT}/loadlib.c
    ${LUA_ROOT}/lobject.c
    ${LUA_ROOT}/lopcodes.c
    ${LUA_ROOT}/loslib.c
    ${LUA_ROOT}/lparser.c
    ${LUA_ROOT}/lstate.c
    ${LUA_ROOT}/lstring.c
    ${LUA_ROOT}/lstrlib.c
    ${LUA_ROOT}/ltable.c
    ${LUA_ROOT}/ltablib.c
    ${LUA_ROOT}/ltm.c
    ${LUA_ROOT}/lundump.c
    ${LUA_ROOT}/lutf8lib.c
    ${LUA_ROOT}/lvm.c
    ${LUA_ROOT}/lzio.c
    )

# include path
include_directories(
    ${LUA_ROOT}
)
if(APPLE)
    include_directories(SYSTEM 
        /System/Library/Frameworks/Python.framework/Versions/2.7/include/python2.7)
elseif(UNIX)
    include_directories(SYSTEM 
        /usr/include/python2.7)
endif()

# external libraries
set(SHARED_LIBS python2.7)

# standard
set(CMAKE_C_STANDARD 99)

# output file adjust
set(CMAKE_SHARED_LIBRARY_SUFFIX .so)
set(CMAKE_SHARED_LIBRARY_PREFIX )

# macros
add_definitions(-DLUA_USE_DLOPEN)

# make executable
add_library(${CUR_PROJ} SHARED ${SOURCE})
target_link_libraries(${CUR_PROJ} ${STATIC_LIBS} ${SHARED_LIBS})
