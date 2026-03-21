#ifndef SESSION_H_
#define SESSION_H_

#include <winsock2.h>

#include <iostream>
#include <memory>

#include "Player.h"
#include "Common.h"
#include "EXP_OVER.h"

class Player;

void recv_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);
void send_callback(DWORD, DWORD, LPWSAOVERLAPPED, DWORD);

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(int id, SOCKET sock) : id_(id), socket_(sock), exp_over_(id) {}
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Init();

    void do_recv();
    void do_send(char* mess);

    int id() const { return id_; }
    SOCKET socket() const { return socket_; }
    std::shared_ptr<Player> player() const { return player_; }
    EXP_OVER& exp_over() { return exp_over_; }  // 원본 주소를 넘겨줘야해서 레퍼런스

private:
    int id_ = -1;
    SOCKET socket_ = INVALID_SOCKET;
    EXP_OVER exp_over_;
    std::shared_ptr<Player> player_;
};

#endif  // SESSION_H_