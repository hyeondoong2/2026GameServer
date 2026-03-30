#include "Session.h"

Session::~Session()
{
    if (socket_ != INVALID_SOCKET)
    {
        closesocket(socket_);
    }
}

void Session::Init()
{
    player_ = std::make_shared<Player>(id_);
    player_->SetSession(shared_from_this());
}

void Session::do_recv()
{
    DWORD flags = 0;
    WSARecv(socket_, &exp_over_.wsa_buf_, 1, nullptr, &flags, &exp_over_.wsa_over_, recv_callback);
}

void Session::do_send(char* mess)
{
    char size = mess[0];
    EXP_OVER* over = new EXP_OVER(id_, size, mess);
    WSASend(socket_, &over->wsa_buf_, 1, nullptr, 0, &over->wsa_over_, send_callback);
}
