#include <iostream>
#include <WS2tcpip.h>
#include <array>
#include <MSWSock.h>
#include "protocol.h"

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")
using namespace std;

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

void send_login_fail(SOCKET client, const char* message)
{
    // 얜 비동기로해도 상관없음

}

// 클라 정보를 갖고 있는 객체 SESSION
class SESSION
{
public:
    SOCKET m_client;
    long long m_id;
    bool m_is_connected;
    short m_x, m_y;
    char m_user_name[MAX_NAME_LEN];
    EXP_OVER m_recv_over; // recv용
    int m_prev_recv;
    SESSION()
    {
        m_is_connected = false;
        m_id = 999;
        m_client = INVALID_SOCKET;
        m_recv_over.m_iotype = IO_RECV;
        m_x = 0;
        m_y = 0;
        m_prev_recv = 0;
    }

public:
    SESSION(int id, SOCKET so) : m_id(id), m_client(so)
    {
        m_recv_over.m_iotype = IO_RECV; // Recv로 초기화 해주기
    }
    ~SESSION()
    {
        if(!m_is_connected)
        closesocket(m_client);
    }

    void do_recv()
    {
        // recv는 전용 버퍼 사용. 패킷 재조립 구현 필요
        DWORD recv_flag = 0;
        memset(&m_recv_over.m_over, 0, sizeof(m_recv_over.m_over));
        int ret = WSARecv(m_client, &m_recv_over.m_wsa, 1, 0, &recv_flag,
            &m_recv_over.m_over, nullptr);
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
        o->m_wsa.len = num_bytes; // num_bytes+2 로 딱 필요한 만큼 전송
        memcpy(o->m_buff + 2, mess, num_bytes);
        WSASend(m_client, &o->m_wsa, 1, 0, 0, &o->m_over, nullptr);
    }

    void send_avatar_info()
    {
        S2C_AvatarInfo_Pakcet packet;
        packet.size = sizeof(S2C_AvatarInfo_Pakcet);
        packet.type = S2C_AVATAR_INFO;

    }

    void send_move_packet(int mover_id)
    {
        S2C_MovePlayer_Packet packet;
        packet.size = sizeof(S2C_MovePlayer_Packet);
        packet.type = S2C_MOVE_PLAYER;
        packet.player_id = mover_id;
        packet.x = clients[mover_id].m_x;
        packet.y = clients[mover_id].m_y;
    }

    void send_login_success()
    {

    }
    void send_add_player(int player_id)
    {
        S2C_AddPlayer_Packet packet;
        packet.size = sizeof(S2C_AddPlayer_Packet);
        packet.type = S2C_ADD_PLAYER;
        packet.player_id = player_id;
        SESSION& pl = clients[player_id];
        memcpy(packet.user_name, pl.m_user_name, sizeof(packet.user_name);
        packet.x = pl.m_x;
        packet.y = pl.m_y;
        do_send();
    }

    void process_packet(unsigned char* p)
    {
        PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
        switch (type)
        {
        case C2S_LOGIN: {
            C2S_Login_Packet* packet = reinterpret_cast<C2S_Login_Packet*>(p);
            strncpy(m_user_name, packet->use_rname, MAX_NAME_LEN);
            send_avatar_info();
            break;
        }
        case  C2S_MOVE: {
            C2S_Move_Packet* packet = reinterpret_cast<C2S_Move_Packet*>(p);
            DIRECTION dir = packet->dir;
            switch (dir)
            {
            case UP: break;
            case DOWN: break;
            case LEFT: break;
            case RIGHT: break;
            }
            send_move_packet();
            break;
        }
        }
    }
};

array<SESSION, MAX_PLAYERS> clients;

int main()
{
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    SOCKET server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
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

            int new_id = -1;
            for (int i = 0; i > MAX_PLAYERS; ++i)
            {
                if (!clients[i].m_is_connected)
                {
                    new_id = i;
                    break;
                }
            }
            if (new_id == -1)
            {
                send_login_fail(client_socket, "Server is Full");
                closesocket(client_socket);
            } else
            {
                CreateIoCompletionPort((HANDLE)client_socket, h_iocp, new_id, 0);	// 클라이언트 소켓을 iocp에 등록, 세션 생성. CompletionKey가 id이자 접속순서
                clients[new_id].m_is_connected = true;
                clients[new_id].m_client = client_socket;
                clients[new_id].m_x = 0;
                clients[new_id].m_y = 0;
                clients[new_id].m_id = new_id;
                clients[new_id].send_login_success_packet();
                clients[new_id].send_avatar_info();
                clients[new_id].m_prev_recv = 0;
                clients[new_id].do_recv();	//  recv 호출
            }

            client_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            memset(&accept_over.m_over, 0, sizeof(accept_over.m_over)); // 재호출 전 초기화 해주기
            AcceptEx(server, client_socket, &accept_over.m_buff, 0,
                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                NULL, &accept_over.m_over); // 다시 비동기 accept를 호출
            break;
        }
        case IO_RECV:
        {
            int player_index = static_cast<int>(key);
            if (num_bytes == 0)
            {
                cout << "Client[" << player_index << "] disconnected." << endl;
                clients.erase(key);
                break;
            }
            cout << "Received message." << endl;
            cout << "Client[" << player_index << "] sent : "
                << clients[player_index].m_recv_over.m_buff << endl;

            SESSION& cl = clients[player_index];

            // 패킷 재조립
            unsigned char* p = reinterpret_cast<unsigned char*>(exp_over->m_buff);
            int data_size = num_bytes + cl.m_prev_recv;
            while (data_size > 0)
            {
                int packet_size = p[0];
                if (packet_size > data_size) break;
                // process packet 받은 패킷 처리
                clients[player_index].process_packet(exp_over->m_buff);
                p += packet_size;
                data_size -= packet_size;
            }
            if (data_size > 0)
            {
                memmove(cl.m_recv_over.m_buff, p, data_size);
                cl.m_prev_recv = recv_size;
            }

            for (auto& cl : clients)
            {
                if (!cl.m_is_connected) continue;
                if (cl.m_id == player_index) continue;
                cl.add_player(player_index);

            }
            clients[player_index].do_recv();	// recv가 완료되면 다시 WSARecv 호출
            break;
        }
        case IO_SEND: {
            cout << "Message sent to client [" << key << "]" << endl;
            EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
            delete o;	// send가 완료되면 메모리 반환
            break;
        }
        default:
            cout << "Unknown IO type." << endl;
            exit(-1);
            break;
        }
    }

    closesocket(server);
    WSACleanup();
}
