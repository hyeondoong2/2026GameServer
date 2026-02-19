#include "GameFramework.h"
#include "Player.h"
#include "Board.h"
#include "NetworkManager.h"

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

  if (!NetworkManager::Instance().Init()) {
    std::cout << "init 실패" << std::endl;
    return;
  }

  Board::Instance().Init();
  Player::Instance().Init();
}

void GameFramework::Progress() {
  if (has_new_input_) {
    KeyPacket packet;
    packet.move_dir = current_move_dir_;

    std::cout << "has new input" << std::endl;
    NetworkManager::Instance().SendKeyPacket(packet);
    has_new_input_ = false;
  }

    Board::Instance().Render(hdc_);
    Player::Instance().Render(hdc_);

}

void GameFramework::CleanUp() {
  if (hwnd_ && hdc_) {
    ReleaseDC(hwnd_, hdc_);
    hdc_ = nullptr;
  }
}



void GameFramework::OnWindowMessage(HWND hwnd, UINT message_id, WPARAM w_param, LPARAM l_param) {
  if (message_id != WM_KEYDOWN) return;
  switch (w_param) {
    case VK_UP:
      current_move_dir_ = PlayerMoveDir::kUp;
      has_new_input_ = true;
      break;
    case VK_DOWN:
      current_move_dir_ = PlayerMoveDir::kDown;
      has_new_input_ = true;
      break;
    case VK_LEFT:
      current_move_dir_ = PlayerMoveDir::kLeft;
      has_new_input_ = true;
      break;
    case VK_RIGHT:
      current_move_dir_ = PlayerMoveDir::kRight;
      has_new_input_ = true;
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

