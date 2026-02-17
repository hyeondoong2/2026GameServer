#include "NetworkManager.h"
#include "Player.h"
#include "GameFramework.h"

std::unique_ptr<NetworkManager> NetworkManager::instance_ = nullptr;

NetworkManager& NetworkManager::Instance() {
  if (instance_ == nullptr) {
    instance_.reset(new NetworkManager());
  }
  return *instance_;
}

bool NetworkManager::Init() {
  wcout.imbue(locale("korean"));

  WSADATA WSAData;
  WSAStartup(MAKEWORD(2, 0), &WSAData);
  socket_ = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);

  SOCKADDR_IN server_addr;
  ZeroMemory(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);

  inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);
  int retval = connect(socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
  if (retval == SOCKET_ERROR) {
    err_disp("connect()", WSAGetLastError());
    closesocket(socket_);
    socket_ = INVALID_SOCKET;
    return false;
  }


  InitializeCriticalSection(&cs_);

  EnterCriticalSection(&cs_);
  is_running_ = true;
  LeaveCriticalSection(&cs_);

  recv_thread_handle_ = CreateThread(nullptr, 0, RecvWorker, this, 0, nullptr);

  if (recv_thread_handle_ == nullptr) {
    DeleteCriticalSection(&cs_);
    return false;
  }

  return true;
}

void NetworkManager::CleanUp() {
  EnterCriticalSection(&cs_);
  is_running_ = false;
  LeaveCriticalSection(&cs_);

  if (socket_ != INVALID_SOCKET) {
    closesocket(socket_);
    socket_ = INVALID_SOCKET;
  }

  if (recv_thread_handle_ != nullptr) {
    WaitForSingleObject(recv_thread_handle_, 3000);
    CloseHandle(recv_thread_handle_);
    recv_thread_handle_ = nullptr;
  }

  DeleteCriticalSection(&cs_);
  WSACleanup();
}

void NetworkManager::SendKeyPacket(const KeyPacket& packet) {
  if (socket_ == INVALID_SOCKET) return;

  EnterCriticalSection(&cs_);
  pending_packet_ = packet;
  has_pending_ = true;
  LeaveCriticalSection(&cs_);
}

DWORD WINAPI NetworkManager::RecvWorker(LPVOID lpParam) {
  NetworkManager* self = static_cast<NetworkManager*>(lpParam);

  while (self->is_running_) {
    EnterCriticalSection(&self->cs_);
    bool has_packet = self->has_pending_;
    KeyPacket send_packet = self->pending_packet_;
    self->has_pending_ = false;
    LeaveCriticalSection(&self->cs_);

    // send
    if (has_packet) {
      WSABUF send_buf = { sizeof(KeyPacket), reinterpret_cast<char*>(&send_packet) };
      DWORD sent_bytes = 0;
      WSASend(self->socket_, &send_buf, 1, &sent_bytes, 0, nullptr, nullptr);
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(self->socket_, &read_set);
    timeval timeout = { 0, 1000 }; 
    if (select(0, &read_set, nullptr, nullptr, &timeout) <= 0) continue;

    // recv
    PosPacket recv_packet = {};
    char* buf = reinterpret_cast<char*>(&recv_packet);
    int total_received = 0;

    while (total_received < (int)sizeof(PosPacket)) {
      WSABUF recv_buf = { sizeof(PosPacket) - total_received, buf + total_received };
      DWORD recv_bytes = 0, flags = 0;
      int result = WSARecv(self->socket_, &recv_buf, 1, &recv_bytes, &flags, nullptr, nullptr);
      if (result == SOCKET_ERROR || recv_bytes == 0) {
        self->is_running_ = false;
        return 0;
      }
      total_received += recv_bytes;
    }

    EnterCriticalSection(&self->cs_);
    Player::Instance().SetPos(recv_packet.x, recv_packet.y);
    LeaveCriticalSection(&self->cs_);
  }
  return 0;
}

DWORD __stdcall NetworkManager::SendWorker(LPVOID lpParam) {
  return 0;
}

void NetworkManager::err_disp(const char* msg, int err_no) {
  WCHAR* h_mess;
  FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL, err_no,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPWSTR)&h_mess, 0, NULL);
  cout << msg;
  wcout << L"  ¿¡·¯ => " << h_mess << endl;
  while (true);
  LocalFree(h_mess);
}