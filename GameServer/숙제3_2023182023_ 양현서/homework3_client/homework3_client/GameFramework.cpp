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
  // send
  KeyPacket packet;
  if(has_new_input_) {
    packet.move_dir = last_move_dir_;
    WSABUF send_buf = { sizeof(KeyPacket), reinterpret_cast<char*>(&packet) };
    DWORD sent_bytes = 0;
    WSASend(NetworkManager::Instance().socket(), &send_buf, 1, &sent_bytes, 0, nullptr, nullptr);
    has_new_input_ = false;
  } else {
    packet.move_dir = PlayerMoveDir::kNone;
    WSABUF send_buf = { sizeof(KeyPacket), reinterpret_cast<char*>(&packet) };
    DWORD sent_bytes = 0;
    WSASend(NetworkManager::Instance().socket(), &send_buf, 1, &sent_bytes, 0, nullptr, nullptr);

  }

  // recv
  PosPacket recv_packet = {};
  char* buf = reinterpret_cast<char*>(&recv_packet);
  int total_received = 0;

  while (total_received < (int)sizeof(PosPacket)) {
    WSABUF recv_buf = { sizeof(PosPacket) - total_received, buf + total_received };
    DWORD recv_bytes = 0, flags = 0;
    int result = WSARecv(NetworkManager::Instance().socket(), &recv_buf, 1, &recv_bytes, &flags, nullptr, nullptr);
    total_received += recv_bytes;
    Player::Instance().SetPos(recv_packet.x, recv_packet.y);
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
    last_move_dir_ = PlayerMoveDir::kUp;
    has_new_input_ = true;
    break;
  case VK_DOWN:
    last_move_dir_ = PlayerMoveDir::kDown;
    has_new_input_ = true;
    break;
  case VK_LEFT:
    last_move_dir_ = PlayerMoveDir::kLeft;
    has_new_input_ = true;
    break;
  case VK_RIGHT:
    last_move_dir_ = PlayerMoveDir::kRight;
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

