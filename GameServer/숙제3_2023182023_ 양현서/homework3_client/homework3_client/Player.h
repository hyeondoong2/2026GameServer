#ifndef PLAYER_H_  
#define PLAYER_H_

#include "stdafx.h"

class Player
{
public:
    void Init();
    void SetPos(int x, int y);
    void Render(HDC hdc);
    void SetID(int id) { id_ = id; }

    Player() = default;
    Player(int x, int y) :x_(x), y_(y) { Init(); };
    ~Player() = default;

private:

    int x_ = 0;
    int y_ = 0;
    int size_ = 0;
    int id_ = -1;

    CImage player_image_;
};

#endif  // PLAYER_H_