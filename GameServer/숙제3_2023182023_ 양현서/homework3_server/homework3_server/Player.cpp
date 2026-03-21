#include "Player.h"

void Player::UpdatePosition(PlayerMoveDir dir)
{
    switch (dir)
    {
    case PlayerMoveDir::kUp:
        y_ -= 100;
        if (y_ < 0)
        {
            y_ += 100;
        }
        break;
    case PlayerMoveDir::kDown:
        y_ += 100;
        if (y_ > 700)
        {
            y_ -= 100;
        }
        break;
    case PlayerMoveDir::kLeft:
        x_ -= 100;
        if (x_ < 0)
        {
            x_ += 100;
        }
        break;
    case PlayerMoveDir::kRight:
        x_ += 100;
        if (x_ > 700)
        {
            x_ -= 100;
        }
        break;
    case PlayerMoveDir::kNone:
    default:
        break;
    }
}
