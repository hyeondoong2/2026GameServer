#include "GameFramework.h"
#include "Player.h"
#include "Board.h"

// static 멤버 변수 초기화
std::unique_ptr<GameFramework> GameFramework::instance_ = nullptr;

GameFramework& GameFramework::Instance() {
  if (instance_ == nullptr) {
    instance_.reset(new GameFramework());
  }
  return *instance_;
}

void GameFramework::Init() {
  hdc_ = GetDC(hwnd_);
  Board::Instance().Init();
  Player::Instance().Init();
  draw_enabled_ = true;
}

void GameFramework::Progress() {
  if (draw_enabled_) {
    Board::Instance().Render(hdc_);
    Player::Instance().Render(hdc_);
    draw_enabled_ = false;
  }
}

void GameFramework::CleanUp() {
  if (hwnd_ && hdc_) {
    ReleaseDC(hwnd_, hdc_);
    hdc_ = nullptr;
  }
}

void GameFramework::OnWindowMessage(HWND hwnd, UINT message_id, WPARAM w_param, LPARAM l_param) {
  switch (w_param) {
    case VK_UP:
      Player::Instance().Move(PlayerMoveDir::kUp);
      draw_enabled_ = true;
      break;
    case VK_DOWN:
      Player::Instance().Move(PlayerMoveDir::kDown);
      draw_enabled_ = true;
      break;
    case VK_LEFT:
      Player::Instance().Move(PlayerMoveDir::kLeft);
      draw_enabled_ = true;
      break;
    case VK_RIGHT:
      Player::Instance().Move(PlayerMoveDir::kRight);
      draw_enabled_ = true;
      break;
    default:
      break;
  }
}

void GameFramework::set_hinstance(HINSTANCE hinstance) {
  hinstance_ = hinstance;
}

HINSTANCE GameFramework::hinstance() const {
  return hinstance_;
}

void GameFramework::set_hwnd(HWND hwnd) {
  hwnd_ = hwnd;
}
