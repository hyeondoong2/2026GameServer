#include <iostream>
#include <WS2tcpip.h>
#include "Common.h"
#include "thread"
#include "memory.h"
#include "Player.h"
#include "Session.h"
#include "EXP_OVER.h"
#include "Protocol.h"
#include <unordered_map>

using namespace std;
#pragma comment (lib, "WS2_32.LIB")

std::unordered_map<int, std::shared_ptr<Session>> g_sessions;   // id와 session 정보를 가진 unordered_map 컨테이너로 관리

void err_disp(const char* msg, int err_no)
{
    WCHAR* h_mess;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        NULL, err_no,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&h_mess, 0, NULL);
    std::cout << msg;
    wcout << L"  에러 => " << h_mess << endl;
    while (true);
    LocalFree(h_mess);
}

int main()
{
    wcout.imbue(std::locale("korean"));

    WSADATA WSAData;
    WSAStartup(MAKEWORD(2, 0), &WSAData);

    SOCKET server_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
    SOCKADDR_IN server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == SOCKET_ERROR)
    {
        std::cout << "[Error] bind 실패: " << WSAGetLastError() << std::endl;
        return 1;
    }
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cout << "[Error] listen 실패: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // accept
    while (true)
    {
        SOCKADDR_IN cl_addr = {};
        int addr_size = sizeof(SOCKADDR_IN);

        SOCKET client_socket = WSAAccept(server_socket, reinterpret_cast<sockaddr*>(&cl_addr), &addr_size, NULL, NULL);

        if (client_socket == INVALID_SOCKET)
        {
            std::cout << "[Error] accept 실패: " << WSAGetLastError() << std::endl;
            continue;
        }

        // id 부여
        int id = -1;
        for (int i = 0; i < 10; ++i)
        {
            if (!g_sessions[i])
            {
                id = i;
                break;
            }
        }

        if (id == -1)
        {
            std::cout << "서버 가득 참" << std::endl;
            closesocket(client_socket);
            continue;
        }

        auto new_session = std::make_shared<Session>(id, client_socket);
        new_session->Init();
        g_sessions[id] = new_session;
        new_session->do_recv();

        SleepEx(100, true);

        std::cout << "클라이언트 접속 [id: " << id << "]" << std::endl;
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;;
}

// 패킷 처리
void process_packet(std::shared_ptr<Session> session, char* buf)
{
    char type = buf[1];

    switch (type)
    {
    case CS_LOGIN: {
        // 기존 플레이어 정보 새 클라이언트에게 전송
        for (auto& pair : g_sessions)
        {
            if (pair.first == session->id()) continue;
            SC_ENTER_PACKET enter;
            enter.id = pair.first;
            enter.x = pair.second->player()->x();
            enter.y = pair.second->player()->y();
            session->do_send(reinterpret_cast<char*>(&enter));
        }
        // 모든 클라이언트에게 새 플레이어 알림
        SC_ENTER_PACKET enter;
        enter.id = session->id();
        enter.x = session->player()->x();
        enter.y = session->player()->y();
        for (auto& pair : g_sessions)
            pair.second->do_send(reinterpret_cast<char*>(&enter));
        break;
    }
    case CS_KEY: {
        CS_KEY_PACKET* packet = reinterpret_cast<CS_KEY_PACKET*>(buf);
        session->player()->UpdatePosition(packet->move_dir);

        SC_MOVE_PACKET move;
        move.id = session->id();
        move.x = session->player()->x();
        move.y = session->player()->y();
        for (auto& pair : g_sessions)
            pair.second->do_send(reinterpret_cast<char*>(&move));
        break;
    }
    case CS_LOGOUT: {
        SC_LOGOUT_PACKET logout;
        logout.id = session->id();
        for (auto& pair : g_sessions)
            pair.second->do_send(reinterpret_cast<char*>(&logout));
        g_sessions.erase(session->id());
        break;
    }
    }
}

// recv가 다 되면 호출
void recv_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
    if (err != 0 || num_bytes == 0)
    {
        size_t s_id = reinterpret_cast<size_t>(over->hEvent);

        SC_LOGOUT_PACKET logout;
        logout.id = s_id;

        for (auto& pair : g_sessions)
        {
            pair.second->do_send(reinterpret_cast<char*>(&logout));
        }

        g_sessions.erase(s_id);
        return;
    }

    // over가 EXP_OVER의 첫번쨰 멤버라서 바로 캐스팅이 가능
    size_t  s_id = reinterpret_cast<size_t>(over->hEvent);
    auto it = g_sessions.find(static_cast<int>(s_id));
    if (it == g_sessions.end()) return;
    auto session = it->second;

    // send와 독립적으로 실행..
    session->do_recv();

    process_packet(session, session->exp_over().send_msg_);
}

// send 버퍼에 있던 데이터가 다 전송되면 호출
void send_callback(DWORD err, DWORD num_bytes, LPWSAOVERLAPPED over, DWORD flags)
{
    EXP_OVER* ex_over = reinterpret_cast<EXP_OVER*>(over);
    delete over;
}
