#ifndef PLAYER_H_  
#define PLAYER_H_

#include <winsock2.h>
#include "common.h"

class Player {
public:
  Player(SOCKET socket) : socket_(socket) {
    InitializeCriticalSection(&cs_);
  }
  ~Player() {
    if (socket_ != INVALID_SOCKET) {
      closesocket(socket_);
    }
    DeleteCriticalSection(&cs_);
  }

  SOCKET socket() const { return socket_; }
  PosPacket pos() const { return pos_; };

  void UpdatePosition(PlayerMoveDir dir);


private:
  SOCKET socket_ = INVALID_SOCKET;
  PosPacket pos_{};
  CRITICAL_SECTION cs_;
};

#endif  // PLAYER_H_
