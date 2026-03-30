#pragma once

#define SC_ENTER   1
#define SC_MOVE    2
#define SC_LOGOUT  3
#define SC_REJECT  4

#define CS_LOGIN   1
#define CS_KEY     2
#define CS_LOGOUT  3

#include "Common.h"

#pragma pack(push, 1)

// SC
struct SC_ENTER_PACKET
{
    char  size = sizeof(SC_ENTER_PACKET);
    char  type = SC_ENTER;
    int   id;
    short x, y;
};

struct SC_MOVE_PACKET
{
    char  size = sizeof(SC_MOVE_PACKET);
    char  type = SC_MOVE;
    int   id;
    short x, y;
};

struct SC_LOGOUT_PACKET
{
    char size = sizeof(SC_LOGOUT_PACKET);
    char type = SC_LOGOUT;
    int  id;
};

// CS
struct CS_LOGIN_PACKET
{
    char size = sizeof(CS_LOGIN_PACKET);
    char type = CS_LOGIN;
};

struct CS_KEY_PACKET
{
    char size = sizeof(CS_KEY_PACKET);
    char type = CS_KEY;
    PlayerMoveDir move_dir;
};

struct CS_LOGOUT_PACKET
{
    char size = sizeof(CS_LOGOUT_PACKET);
    char type = CS_LOGOUT;
};

#pragma pack(pop)