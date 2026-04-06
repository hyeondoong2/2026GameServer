#include <iostream>
#include <WS2tcpip.h>
using namespace std;
#pragma comment (lib, "WS2_32.LIB")
const char* SERVER_IP = "127.0.0.1";
const short SERVER_PORT = 54000;
const int BUFFER_SIZE = 4096;

int main() {
  wcout.imbue(locale("korean"));
  WSADATA WSAData;
  WSAStartup(MAKEWORD(2, 0), &WSAData);
  SOCKET s_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, 0);
  SOCKADDR_IN server_addr{};
  char recv_buf[BUFFER_SIZE];
  WSABUF recv_wsa_buf;
  recv_wsa_buf.buf = recv_buf;
  ZeroMemory(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
  connect(s_socket, reinterpret_cast<SOCKADDR*>(&server_addr), sizeof(server_addr));

  for (;;) {
    char buf[BUFFER_SIZE];
    cout << "Enter Message : "; 
    cin.getline(buf, BUFFER_SIZE);

    // send
    DWORD sent_byte;
    WSABUF wsa_buf;
    wsa_buf.buf = buf;
    wsa_buf.len = static_cast<ULONG>(strlen(buf)) + 1; 
    WSASend(234, &wsa_buf, 1, &sent_byte, 0, 0, 0);

    // recv
    recv_wsa_buf.len = BUFFER_SIZE;
    DWORD recv_byte;
    DWORD recv_flag = 0;
    WSARecv(s_socket, &recv_wsa_buf, 1, &recv_byte, &recv_flag, 0, 0);

    cout << "Server Sent [" << recv_byte << "bytes] : " << recv_buf << endl;
  }

  WSACleanup();
}
