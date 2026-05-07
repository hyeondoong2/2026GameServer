#include <iostream>
#include "include/lua.hpp"

#pragma comment(lib, "lua55.lib")

int main()
{
    const char* buff = "print \'Hello, Lua!\'";

    // 가상 머신을 포인터로 지정해줘야함...
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaL_loadfile(L, "dragon.lua");
    int error = lua_pcall(L, 0, 0, 0);
    if (error)
    {
        std::cerr << "Error: " << lua_tostring(L, -1) << std::endl;
        lua_pop(L, 1);
    }

    //lua_getglobal(L, "pos_x");
    //lua_getglobal(L, "pos_y");
    //// 스택에 푸쉬
    //int pos_x = (int)lua_tonumber(L, -2);
    //int pos_y = (int)lua_tonumber(L, -1);

    //printf("pos_x %d, pos_y %d\n", pos_x, pos_y);

    //lua_pop(L, 2);

    lua_getglobal(L, "addtwo");
    lua_pushnumber(L, 5);
    lua_pcall(L, 1, 1, 0);
    int result = (int)lua_tonumber(L, -1);
    printf("result %d\n", result);
    lua_pop(L, 1);

    lua_close(L);
}