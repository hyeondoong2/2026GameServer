#include "Player.h"

void Player::Init() {
  HRESULT result = player_image_.Load(L"chess.png");

  if (FAILED(result)) {
    OutputDebugString(L"Player Image Load Failed!\n");
  }
}

void Player::SetPos(int x, int y) {
  x_ = x;
  y_ = y;
}

void Player::Render(HDC hdc) {
  if (!player_image_.IsNull()) {
    player_image_.Draw(hdc, x_, y_);
  }
}
