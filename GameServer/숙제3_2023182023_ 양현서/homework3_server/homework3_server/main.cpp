#include <iostream>
#include <WS2tcpip.h>
#include "Common.h"
#include "thread"
#include "memory.h"
#include "Player.h"

using namespace std;
#pragma comment (lib, "WS2_32.LIB")

const short SERVER_PORT = 4000;
const int BUFSIZE = 256;

bool g_is_running = true;
CRITICAL_SECTION g_cs;

void err_disp(const char* msg, int err_no) {
  WCHAR* h_mess;
  FormatMessage(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
    NULL, err_no,
    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
    (LPWSTR)&h_mess, 0, NULL);
  cout << msg;
  wcout << L"  에러 => " << h_mess << endl;
  while (true);
  LocalFree(h_mess);
}

bool IsRunning() {
  EnterCriticalSection(&g_cs);
  bool running = g_is_running;
  LeaveCriticalSection(&g_cs);
  return running;
}

DWORD WINAPI ClientWorker(LPVOID lpParam) {
  Player* player = static_cast<Player*>(lpParam);
  SOCKET sock = player->socket();

  while (IsRunning()) {
    KeyPacket key_packet = {};
    char* buf = reinterpret_cast<char*>(&key_packet);
    int total_received = 0;
    int packet_size = sizeof(KeyPacket);
    bool disconnected = false;

    // recv
    while (total_received < packet_size) {
      WSABUF recv_buf = { packet_size - total_received, buf + total_received };
      DWORD recv_bytes = 0, flags = 0;
      int result = WSARecv(sock, &recv_buf, 1, &recv_bytes, &flags, nullptr, nullptr);

      if (result == SOCKET_ERROR) {
        std::cout << "[Error] WSARecv 오류: " << WSAGetLastError() << std::endl;
        disconnected = true;
        break;
      }
      if (recv_bytes == 0) {
        std::cout << "[Info] 클라이언트 연결 종료" << std::endl;
        disconnected = true;
        break;
      }
      total_received += recv_bytes;
    }

    if (disconnected) break;

    player->UpdatePosition(key_packet.move_dir);
    PosPacket pos_packet = player->pos();

    // send
    WSABUF send_buf = { sizeof(PosPacket), reinterpret_cast<char*>(&pos_packet) };
    DWORD sent_bytes = 0;
    int send_result = WSASend(sock, &send_buf, 1, &sent_bytes, 0, nullptr, nullptr);
    if (send_result == SOCKET_ERROR) {
      std::cout << "[Error] WSASend 오류: " << WSAGetLastError() << std::endl;
      break;
    }
    std::cout << "New Pos: " << pos_packet.x << ", " << pos_packet.y << std::endl;
  }

  closesocket(sock);
  delete player;
  return 0;
}

int main() {
  wcout.imbue(std::locale("korean"));
  InitializeCriticalSection(&g_cs);

  WSADATA WSAData;
  WSAStartup(MAKEWORD(2, 0), &WSAData);

  SOCKET server_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);
  SOCKADDR_IN server_addr = {};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR) {
    std::cout << "[Error] bind 실패: " << WSAGetLastError() << std::endl;
    return 1;
  }
  if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
    std::cout << "[Error] listen 실패: " << WSAGetLastError() << std::endl;
    return 1;
  }

  // accept
  while (IsRunning()) {
    int addr_size = sizeof(sockaddr_in);
    SOCKET client_socket = accept(server_socket, nullptr, &addr_size);

    if (client_socket == INVALID_SOCKET) {
      std::cout << "[Error] accept 실패: " << WSAGetLastError() << std::endl;
      continue;
    }

    std::cout << "클라이언트 접속" << std::endl;

    Player* player = new Player(client_socket);

    HANDLE hThread = CreateThread(nullptr, 0, ClientWorker, player, 0, nullptr);

    if (hThread == nullptr) {
      std::cout << "[Error] 스레드 생성 실패" << std::endl;
      closesocket(client_socket);
      delete player;
    } else {
      CloseHandle(hThread);
    }
  }

  closesocket(server_socket);
  DeleteCriticalSection(&g_cs);
  WSACleanup();
  return 0;;
}



