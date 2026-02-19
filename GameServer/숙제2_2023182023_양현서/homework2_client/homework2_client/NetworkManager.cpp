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

  DeleteCriticalSection(&cs_);
  WSACleanup();
}

SOCKET NetworkManager::socket() const {
  return socket_;
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