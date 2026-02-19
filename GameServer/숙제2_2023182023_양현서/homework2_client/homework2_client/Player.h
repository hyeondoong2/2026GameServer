#ifndef PLAYER_H_  
#define PLAYER_H_

#include "stdafx.h"

class Player {
public:
  // 일단 플레이어도 하나니까 싱글톤으로..
  static Player& Instance();

  void Init();
  void SetPos(int x, int y);
  void Render(HDC hdc);

private:
  Player() = default;
  ~Player() = default;

  friend std::default_delete<Player>;

  // 복사 및 이동 방지
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;
  Player(Player&&) = delete;
  Player& operator=(Player&&) = delete;

  static std::unique_ptr<Player> instance_;

  int x_ = 0;
  int y_ = 0;
  int size_ = 0;

  CImage player_image_;
};

#endif  // PLAYER_H_