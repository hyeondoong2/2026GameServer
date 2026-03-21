#ifndef PLAYER_H_  
#define PLAYER_H_

#include <winsock2.h>
#include <memory>
#include "common.h"

#include "Session.h"

class Session;

class Player
{
public:
    Player(int id) : id_(id)
    {
        x_ = 0;
        y_ = 0;
    }

    ~Player() = default;

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    void SetSession(std::shared_ptr<Session> session)
    {
        session_ = session;
    }

    void UpdatePosition(PlayerMoveDir dir);

    int id() const { return id_; }
    int x() const { return x_; }
    int y() const { return y_; }

private:
    int id_ = -1;
    int x_;
    int y_;
    std::weak_ptr<Session> session_;
};

#endif  // PLAYER_H_
