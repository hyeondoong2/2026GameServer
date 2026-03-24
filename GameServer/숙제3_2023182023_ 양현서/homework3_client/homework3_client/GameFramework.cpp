#include "GameFramework.h"
#include "Player.h"
#include "Board.h"
#include "NetworkManager.h"
#include "Protocol.h"

// static 멤버 변수 초기화
std::unique_ptr<GameFramework> GameFramework::instance_ = nullptr;

GameFramework& GameFramework::Instance()
{
    if (instance_ == nullptr)
    {
        instance_.reset(new GameFramework());
    }
    return *instance_;
}

void GameFramework::Init()
{
    hdc_ = GetDC(hwnd_);
    back_dc_ = CreateCompatibleDC(hdc_);
    back_bmp_ = CreateCompatibleBitmap(hdc_, width_, height_);
    old_bmp_ = (HBITMAP)SelectObject(back_dc_, back_bmp_);

    ReleaseDC(hwnd_, hdc_);

    if (!NetworkManager::Instance().Init())
    {
        std::cout << "init 실패" << std::endl;
        return;
    }

    //myplayer_.Init();
    Board::Instance().Init();
}

void GameFramework::Progress()
{
    PatBlt(back_dc_, 0, 0, width_, height_, WHITENESS);

    // send
    CS_KEY_PACKET packet;
    if (has_new_input_)
    {
        packet.move_dir = last_move_dir_;
        WSABUF send_buf = { packet.size, reinterpret_cast<char*>(&packet) };
        DWORD sent_bytes = 0;
        WSASend(NetworkManager::Instance().socket(), &send_buf, 1, &sent_bytes, 0, nullptr, nullptr);
        has_new_input_ = false;
    } else
    {
        packet.move_dir = PlayerMoveDir::kNone;
        WSABUF send_buf = { packet.size, reinterpret_cast<char*>(&packet) };
        DWORD sent_bytes = 0;
        WSASend(NetworkManager::Instance().socket(), &send_buf, 1, &sent_bytes, 0, nullptr, nullptr);
    }

    ProcessPacket();

    // Draw
    Board::Instance().Render(back_dc_);

    for (auto& player : g_players)
    {
        if (player.first != -1)
        {
            player.second->Render(back_dc_);
        }
    }

    HDC hdc = GetDC(hwnd_);
    BitBlt(hdc, 0, 0, width_, height_, back_dc_, 0, 0, SRCCOPY);
    ReleaseDC(hwnd_, hdc);
}

void GameFramework::CleanUp()
{
    SelectObject(back_dc_, old_bmp_);
    DeleteObject(back_bmp_);
    DeleteDC(back_dc_);
}

void GameFramework::ProcessPacket()
{
    static char recv_buffer[BUFSIZE];
    static int total_received = 0;

    WSABUF wsa_buf;
    wsa_buf.buf = recv_buffer + total_received;
    wsa_buf.len = BUFSIZE - total_received;

    DWORD recv_bytes = 0, flags = 0;
    int result = WSARecv(NetworkManager::Instance().socket(), &wsa_buf, 1, &recv_bytes, &flags, nullptr, nullptr);

    if (result == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            PostQuitMessage(0);
        }
        return;
    }

    if (recv_bytes == 0)
    {
        PostQuitMessage(0);
        return;
    }

    total_received += recv_bytes;

    while (total_received >= sizeof(char))
    {
        unsigned char packet_size = static_cast<unsigned char>(recv_buffer[0]);

        if (total_received < packet_size) break;

        char packet_type = recv_buffer[1];
        HandlePacket(packet_type, recv_buffer);

        total_received -= packet_size;
        memmove(recv_buffer, recv_buffer + packet_size, total_received);
    }
}

void GameFramework::HandlePacket(char type, char* ptr)
{
    switch (type)
    {
    case SC_ENTER: {
        SC_ENTER_PACKET* pkt = reinterpret_cast<SC_ENTER_PACKET*>(ptr);
        auto new_player = std::make_shared<Player>(pkt->x, pkt->y);
        g_players.insert({ pkt->id, new_player });
        break;
    }
    case SC_MOVE: {
        SC_MOVE_PACKET* pkt = reinterpret_cast<SC_MOVE_PACKET*>(ptr);
        if (g_players.count(pkt->id))
        {
            g_players[pkt->id]->SetPos(pkt->x, pkt->y);
        }
        break;
    }
    case SC_LOGOUT: {
        SC_LOGOUT_PACKET* pkt = reinterpret_cast<SC_LOGOUT_PACKET*>(ptr);
        g_players.erase(pkt->id);
        break;
    }
    }
}

void GameFramework::OnWindowMessage(HWND hwnd, UINT message_id, WPARAM w_param, LPARAM l_param)
{
    if (message_id != WM_KEYDOWN) return;
    switch (w_param)
    {
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

void GameFramework::set_hinstance(HINSTANCE hinstance)
{
    hinstance_ = hinstance;
}

HINSTANCE GameFramework::hinstance() const
{
    return hinstance_;
}

void GameFramework::set_hwnd(HWND hwnd)
{
    hwnd_ = hwnd;
}

