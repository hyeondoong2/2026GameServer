#include "Player.h"

std::unique_ptr<Player> Player::instance_ = nullptr;

Player& Player::Instance() {
  if (instance_ == nullptr) {
    instance_.reset(new Player());
  }
  return *instance_;
}

void Player::Init() {
  x_ = 0;
  y_ = 0;

  HRESULT result = player_image_.Load(L"chess.png");

  if (FAILED(result)) {
    OutputDebugString(L"Player Image Load Failed!\n");
  }
}

void Player::Move(PlayerMoveDir dir) {
  switch (dir) {
  case PlayerMoveDir::kUp:
    y_ -= kMoveStep;
    if (y_ < kMinPos) {
      y_ += kMoveStep;
    }
    break;
  case PlayerMoveDir::kDown:
    y_ += kMoveStep;
    if (y_ > kMaxPos) {
      y_ -= kMoveStep;
    }
    break;
  case PlayerMoveDir::kLeft:
    x_ -= kMoveStep;
    if (x_ < kMinPos) {
      x_ += kMoveStep;
    }
    break;
  case PlayerMoveDir::kRight:
    x_ += kMoveStep;
    if (x_ > kMaxPos) {
      x_ -= kMoveStep;
    }
    break;
  }
}

void Player::Render(HDC hdc) {
  if (!player_image_.IsNull()) {
    player_image_.Draw(hdc, x_, y_);
  }
}
