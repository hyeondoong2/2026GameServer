#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>	// Session 여러 개를 unordered_map으로 관리하기 위해
#include <MSWSock.h>
#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")
using namespace std;
constexpr int PORT_NUM = 3500;
constexpr int BUF_SIZE = 200;

enum IOType { IO_Send, IO_Recv, Io_Accept };  // 어떤 타입인지 구분이 필요

class EXP_OVER {
public:
  WSAOVERLAPPED m_over;
  IOType m_iotype;
  WSABUF	m_wsa;
  char  m_buff[BUF_SIZE];

public:

  EXP_OVER() {
    ZeroMemory(&m_over, sizeof(m_over));
    m_wsa.buf = m_buff;
    m_wsa.len = BUF_SIZE;
  }
  EXP_OVER(IOType iot) : m_iotype(iot) {
    ZeroMemory(&m_over, sizeof(m_over));
    m_wsa.buf = m_buff;
    m_wsa.len = BUF_SIZE;
  }
};

class SESSION;
unordered_map<long long, SESSION> clients;

class SESSION {
  SOCKET client;
  long long m_id;
public:
  EXP_OVER recv_over;
  SESSION() { exit(-1); }
  SESSION(int id, SOCKET so) : m_id(id), client(so) {}
  ~SESSION() {
    closesocket(client);
  }

  void do_recv() {
    // WSABUF 구조체를 초기화하고 WSARecv 호출
    DWORD recv_flag = 0;
    memset(&recv_over.m_over, 0, sizeof(recv_over.m_over));
    WSARecv(client, &recv_over.m_wsa, 1, 0, &recv_flag, &recv_over.m_over, nullptr);
  }

  void do_send(int sender_id, int num_bytes, char* mess) {
    // EXP_OVER 객체를 생성하여 WSASend에 넘김
    EXP_OVER* o = new EXP_OVER(IO_Send);
    o->m_buff[0] = num_bytes + 2;
    o->m_buff[1] = static_cast<char>(sender_id);
    memcpy(o->m_buff + 2, mess, num_bytes);
    WSASend(client, &o->m_wsa, 1, 0, 0, &o->m_over, nullptr);
  }
};

void CALLBACK recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags) {
  // hEvent에 client_id가 저장되어 있음
  int client_id = static_cast<int>(reinterpret_cast<long long>(over->hEvent));
  cout << "Client[" << client_id << "] sent: " << clients[client_id].c_mess << endl;

  // 받은 메시지를 모든 클라이언트에게 전송
  for (auto& cl : clients)
    cl.second.do_send(client_id, num_bytes, clients[client_id].c_mess);
  clients[client_id].do_recv();
}

void CALLBACK send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags) {
  // 전송이 완료된 후, EXP_OVER 객체를 해제
  EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
  delete over;  // EXP_OVER로 변환 한 후 delete 해줘야함!!
}

int main() {
  WSADATA WSAData;
  WSAStartup(MAKEWORD(2, 2), &WSAData);
  SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
  SOCKADDR_IN server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT_NUM);
  server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
  bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
  listen(server, SOMAXCONN);
  HANDLE h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
  CreateIoCompletionPort((HANDLE)server, h_iocp, 0, 0); // 서버 소켓을 iocp에 등록
  // 첫번쨰 0 : 서버 소켓이므로 0, accept는 별도의 소켓이 생성되므로 accept에서 client_id를 넘겨줘야함


  // AcceptEx를 사용하여 클라이언트 연결을 비동기로 처리 
  SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
  EXP_OVER accept_over(Io_Accept);
  AcceptEx(server, client_socket, &accept_over.m_buff, 0,
    sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
    NULL, &accept_over.m_over);

  while (true) {
    DWORD num_bytes;
    ULONG_PTR key;
    LPOVERLAPPED over;

    // GetQueuedCompletionStatus를 호출하여 완료된 작업을 기다림 
    GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);
    if (over == nullptr) {
      cerr << "GetQueuedCompletionStatus failed: " << GetLastError() << endl;
      continue;
    }

    EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
    switch (o->m_iotype) {
    case Io_Accept:
      clients.try_emplace(i, i, client_socket);
      clients[i].do_recv();
      client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
      AcceptEx(server, client_socket, &accept_over.m_buff, 0,
        sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
        NULL, &accept_over.m_over);
      break;
    case IO_Recv:
      CreateIoCompletionPort((HANDLE)clients[static_cast<int>(key)].clients[i], h_iocp, key, 0); // 클라이언트 소켓을 iocp에 등록
      int client_id = static_cast<int>(key);
      std::cout << "Client [" << client_id << "] sent: " << clients[client_id].c_mess << std::endl;
      for(auto& cl : clients){
        cl.second.do_send(client_id, num_bytes, clients[client_id].c_mess);
      }
      clients[client_id].do_recv(); // 다음 메시지 수신 대기
      break;
    case IO_Send:
      break;
    }
    if (o->m_iotype == Io_Accept) {
      // AcceptEx 완료 처리
      SOCKET client = o->m_over.hEvent; // AcceptEx에서 생성된 클라이언트 소켓
      int client_id = static_cast<int>(key); // completion_key에 저장된 client_id
      clients.try_emplace(client_id, client_id, client);
      clients[client_id].do_recv(); // 클라이언트로부터 메시지 수신 시작

      // 다음 AcceptEx 호출 준비
      client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
      accept_over.m_over.hEvent = reinterpret_cast<HANDLE>(client_id); // 다음 연결을 위해 client_id 설정
      AcceptEx(server, client_socket, &accept_over.m_buff, 0,
        sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
        NULL, &accept_over.m_over);
    } else if (o->m_iotype == IO_Recv) {
      recv_callback(0, num_bytes, over, 0); // recv_callback ��출
    } else if (o->m_iotype == IO_Send) {
      send
    }

    SOCKADDR_IN cl_addr;
    int addr_size = sizeof(cl_addr);
    for (int i = 1; ; ++i) {
      SOCKET client = WSAAccept(server,
        reinterpret_cast<sockaddr*>(&cl_addr), &addr_size, NULL, NULL);
      clients.try_emplace(i, i, client);
      clients[i].do_recv();
    }
    closesocket(server);
    WSACleanup();
  }
