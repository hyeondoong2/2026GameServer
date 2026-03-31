#include <iostream>
#include <WS2tcpip.h>
#include <unordered_map>
#include <MSWSock.h>

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")
using namespace std;
constexpr int PORT_NUM = 3500;
constexpr int BUF_SIZE = 200;

enum IOType { IO_SEND, IO_RECV, IO_ACCEPT };	// 어떤 타입인지 구분 필요,,,

// EXP_OVER로 클라마다 버퍼 따로 관리
class EXP_OVER
{
public:
    WSAOVERLAPPED m_over;
    IOType  m_iotype;   // 현재 IO의 Type을 알기위해 사용
    WSABUF	m_wsa;
    char  m_buff[BUF_SIZE];
    EXP_OVER()
    {
        ZeroMemory(&m_over, sizeof(m_over));
        m_wsa.buf = m_buff;
        m_wsa.len = BUF_SIZE;
    }
    EXP_OVER(IOType iot) : m_iotype(iot)
    {
        ZeroMemory(&m_over, sizeof(m_over));
        m_wsa.buf = m_buff;
        m_wsa.len = BUF_SIZE;
    }
};

void error_display(const wchar_t* msg, int err_no)
{
    WCHAR* lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err_no,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf, 0, NULL);
    std::wcout << msg;
    std::wcout << L" === 에러 " << lpMsgBuf << std::endl;
    while (true);   // 디버깅 용
    LocalFree(lpMsgBuf);
}

class SESSION;
unordered_map<long long, SESSION> clients;

// 클라 정보를 갖고 있는 객체 SESSION
class SESSION
{
    SOCKET client;
    long long m_id;
public:
    EXP_OVER recv_over; // recv용
    SESSION() { exit(-1); }
    SESSION(int id, SOCKET so) : m_id(id), client(so)
    {
        recv_over.m_iotype = IO_RECV; // Recv로 초기화 해주기
    }
    ~SESSION()
    {
        closesocket(client);
    }

    void do_recv()
    {
        // recv는 전용 버퍼 사용. 패킷 재조립 구현 필요
        DWORD recv_flag = 0;
        memset(&recv_over.m_over, 0, sizeof(recv_over.m_over));
        int ret = WSARecv(client, &recv_over.m_wsa, 1, 0, &recv_flag,
            &recv_over.m_over, nullptr);
        if (ret == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING)
        {
            cout << "Client[" << m_id << "] recv error!" << endl;
            clients.erase(m_id);
        }
    }
    void do_send(int sender_id, int num_bytes, char* mess)
    {
        // send는 할때마다 new/delete -> 성능을 위해서 메모리풀 만들면 좋음
        EXP_OVER* o = new EXP_OVER(IO_SEND);
        o->m_buff[0] = num_bytes + 2;
        o->m_buff[1] = sender_id;
        memcpy(o->m_buff + 2, mess, num_bytes);
        o->m_wsa.len = num_bytes + 2; // num_bytes+2 로 딱 필요한 만큼 전송
        WSASend(client, &o->m_wsa, 1, 0, 0, &o->m_over, nullptr);
    }
};

int main()
{
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
    CreateIoCompletionPort((HANDLE)server, h_iocp, 0, 0);	// 서버메인 소켓 iocp에 등록

    SOCKET client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);    // 클라 소켓 미리 생성

    // AcceptEX로 클라이언트 연결도 비동기 처리
    EXP_OVER accept_over(IO_ACCEPT);
    AcceptEx(server, client_socket, &accept_over.m_buff, 0,
        sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
        NULL, &accept_over.m_over);

    int client_id_counter = 1;

    while (true)
    {
        DWORD num_bytes;
        ULONG_PTR key;
        LPOVERLAPPED over;

        // overlapped i/o 콜백에서 하던걸 여기서 처리하는방식...
        // 이걸 받았을 떄 클라 객체를 찾을 수 있어야 함. key를 클라의 id나 인덱스로 설정하면 좋다.
        GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);	// IOCP 완료 검사

        if (over == nullptr)
        {
            error_display(L"GQCS Errror: ", WSAGetLastError());
            continue;
        }

        // over 값에 따른 처리
        EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(over);
        switch (exp_over->m_iotype)
        {
        case IO_ACCEPT:
        {
            int new_id = client_id_counter++;
            cout << "Client[" << new_id << "] connected." << endl;
            CreateIoCompletionPort((HANDLE)client_socket, h_iocp, new_id, 0);	// 클라이언트 소켓을 iocp에 등록, 세션 생성. CompletionKey가 id이자 접속순서
            clients.try_emplace(new_id, new_id, client_socket);
            clients[new_id].do_recv();	//  recv 호출
            client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            memset(&accept_over.m_over, 0, sizeof(accept_over.m_over)); // 재호출 전 초기화 해주기
            AcceptEx(server, client_socket, &accept_over.m_buff, 0,
                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                NULL, &accept_over.m_over); // 다시 비동기 accept를 호출
            break;
        }
        case IO_RECV:
        {
            int client_id = static_cast<int>(key);
            if (num_bytes == 0)
            {
                cout << "Client[" << client_id << "] disconnected." << endl;
                clients.erase(key);
                break;
            }
            cout << "Received message." << endl;
            cout << "Client[" << client_id << "] sent: "
                << clients[client_id].recv_over.m_buff << endl;
            for (auto& cl : clients)
                cl.second.do_send(client_id, num_bytes, clients[client_id].recv_over.m_buff);
            clients[client_id].do_recv();	// recv가 완료되면 다시 WSARecv 호출
        break;
        }
        case IO_SEND: {
            cout << "Message sent." << endl;
            EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
            delete o;	// send가 완료되면 메모리 반환
                    break;
        }
        default:
            cout << "Unknown IO type." << endl;
            break;
        }
    }

    closesocket(server);
    WSACleanup();
}
