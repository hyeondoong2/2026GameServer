#pragma once

constexpr short PORT = 3500;
constexpr int WORLD_WIDTH = 8;
constexpr int WORLD_HEIGHT = 8;
constexpr int MAX_PLAYERS = 10;
constexpr int MAX_NAME_LEN = 20;

enum PACKET_TYPE {
    C2S_LOGIN,
    C2S_MOVE, 
    S2C_LOGIN_RESULT,
    S2C_AVATAR_INFO,
    S2C_ADD_PLAYER,
    S2C_REMOVE_PLAYER,
    S2C_MOVE_PLAYER
};
enum DIRECTION { UP, DOWN, LEFT, RIGHT };


#pragma pack(push, 1)

struct BUF_SIZE
{
    unsigned char size; // unsigned 인 이유 : 사이즈가 마이너스일리가 없음
    PACKET_TYPE type;
    char use_rname[20];
    int id;
};

struct C2S_Move_Packet
{
    unsigned char size;
    PACKET_TYPE type;
    DIRECTION dir;
};

struct S2C_Login_Result_Packet
{
    unsigned char size;
    PACKET_TYPE type;
    bool success;
    char message[50];
};

struct S2C_AvatarInfo_Pakcet
{
    unsigned char size;
    PACKET_TYPE type;
    int player_id;
    short x;
    short y;
};

struct S2C_AddPlayer_Packet
{
    unsigned char size;
    PACKET_TYPE type;
    int player_id;
    char user_name[MAX_NAME_LEN];
    short x;
    short y;
};

struct S2C_ReomvePlayer_Packet
{
    unsigned char size;
    PACKET_TYPE type;
    int player_id;
};

struct S2C_MovePlayer_Packet
{
    unsigned char size;
    PACKET_TYPE type;
    int player_id;
    int x;
    int y;
};

#pragma pack(pop)