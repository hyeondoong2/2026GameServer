#include <iostream>
#include <WS2tcpip.h>
#include <array>
#include <MSWSock.h>
#include <thread>
#include <vector>
#include "protocol.h"
#include <tbb/concurrent_unordered_map.h>
#include <memory>

#pragma comment(lib, "MSWSock.lib")
#pragma comment(lib, "WS2_32.lib")
using namespace std;

constexpr int BUF_SIZE = 200;

std::atomic<int> player_index = 0;
HANDLE h_iocp;
SOCKET server;

enum IOType { IO_SEND, IO_RECV, IO_ACCEPT };

class EXP_OVER
{
public:
    WSAOVERLAPPED m_over;
    IOType  m_iotype;
    WSABUF	m_wsa;
    SOCKET  m_client_socket;
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

enum CL_STATE { CS_CONNECT, CS_PLAYING, CS_LOGOUT };

class SESSION
{
public:
    // no data race
    SOCKET m_client;
    int m_id;
    EXP_OVER m_recv_over;
    int m_prev_recv;
    CL_STATE m_state;  // 클라 상태 표시

    // data race
    char m_username[MAX_NAME_LEN];
    short m_x, m_y;

    SESSION()
    {
        std::cout << "SESSION Creation Error!\n";
        exit(-1);
    }
    SESSION(SOCKET s, int id) : m_client(s), m_id(id)
    {
        m_state = CS_CONNECT;
        m_recv_over.m_iotype = IO_RECV;
        m_x = 0; 		m_y = 0;
        m_prev_recv = 0;
    }
    ~SESSION()
    {
        if (m_state == CS_CONNECT)
            closesocket(m_client);
    }
    void do_recv()
    {
        DWORD recv_flag = 0;
        memset(&m_recv_over.m_over, 0, sizeof(m_recv_over.m_over));
        WSARecv(m_client, &m_recv_over.m_wsa, 1, 0, &recv_flag, &m_recv_over.m_over, nullptr);
    }
    void do_send(int num_bytes, char* mess)
    {
        EXP_OVER* o = new EXP_OVER(IO_SEND);
        o->m_wsa.len = num_bytes;
        memcpy(o->m_buff, mess, num_bytes);
        WSASend(m_client, &o->m_wsa, 1, 0, 0, &o->m_over, nullptr);
    }
    void send_avatar_info()
    {
        S2C_AvatarInfo packet;
        packet.size = sizeof(S2C_AvatarInfo);
        packet.type = S2C_AVATAR_INFO;
        packet.playerId = m_id;
        packet.x = m_x;
        packet.y = m_y;
        do_send(packet.size, reinterpret_cast<char*>(&packet));
    }
    void send_move_packet(int mover);
    void send_add_player(int player_id);
    void send_login_success()
    {
        S2C_LoginResult packet;
        packet.size = sizeof(S2C_LoginResult);
        packet.type = S2C_LOGIN_RESULT;
        packet.success = true;
        strncpy_s(packet.message, "Login successful.", sizeof(packet.message));
        do_send(packet.size, reinterpret_cast<char*>(&packet));
    }
    void send_remove_player(int player_id)
    {
        S2C_RemovePlayer packet;
        packet.size = sizeof(S2C_RemovePlayer);
        packet.type = S2C_REMOVE_PLAYER;
        packet.playerId = player_id;
        do_send(packet.size, reinterpret_cast<char*>(&packet));
    }
    void process_packet(unsigned char* p);
};

// thread 는 별도의 함수이므로 세션을 전역변수로 관리.. -> Data Race 발생
// id를 재사용 하지 않으면서 data race 없이 세션을 관리
// mutex를 사용하지 않아도 SESSION 추가/검색 시 Data Race가 발생하지 않는다
// 단점 : erase가 안됨 -> atomic<shared_ptr<T>> 사용
// intel tbb를 써야 atomic<shared_ptr<T>> 사용 가능
tbb::concurrent_unordered_map<int,
    std::atomic<std::shared_ptr<SESSION>>> g_clients;


void SESSION::send_add_player(int player_id)
{
    auto it = g_clients.find(player_id);
    if (it == g_clients.end()) return;

    std::shared_ptr<SESSION> target = it->second.load();

    S2C_AddPlayer packet;
    packet.size = sizeof(S2C_AddPlayer);
    packet.type = S2C_ADD_PLAYER;
    packet.playerId = player_id;

    memcpy(packet.username, target->m_username, sizeof(packet.username));
    packet.x = target->m_x;
    packet.y = target->m_y;
    do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void SESSION::process_packet(unsigned char* p)
{
    PACKET_TYPE type = *reinterpret_cast<PACKET_TYPE*>(&p[1]);
    switch (type)
    {
    case C2S_LOGIN: {
        C2S_Login* packet = reinterpret_cast<C2S_Login*>(p);
        strncpy_s(m_username, packet->username, MAX_NAME_LEN);
        cout << "Player[" << m_id << "] logged in as " << m_username << endl;
        send_avatar_info();
    }
                  break;
    case C2S_MOVE: {
        C2S_Move* packet = reinterpret_cast<C2S_Move*>(p);
        DIRECTION dir = packet->dir;
        switch (dir)
        {
        case UP: m_y = max(0, m_y - 1); break;
        case DOWN: m_y = min(WORLD_HEIGHT - 1, m_y + 1); break;
        case LEFT: m_x = max(0, m_x - 1); break;
        case RIGHT: m_x = min(WORLD_WIDTH - 1, m_x + 1); break;
        }
        cout << "Player[" << m_id << "] moved to (" << m_x << ", " << m_y << ")\n";

        for (auto& pair : g_clients)
        {
            std::shared_ptr<SESSION> cl = pair.second.load();
            if (cl->m_state == CS_CONNECT)
            {
                cl->send_move_packet(m_id);
            }
        }
    }
                 break;
    default:
        cout << "Unknown packet type received from player[" << m_id << "].\n";
        break;
    }
}

void SESSION::send_move_packet(int mover)
{
    auto it = g_clients.find(mover);
    std::shared_ptr<SESSION> client = it->second.load();

    S2C_MovePlayer packet;
    packet.size = sizeof(S2C_MovePlayer);
    packet.type = S2C_MOVE_PLAYER;
    packet.playerId = mover;
    packet.x = client->m_x;
    packet.y = client->m_y;
    do_send(packet.size, reinterpret_cast<char*>(&packet));
}

void send_login_fail(SOCKET client, const char* message)
{
    S2C_LoginResult packet;
    packet.size = sizeof(S2C_LoginResult);
    packet.type = S2C_LOGIN_RESULT;
    packet.success = false;
    strncpy_s(packet.message, message, sizeof(packet.message));
    WSABUF wsa_buf;
    wsa_buf.buf = reinterpret_cast<char*>(&packet);
    wsa_buf.len = packet.size;
    WSASend(client, &wsa_buf, 1, 0, 0, nullptr, nullptr);
}

// main 함수의 GQCS 루프를 worker thread로 이전
void worker_thread()
{
    for (;;)
    {
        DWORD num_bytes;
        ULONG_PTR key;
        LPOVERLAPPED over;

        GetQueuedCompletionStatus(h_iocp, &num_bytes, &key, &over, INFINITE);

        // 종료 및 에러 처리
        if (over == nullptr)
        {
            error_display(L"GQCS Error: ", WSAGetLastError());
            continue; // key값이 유효하지 않으므로 여기서 클라이언트 처리를 하면 안 됨
        }

        EXP_OVER* exp_over = reinterpret_cast<EXP_OVER*>(over);

        switch (exp_over->m_iotype)
        {
        case IO_ACCEPT:
        {
            cout << "Client connected." << endl;

            int player_id = player_index++;
            SOCKET client_socket = exp_over->m_client_socket;

            if (MAX_PLAYERS <= g_clients.size())
            {
                cout << "No more player can be accepted." << endl;
                send_login_fail(exp_over->m_client_socket, "Server is full.");
                closesocket(exp_over->m_client_socket);
            } else
            {
                CreateIoCompletionPort((HANDLE)client_socket, h_iocp, player_id, 0);

                // 컨테이너에서 해당 id를 가진 위치 (이터레이터) 찾기
                auto it = g_clients.find(player_id);

                if (it == g_clients.end())
                {
                    auto new_session = std::make_shared<SESSION>(client_socket, player_id);
                    new_session->m_id = player_id;
                    g_clients[player_id].store(new_session);

                    it = g_clients.find(player_id);
                }

                std::shared_ptr<SESSION> session = it->second.load();

                session->m_state = CS_CONNECT;
                session->m_client = client_socket;
                session->m_x = 0;
                session->m_y = 0;
                session->m_id = player_id;
                session->m_prev_recv = 0;

                session->send_login_success();

                for (auto& pair : g_clients)
                {
                    std::shared_ptr<SESSION> other_session = pair.second.load();

                    if (CS_CONNECT != other_session->m_state) continue;
                    if (other_session->m_id == player_id) continue;

                    // 기존 유저에게 내 정보 보내기
                    other_session->send_add_player(player_id);

                    // 나에게(session) 기존 유저 정보 보내기
                    session->send_add_player(other_session->m_id);
                }

                session->do_recv();
            }

            // 다음 접속자를 받기 위해 다시 AcceptEX를 던짐
            SOCKET next_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
            EXP_OVER* next_accept_over = new EXP_OVER(IO_ACCEPT);
            next_accept_over->m_client_socket = next_socket;

            AcceptEx(server, next_socket, &next_accept_over->m_buff, 0,
                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                NULL, &next_accept_over->m_over);

            // 처리한건 delete 해주기
            delete exp_over;
        }
        break;
        case IO_RECV:
        {
            int player_id = static_cast<int>(key);

            if (num_bytes == 0)
            {
                std::cout << "client[" << key << "] Disconnected. (Graceful)\n";

                auto it = g_clients.find(player_id);
                if (it != g_clients.end())
                {
                    std::shared_ptr<SESSION> cl = it->second.load();
                    cl->m_state = CS_LOGOUT;

                    for (auto& other : g_clients)
                    {
                        std::shared_ptr<SESSION> o = other.second.load();
                        if (CS_PLAYING == o->m_state && o->m_id != player_id)
                            o->send_remove_player(player_id);
                    }
                    closesocket(cl->m_client);
                    cl->m_client = INVALID_SOCKET;
                }
                g_clients.unsafe_erase(player_id);

                delete exp_over; // RECV 하려고 던져놨던 낚싯대 지워주기
                continue;
            }

            cout << "Client[" << player_index << "] sent a message." << endl;

            auto it = g_clients.find(player_index);
            std::shared_ptr<SESSION> session = it->second.load();

            unsigned char* p = reinterpret_cast<unsigned char*>(exp_over->m_buff);
            int data_size = num_bytes + session->m_prev_recv;
            while (data_size > 0)
            {
                int packet_size = p[0];
                if (packet_size > data_size) break;
                session->process_packet(p);
                p += packet_size;
                data_size -= packet_size;
            }
            if (data_size > 0)
            {
                memmove(session->m_recv_over.m_buff, p, data_size);
                session->m_prev_recv = data_size;
            }
            session->do_recv();
        }
        break;
        case IO_SEND: {
            cout << "Message sent. to client[" << key << "]\n";
            EXP_OVER* o = reinterpret_cast<EXP_OVER*>(over);
            delete o;
        }
                    break;
        default:
            cout << "Unknown IO type." << endl;
            exit(-1);
            break;
        }
    }
}

int main()
{
    DWORD bytes = 0;
    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 2), &WSAData);
    server = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.S_un.S_addr = INADDR_ANY;
    bind(server, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    listen(server, SOMAXCONN);
    h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

    // 서버 소켓 iocp에 등록
    CreateIoCompletionPort((HANDLE)server, h_iocp, -1, 0);

    // 비동기 accept를 위한 소켓 생성
    SOCKET accept_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

    EXP_OVER* accept_over = new EXP_OVER(IO_ACCEPT);
    accept_over->m_client_socket = accept_socket;

    // new로 만든 EXP_OVER를 AcceptEX 로 낚싯대를 던져둠
    AcceptEx(server, accept_socket, &accept_over->m_buff, 0,
        sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
        &bytes, &accept_over->m_over);

    // 스레드를 담을 벡터 
    vector <thread> worker_threads;

    // 현재 컴퓨터의 CPU 코어 개수를 가져와서 가장 효율적인 스레드 수를 계산한다
    int num_threads = thread::hardware_concurrency();

    // 스레드를 생성하고, worker_thread 함수 실행
    for (int i = 0; i < num_threads; ++i)
        worker_threads.emplace_back(worker_thread);

    // 메인 프로그램은 각 스레드들이 일을 마칠 때까지 대기
    for (auto& th : worker_threads)
        th.join();

    closesocket(server);
    WSACleanup();
}
