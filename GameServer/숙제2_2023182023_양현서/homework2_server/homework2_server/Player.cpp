#include "Player.h"

void Player::UpdatePosition(PlayerMoveDir dir) {
  EnterCriticalSection(&cs_);
  switch (dir) {
  case PlayerMoveDir::kUp:
    pos_.y -= 100;
    if (pos_.y < 0) {
      pos_.y += 100;
    }
    break;
  case PlayerMoveDir::kDown:
    pos_.y += 100;
    if (pos_.y > 700) {
      pos_.y -= 100;
    }
    break;
  case PlayerMoveDir::kLeft:
    pos_.x -= 100;
    if (pos_.x < 0) {
      pos_.x += 100;
    }
    break;
  case PlayerMoveDir::kRight:
    pos_.x += 100;
    if (pos_.x > 700) {
      pos_.x -= 100;
    }
    break;
  default:
    break;
  }
  LeaveCriticalSection(&cs_);
}
